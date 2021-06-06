
#include <string.h>

#include <stdexcept>
#include <sstream>

#include <errlog.h>
#include <epicsThread.h>
#include <db_access.h>
#include <cadef.h>
#include <pv/reftrack.h>

#include "collector.h"
#include "collect_ca.h"

#include <epicsExport.h>

namespace pvd = epics::pvData;

int collectorCaDebug;

double collectorCaScalarMaxRate = 140.0;
double collectorCaArrayMaxRate = 1.5;

namespace {

struct eca_error : public std::runtime_error
{
    explicit eca_error(int code, const char *msg =0) :std::runtime_error(buildMsg(code)) {}

    static std::string buildMsg(int code, const char *msg =0) {
        std::ostringstream strm;
        strm<<(msg?msg:"CA Error")<<" : "<<ca_message(code);
        return strm.str();
    }

    static void check(int code, const char *msg =0) {
        if(code!=ECA_NORMAL)
            throw eca_error(code, msg);
    }
};

void onError(exception_handler_args args)
{
    errlogPrintf("Collector CA exception on %s : %s on %s:%u\n%s",
                 args.chid?ca_name(args.chid):"<unknown>",
                 ca_message(args.stat),
                 args.pFile, args.lineNo, args.ctx);
}

} // namespace

size_t DBRValue::Holder::num_instances;

DBRValue::Holder::Holder()
    :sevr(4), stat(LINK_ALARM), count(1u)
{
    REFTRACE_INCREMENT(num_instances);
    ts.secPastEpoch = 0;
    ts.nsec = 0;
}

DBRValue::Holder::~Holder()
{
    REFTRACE_DECREMENT(num_instances);
}

size_t CAContext::num_instances;

CAContext::CAContext(unsigned int prio, bool fake)
    :context(0)
{
    REFTRACE_INCREMENT(num_instances);
    if(fake) return;

    epicsThreadId me = epicsThreadGetIdSelf();
    unsigned int orig_prio = epicsThreadGetPrioritySelf();

    // the CA context we create will inherit our priority
    epicsThreadSetPriority(me, prio);

    struct ca_client_context *current = ca_current_context();
    if(current)
        ca_detach_context();
    try {
        int err = ca_context_create(ca_enable_preemptive_callback);
        eca_error::check(err, "Create context");

        context = ca_current_context();
        if(!context)
            throw std::logic_error("Create context, but not really?");

        err = ca_add_exception_event(&onError, 0);
        eca_error::check(err, "Change CA exception handler");

        ca_detach_context();

    }catch(...){
        if(ca_current_context())
            ca_detach_context();
        if(current)
            ca_attach_context(current);
        epicsThreadSetPriority(me, orig_prio);
        throw;
    }
    if(current)
        ca_attach_context(current);
    epicsThreadSetPriority(me, orig_prio);
}

CAContext::~CAContext()
{
    REFTRACE_DECREMENT(num_instances);

    if(!context) return;

    struct ca_client_context *current = ca_current_context();
    if(current)
        ca_detach_context();

    ca_attach_context(context);
    ca_context_destroy();

    if(current)
        ca_attach_context(current);
}

CAContext::Attach::Attach(const CAContext &ctxt)
    :previous(ca_current_context())
{
    if(previous)
        ca_detach_context();

    ca_attach_context(ctxt.context);
}

CAContext::Attach::~Attach()
{
    ca_detach_context();

    if(previous)
        ca_attach_context(previous);
}

size_t Subscription::num_instances;

Subscription::Subscription(const CAContext &context,
                           size_t column,
                           const std::string& pvname,
                           Collector &collector)
    :pvname(pvname)
    ,context(context)
    ,collector(collector)
    ,column(column)
    ,chid(0)
    ,evid(0)
    ,connected(false)
    ,nDisconnects(0u)
    ,lDisconnects(0u)
    ,nErrors(0u)
    ,lErrors(0u)
    ,nUpdates(0u)
    ,lUpdates(0u)
    ,nUpdateBytes(0u)
    ,lUpdateBytes(0u)
    ,nOverflows(0u)
    ,lOverflows(0u)
    ,limit(16u) // arbitrary, will be overwritten during first data update
{
    REFTRACE_INCREMENT(num_instances);

    last_event.secPastEpoch = 0;
    last_event.nsec = 0;

    if(!context.context) return;

    CAContext::Attach A(context);

    int err = ca_create_channel(pvname.c_str(), &onConnect, this, 0, &chid);
    eca_error::check(err, "Create Channel");
    if(collectorCaDebug>0) {
        errlogPrintf("Create Channel to '%s'\n", pvname.c_str());
    }
}

Subscription::~Subscription()
{
    close();
    REFTRACE_DECREMENT(num_instances);
}

const size_t Subscription::get_column()
{
    return column;
}

const std::string Subscription::get_pvname()
{
    return pvname;
}

void Subscription::close()
{
    if(!context.context) return;

    {
        Guard G(mutex);
        if(!chid) return;
    }
    CAContext::Attach A(context);

    if(collectorCaDebug>0) {
        errlogPrintf("Clear Channel to '%s'\n", pvname.c_str());
    }

    int err = ca_clear_channel(chid); // implies ca_clear_subscription
    // any callbacks are complete now;
    {
        Guard G(mutex);
        chid = 0;
        evid = 0;
    }
    eca_error::check(err);
}

void Subscription::clear(size_t remain)
{
    Guard G(mutex);
    while(values.size()>remain) {
        values.pop_front();
        nOverflows++;
    }

}

const std::deque<std::tr1::shared_ptr<RValue>>& Subscription::get_values()
{
    return values;
}

std::tr1::shared_ptr<RValue> Subscription::pop()
{
    std::tr1::shared_ptr<RValue> ret =
        std::tr1::shared_ptr<DBRValue>(new DBRValue());
    {
        Guard G(mutex);
        if(!values.empty()) {
            ret = values.front();
            values.pop_front();
        }
    }
    return ret;
}

void Subscription::push(const RValue &v)
{
    assert(!context.context); // only call in unittest code
    {
        Guard G(mutex);
        DBRValue temp(dynamic_cast<const DBRValue&>(v));
        _push(temp);
    }
}

// assume locked
void Subscription::_push(RValue& v)
{
    while(values.size() > limit) {
        // we drop newest element to maximize chance of overlapping with lower rate PVs
        values.pop_front();
        nOverflows++;
    }

    values.push_back(std::tr1::shared_ptr<DBRValue>(new DBRValue()));
    values.back()->swap(v);
}

void Subscription::onConnect (struct connection_handler_args args)
{
    Subscription *self = static_cast<Subscription*>(ca_puser(args.chid));
    if(collectorCaDebug>0)
        errlogPrintf("%s %sconnected\n", ca_name(args.chid), (args.op==CA_OP_CONN_UP)?"":"dis");
    try {
        // on CA worker thread

        if(args.op==CA_OP_CONN_UP) {
            short native = ca_field_type(args.chid);
            short promoted = dbf_type_to_DBR_TIME(native);
            unsigned long maxcnt = ca_element_count(args.chid);

            if(native==DBF_STRING) {
                errlogPrintf("%s DBF_STRING not supported, ignoring\n", self->pvname.c_str());
                return;
            }

            // subscribe 0 triggers dynamic array size
            int err = ca_create_subscription(promoted, 0, args.chid, DBE_VALUE|DBE_ALARM, &onEvent, self, &self->evid);
            eca_error::check(err);

            {
                Guard G(self->mutex);
                self->last_event.secPastEpoch = 0;
                self->last_event.nsec = 0;
                self->connected = true;
                self->limit = std::max(size_t(4u), size_t(bsasFlushPeriod*(maxcnt!=1u ? collectorCaArrayMaxRate : collectorCaScalarMaxRate)));
            }

        } else if(args.op==CA_OP_CONN_DOWN) {

            if(!self->evid) return; // unsupported DBF_STRING

            const int err = ca_clear_subscription(self->evid);
            self->evid = 0;

            DBRValue val(new DBRValue::Holder);
            epicsTimeStamp ts;
            epicsTimeGetCurrent(&ts);
            val.set_ts(ts);

            bool notify;
            {
                Guard G(self->mutex);
                notify = self->values.empty();

                self->connected = false;
                self->nDisconnects++;

                self->_push(val);
            }

            if(notify) {
                self->collector.notEmpty(self);
            }

            eca_error::check(err);

        } else {
            // shouldn't happen, but ignore if it does
        }
    } catch(std::exception& err) {
        errlogPrintf("Unexpected exception in Subscription::onConnect() for \"%s\" : %s\n", ca_name(args.chid), err.what());

        Guard G(self->mutex);
        self->nErrors++;
    }
}

void Subscription::onEvent (struct event_handler_args args)
{
    Subscription *self = static_cast<Subscription*>(args.usr);
    if(collectorCaDebug>1)
        errlogPrintf("%s event dbr:%ld count:%ld\n", ca_name(args.chid), args.type, args.count);
    try {

        if(!dbr_type_is_TIME(args.type))
            throw std::runtime_error("CA server doesn't honor DBR_TIME_*");

        size_t count = args.count;
        size_t elem_size = dbr_value_size[args.type];
        size_t size = dbr_size_n(args.type, args.count);

        // workaround for zero-length array
        // https://bugs.launchpad.net/epics-base/+bug/1242919
        if(args.count==0 && size > elem_size)
            size -= elem_size;

        pvd::ScalarType type;
        switch(args.type) {
        case DBR_TIME_STRING: type = pvd::pvString; break;
        case DBR_TIME_SHORT:  type = pvd::pvShort; break;
        case DBR_TIME_FLOAT:  type = pvd::pvFloat; break;
        case DBR_TIME_ENUM:   type = pvd::pvShort; break;
        case DBR_TIME_CHAR:   type = pvd::pvByte; break;
        case DBR_TIME_LONG:   type = pvd::pvInt; break;
        case DBR_TIME_DOUBLE: type = pvd::pvDouble; break;
        default:
            // treat any unknown as byte array
            type = pvd::pvByte; break;
        }

        // all of the dbr_time_* structs have the same prefix for alarm and timestamp
        dbr_time_double meta;
        // dbr_time_double includes space for the first value, but we don't want to copy this now
        memcpy(&meta, args.dbr, offsetof(dbr_time_double, value));

        pvd::shared_vector<void> buf;
        if(type!=pvd::pvString) {
            buf = pvd::ScalarTypeFunc::allocArray(type, count);

            if(buf.size() != elem_size*count)
                throw std::logic_error("DBR buffer size computation error");

            memcpy(buf.data(),
                   dbr_value_ptr(args.dbr, args.type),
                   buf.size());

        } else {
            // TODO: not currently used

            Guard G(self->mutex);

            self->nErrors++;
            self->nOverflows++;
            if(collectorCaDebug>0) {
                errlogPrintf("%s DBF_STRING not supported, ignoring\n", self->pvname.c_str());
            }
            return;
        }

        DBRValue val(new DBRValue::Holder);
        val.set_sevr(meta.severity);
        val.set_stat(meta.status);
        val.set_ts(meta.stamp);
        val.set_count(count);
        val.set_buffer(pvd::freeze(buf));

        bool notify;
        {
            Guard G(self->mutex);

            self->nUpdates++;
            /* Assumptions and approximations in bandwidth usage calculation.
             * Assume Ethernet with MTU 1500.
             * No IP fragmentation.
             * No IP or TCP header options after SYN.
             * Only one (partial) subscription per frame (worst case).
             * Ignore other IOC -> client traffic.
             *
             * 14 bytes - ethernet header
             * 20 bytes - IP header
             * 32 bytes - TCP header
             * 16 bytes - CA header
             * 16 bytes - DBR_TIME_* meta-data
             *
             * 98+1402 body bytes in the first frame. 66+1434 in subsequent frames.
             */
            self->nUpdateBytes += size + 98u;
            if(size > 1402u) {
                self->nUpdateBytes += 66u*(1u + (size-1402u)/1434u);
            }


            if(epicsTimeDiffInSeconds(&meta.stamp, &self->last_event) > 0.0) {
                notify = self->values.empty();

                self->_push(val);
            } else {
                self->nErrors++;
                notify = false;

                if(collectorCaDebug>2) {
                    errlogPrintf("%s ignoring non-monotonic TS\n", self->pvname.c_str());
                }
            }
            self->last_event = meta.stamp;
        }

        if(notify) {
            self->collector.notEmpty(self);
        }

    } catch(std::exception& err) {
        errlogPrintf("Unexpected exception in Subscription::onEvent() for \"%s\" : %s\n", ca_name(args.chid), err.what());

        Guard G(self->mutex);
        self->nErrors++;
    }
}

extern "C" {
epicsExportAddress(int, collectorCaDebug);
epicsExportAddress(double, collectorCaScalarMaxRate);
epicsExportAddress(double, collectorCaArrayMaxRate);
}
