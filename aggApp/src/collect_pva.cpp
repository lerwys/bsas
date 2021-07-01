#include <iostream>
#include <sstream>

#include <errlog.h>
#include <epicsTypes.h>
#include <epicsExport.h>

#include <pv/standardField.h>
#include <pv/createRequest.h>

#include "collect_pva.h"
#include "collector.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

static const size_t MAX_ROWS = 1000;

namespace {

pvd::StructureConstPtr type_placeholder_table(pvd::getFieldCreate()->createFieldBuilder()
                                   ->setId("epics:nt/NTTable:1.0")
                                   ->addArray("labels", pvd::pvString)
                                   ->addNestedStructure("value")
                                       ->addArray("secondsPastEpoch",  pvd::pvUInt)
                                       ->addArray("nanoseconds",  pvd::pvUInt)
                                       ->addArray("count0",  pvd::pvUInt)
                                       ->addArray("count1",  pvd::pvUInt)
                                       ->addArray("count2",  pvd::pvUInt)
                                       ->addArray("count3",  pvd::pvUInt)
                                       ->addArray("count4",  pvd::pvUInt)
                                       ->addArray("count5",  pvd::pvUInt)
                                       ->addArray("count6",  pvd::pvUInt)
                                       ->addArray("count7",  pvd::pvUInt)
                                       ->addArray("count8",  pvd::pvUInt)
                                       ->addArray("count9",  pvd::pvUInt)
                                       ->addArray("count10", pvd::pvUInt)
                                       ->addArray("count11", pvd::pvUInt)
                                       ->addArray("count12", pvd::pvUInt)
                                       ->addArray("count13", pvd::pvUInt)
                                       ->addArray("count14", pvd::pvUInt)
                                       ->addArray("count15", pvd::pvUInt)
                                   ->endNested()
                                   ->add("alarm", pvd::getStandardField()->alarm())
                                   ->add("timeStamp", pvd::getStandardField()->timeStamp())
                                   ->createStructure());
} //namespace

int collectorPvaDebug;

double collectorPvaScalarMaxRate = 140.0;
double collectorPvaArrayMaxRate = 1.5;

size_t WorkQueuePVA::num_instances;

WorkQueuePVA::WorkQueuePVA(unsigned int prio)
    : running(true)
    , worker(pvd::Thread::Config()
             .name("PVA Monitor handler")
             .prio(prio)
             .autostart(true)
             .run(this)
             )
{
    REFTRACE_INCREMENT(num_instances);
}

WorkQueuePVA::~WorkQueuePVA()
{
    REFTRACE_DECREMENT(num_instances);
    close();
}

void WorkQueuePVA::close()
{
    {
        Guard G(mutex);
        running = false;
    }
    event.signal();
    worker.exitWait();
}

void WorkQueuePVA::push(const std::weak_ptr<Worker>& cb, const pvac::MonitorEvent& evt)
{
    bool wake;
    {
        Guard G(mutex);
        if(!running) return; // silently refuse to queue during/after close()
        wake = queue.empty();
        queue.push_back(std::make_pair(cb, evt));
    }
    if(wake) {
        event.signal();
    }
}

void WorkQueuePVA::run()
{
    Guard G(mutex);

    while(running) {
        if(queue.empty()) {
            UnGuard U(G);
            event.wait();
        } else {
            queue_t::value_type ent(queue.front());
            value_type cb(ent.first.lock());
            queue.pop_front();
            if(!cb)
                continue;

            try {
                UnGuard U(G);
                cb->process(ent.second);
            } catch(std::exception& e){
                errlogPrintf("WorkQueuePVA: Error in monitor handler: '%s'\n", e.what());
            }
        }
    }
}

size_t SubscriptionPVA::num_instances;

SubscriptionPVA::SubscriptionPVA(pvac::ClientProvider& provider,
        WorkQueuePVA& monwork,
        const std::string& pvname,
        const pvd::PVStructurePtr& pvRequest,
        Collector &collector,
        size_t idx)
    :pvname(pvname)
    ,provider(provider)
    ,monwork(monwork)
    ,collector(collector)
    ,idx(idx)
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

    chan = provider.connect(pvname);
    // register connection callback
    chan.addConnectListener(this);
    mon = chan.monitor(this, pvRequest);
}

SubscriptionPVA::~SubscriptionPVA()
{
    REFTRACE_DECREMENT(num_instances);
    close();
}

void SubscriptionPVA::close()
{
    {
        Guard G(mutex);
        chan.removeConnectListener(this);
    }
    if(collectorPvaDebug>0) {
        errlogPrintf("SubscriptionPVA: clear Channel to '%s'\n", pvname.c_str());
    }
}

size_t SubscriptionPVA::get_idx()
{
    return idx;
}

// running on internal provider worker thread. Keep it short
void SubscriptionPVA::connectEvent(const pvac::ConnectEvent& evt)
{
    try {
        if(collectorPvaDebug>0)
            errlogPrintf("SubscriptionPVA: %s %sconnected\n", pvname.c_str(), (evt.connected)?"":"dis");

        if(evt.connected) {
            Guard G(mutex);
            last_event.secPastEpoch = 0;
            last_event.nsec = 0;
            connected = true;
            limit = std::max(size_t(4u), size_t((collectorPvaArrayMaxRate)));
        }
        else {
            auto val = createDefaultPV();

            bool notify;
            {
                Guard G(mutex);
                notify = values.empty();

                connected = false;
                nDisconnects++;

                _push(val);
            }

            if(notify) {
                collector.notEmpty(this);
            }
        }

    } catch(std::exception& err) {
        errlogPrintf("Unexpected exception in SubscriptionPVA::connectEvent() for \"%s\" : %s\n", pvname.c_str(), err.what());

        Guard G(mutex);
        nErrors++;
    }
}

// running on internal provider worker thread. Keep it short.
void SubscriptionPVA::monitorEvent(const pvac::MonitorEvent& evt)
{
    // shared_from_this() will fail as Cancel is delivered in our dtor.
    if(evt.event==pvac::MonitorEvent::Cancel)
        return;

    // running on internal provider worker thread
    // minimize work here and push it to work queue
    monwork.push(shared_from_this(), evt);
}

// running on our worker thread
void SubscriptionPVA::process(const pvac::MonitorEvent& evt)
{
    try {
        switch(evt.event) {
            case pvac::MonitorEvent::Fail:
                errlogPrintf("SubscriptionPVA: %s Error %s\n", mon.name().c_str(), evt.message.c_str());
                break;
            case pvac::MonitorEvent::Cancel:
                errlogPrintf("SubscriptionPVA: %s <Cancel>\n", mon.name().c_str());
                break;
            case pvac::MonitorEvent::Disconnect:
                errlogPrintf("SubscriptionPVA: %s <Disconnect>\n", mon.name().c_str());
                valid.clear();
                break;
            case pvac::MonitorEvent::Data:
                {
                    unsigned n;
                    // tune this value, maybe
                    for(n=0; n<2 && mon.poll(); n++) {
                        std::ostringstream val;
                        valid |= mon.changed;

                        try {
                            // mon.root only != NULL after poll()
                            auto& pv_type = mon.root->getStructure();
                            // get the size of just one array and say it's the same for all
                            auto pv_sec_size = mon.root->getSubFieldT<pvd::PVUIntArray>("value.secondsPastEpoch")->
                                getLength();

                            if(collectorPvaDebug>1) {
                                errlogPrintf("SubscriptionPVA: %s event ID: %s count: %ld\n", pvname.c_str(),
                                        pv_type->getID().empty()? "(empty)":pv_type->getID().c_str(), pv_sec_size);

                                pvd::PVStructure::Formatter fmt(mon.root->stream()
                                        .format(pvd::PVStructure::Formatter::NT));
                                val << fmt;
                                errlogPrintf("SubscriptionPVA: %s channel value:\n%s\n", pvname.c_str(),val.str().c_str());
                            }

                            // copy temp with received values
                            auto temp = createDefaultPV();
                            auto temp_val = temp->getSubFieldT<pvd::PVStructure>("value");
                            auto val = mon.root->getSubFieldT<pvd::PVStructure>("value");
                            for(auto& it : val->getPVFields()) {
                                auto val_field_name = it->getFieldName();
                                auto val_field = val->getSubFieldT<pvd::PVUIntArray>(val_field_name);
                                auto val_field_length = val_field->getLength();
                                // FIXME: how to get the ScalarType from the PVUIntArray field?
                                auto const val_field_stype = pvd::pvUInt;

                                auto buf = pvd::ScalarTypeFunc::allocArray<val_field_stype>(val_field_length);
                                std::copy(val_field->view().begin(), val_field->view().end(), buf.begin());

                                auto temp_field = temp_val->getSubFieldT<pvd::PVUIntArray>(val_field_name);
                                temp_field->putFrom(pvd::freeze(buf));
                            }

                            // FIXME: how to copy timestamp/alarm automatically?

                            bool notify;
                            {
                                Guard G(mutex);
                                notify = values.empty();

                                _push(temp);
                            }

                            if(notify) {
                                collector.notEmpty(this);
                            }
                        }
                        catch(std::exception& e) {
                            errlogPrintf("SubscriptionPVA: could not get value from channel %s: %s\n",
                                    mon.name().c_str(), e.what());
                        }
                    }
                    if(n==2) {
                        // too many updates, re-queue to balance with others
                        monwork.push(shared_from_this(), evt);
                    } else if(n==0) {
                        errlogPrintf("SubscriptionPVA: %s Spurious data event on channel\n", mon.name().c_str());
                    }
                }
                break;
        }
    }
    catch(std::exception& e) {
        errlogPrintf("SubscriptionPVA: Unexpected exception in SubscriptionPVA::onEvent() for \"%s\" : %s\n", mon.name().c_str(), e.what());

        Guard G(mutex);
        nErrors++;
    }
}

const std::string SubscriptionPVA::get_pvname()
{
    return pvname;
}

void SubscriptionPVA::clear(size_t remain)
{
    Guard G(mutex);
    while(values.size()>remain) {
        values.pop_front();
        nOverflows++;
    }
}

pvd::PVStructurePtr SubscriptionPVA::pop()
{
    auto ret = pvd::getPVDataCreate()->createPVStructure(type_placeholder_table);
    {
        Guard G(mutex);
        if(!values.empty()) {
            ret = values.front();
            values.pop_front();
        }
    }
    return ret;
}


pvd::PVStructurePtr SubscriptionPVA::createDefaultPV()
{
    // Add labels to placeholder strucutre columns
    auto val = pvd::getPVDataCreate()->createPVStructure(type_placeholder_table);
    {
        pvd::shared_vector<std::string> labels;
        labels.push_back("sec");
        labels.push_back("ns");
        labels.push_back("count0");
        labels.push_back("count1");
        labels.push_back("count2");
        labels.push_back("count3");
        labels.push_back("count4");
        labels.push_back("count5");
        labels.push_back("count6");
        labels.push_back("count7");
        labels.push_back("count8");
        labels.push_back("count9");
        labels.push_back("count10");
        labels.push_back("count11");
        labels.push_back("count12");
        labels.push_back("count13");
        labels.push_back("count14");
        labels.push_back("count15");

        auto flabel = val->getSubFieldT<pvd::PVStringArray>("labels");
        flabel->replace(pvd::freeze(labels));
    }

    epicsTimeStamp ts;
    epicsTimeGetCurrent(&ts);

    // add timestamp to placeholder strucutre
    {
        pvd::shared_vector<pvd::uint32> ns;
        pvd::shared_vector<pvd::uint32> sec;

        ns.push_back(ts.secPastEpoch + POSIX_TIME_AT_EPICS_EPOCH);
        sec.push_back(ts.nsec);

        auto fsec = val->getSubFieldT<pvd::PVUIntArray>("value.secondsPastEpoch");
        fsec->replace(pvd::freeze(sec));
        auto fns = val->getSubFieldT<pvd::PVUIntArray>("value.nanoseconds");
        fns->replace(pvd::freeze(ns));
    }

    return val;
}

// assume locked
void SubscriptionPVA::_push(std::shared_ptr<pvd::PVStructure> v)
{
    while(values.size() > limit) {
        // we drop newest element to maximize chance of overlapping with lower rate PVs
        values.pop_front();
        nOverflows++;
    }

    values.push_back(v);
}

extern "C" {
epicsExportAddress(int, collectorPvaDebug);
epicsExportAddress(double, collectorPvaScalarMaxRate);
epicsExportAddress(double, collectorPvaArrayMaxRate);
}
