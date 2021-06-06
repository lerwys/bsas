#ifndef COLLECT_CA_H
#define COLLECT_CA_H

#include <string>
#include <deque>

#include <epicsTime.h>
#include <epicsMutex.h>
#include <epicsGuard.h>
#include <alarm.h>
#include <pv/noDefaultMethods.h>
#include <pv/sharedVector.h>

#include "collectible.h"

typedef epicsGuard<epicsMutex> Guard;
typedef epicsGuardRelease<epicsMutex> UnGuard;

// cf. cadef.h
struct ca_client_context;
struct oldChannelNotify;
struct oldSubscription;
struct connection_handler_args;

struct Collector;

struct DBRValue : public RValue {
public:
    struct Holder {
        static size_t num_instances;

        epicsTimeStamp ts; // in epics epoch
        epicsUInt16 sevr, // [0-3] or 4 (Disconnect)
                    stat; // status code a la Base alarm.h
        epicsUInt32 count;
        epics::pvData::shared_vector<const void> buffer; // contains DBF_* mapped to pvd:pv* code
        Holder();
        ~Holder();
    };
private:
    std::tr1::shared_ptr<Holder> held;
public:

    DBRValue() {}
    DBRValue(Holder *H) :held(H) {}

    virtual epicsTimeStamp get_ts() OVERRIDE FINAL {
        return held->ts;
    }
    virtual void set_ts(epicsTimeStamp ts) OVERRIDE FINAL {
        held->ts = ts;
    }
    virtual epicsUInt16 get_sevr() OVERRIDE FINAL {
        return held->sevr;
    }
    virtual void set_sevr(epicsUInt16 sevr) OVERRIDE FINAL {
        held->sevr = sevr;
    }
    virtual epicsUInt16 get_stat() OVERRIDE FINAL {
        return held->stat;
    }
    virtual void set_stat(epicsUInt16 stat) OVERRIDE FINAL {
        held->stat = stat;
    }
    virtual epicsUInt32 get_count() OVERRIDE FINAL {
        return held->count;
    }
    virtual void set_count(epicsUInt32 count) OVERRIDE FINAL {
        held->count = count;
    }
    virtual const epics::pvData::shared_vector<const void>& get_buffer() OVERRIDE FINAL {
        return held->buffer;
    }
    virtual void set_buffer(const epics::pvData::shared_vector<const void>& buffer) OVERRIDE FINAL {
        held->buffer = buffer;
    }
    virtual bool valid() const OVERRIDE FINAL {
        return !!held;
    }
    virtual void swap(RValue& o) OVERRIDE FINAL {
        _swap(dynamic_cast<DBRValue &>(o));
    }
    virtual void reset() OVERRIDE FINAL {
        held.reset();
    }

private:
    void _swap(DBRValue& o) {
        held.swap(o.held);
    }
};

struct CAContext {
    static size_t num_instances;

    explicit CAContext(unsigned int prio, bool fake=false);
    ~CAContext();

    struct ca_client_context *context;

    // manage attachment of a context to the current thread
    struct Attach {
        struct ca_client_context *previous;
        Attach(const CAContext&);
        ~Attach();
    };

    EPICS_NOT_COPYABLE(CAContext)
};

struct Subscription : public Subscribable {
    static size_t num_instances;

    Subscription(const CAContext& context,
                 size_t column,
                 const std::string& pvname,
                 Collector& collector);
    ~Subscription();

    virtual const size_t get_column() OVERRIDE FINAL;
    virtual const std::string get_pvname() OVERRIDE FINAL;
    virtual void close() OVERRIDE FINAL;
    virtual std::tr1::shared_ptr<RValue> pop() OVERRIDE FINAL;
    virtual void clear(size_t remain) OVERRIDE FINAL;
    virtual const std::deque<std::tr1::shared_ptr<RValue>>& get_values() OVERRIDE FINAL;

    // for test code only
    virtual void push(const RValue& v) OVERRIDE FINAL;

private:
    void _push(RValue& v);

    static void onConnect (struct connection_handler_args args);
    static void onEvent (struct event_handler_args args);

    const std::string pvname;
    const CAContext& context;
    Collector& collector;
    const size_t column;

    // set before callbacks are possible, cleared after callbacks are impossible
    struct oldChannelNotify *chid;
    // effectively a local of a CA worker, set and cleared from onConnect()
    struct oldSubscription *evid;

    epicsTimeStamp last_event;

    std::deque<std::tr1::shared_ptr<RValue>> values;

    // properties
    DEFINE_IMPL_PROP_METHODS(connected, size_t)
    DEFINE_IMPL_PROP_METHODS2(Disconnects, size_t, n, l)
    DEFINE_IMPL_PROP_METHODS2(Errors, size_t, n, l)
    DEFINE_IMPL_PROP_METHODS2(Updates, size_t, n, l)
    DEFINE_IMPL_PROP_METHODS2(UpdateBytes, size_t, n, l)
    DEFINE_IMPL_PROP_METHODS2(Overflows, size_t, n, l)
    DEFINE_IMPL_PROP_METHODS(limit, size_t)

    EPICS_NOT_COPYABLE(Subscription)
};

#endif // COLLECT_CA_H
