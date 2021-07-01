#ifndef COLLECT_PVA_H
#define COLLECT_PVA_H

#include <string>
#include <deque>

#include <epicsTime.h>
#include <epicsMutex.h>
#include <epicsGuard.h>

#include <alarm.h>
#include <pv/noDefaultMethods.h>
#include <pv/sharedVector.h>
#include <pv/reftrack.h>
#include <pv/thread.h>
#include <pva/client.h>

#include "subscribable.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

typedef epicsGuard<epicsMutex> Guard;
typedef epicsGuardRelease<epicsMutex> UnGuard;

// From pvAccess examples/monitorme.cpp and pvtoolsSrc/pvget.cpp
struct Worker {
    virtual ~Worker() {}
    virtual void process(const pvac::MonitorEvent& event) =0;
};

// simple work queue with thread.
// moves monitor queue handling off of PVA thread(s)
class WorkQueuePVA : public epicsThreadRunable {
public:
    static size_t num_instances;

    WorkQueuePVA(unsigned int prio);
    ~WorkQueuePVA();

    void close();
    void push(const std::tr1::weak_ptr<Worker>& cb, const pvac::MonitorEvent& evt);

private:
    epicsMutex mutex;
    typedef std::tr1::shared_ptr<Worker> value_type;
    typedef std::tr1::weak_ptr<Worker> weak_type;
    // work queue holds only weak_ptr
    // so jobs must be kept alive seperately
    typedef std::deque<std::pair<weak_type, pvac::MonitorEvent> > queue_t;
    queue_t queue;
    epicsEvent event;
    bool running;
    pvd::Thread worker;

    // Thread handler
    virtual void run() override final;
};

class Collector;

class SubscriptionPVA :  public Subscribable,
                         public pvac::ClientChannel::ConnectCallback,
                         public pvac::ClientChannel::MonitorCallback,
                         public Worker,
                         public std::enable_shared_from_this<SubscriptionPVA>
{
public:
    static size_t num_instances;

    SubscriptionPVA(pvac::ClientProvider& provider,
                 WorkQueuePVA& monwork,
                 const std::string& pvname,
                 const pvd::PVStructurePtr& pvRequest,
                 Collector& collector,
                 size_t idx);
    ~SubscriptionPVA();

    //overrides Subscribable
    virtual const std::string get_pvname() override final;
    virtual void close() override final;
    virtual size_t get_idx() override final;
    virtual pvd::PVStructurePtr pop()  override final;
    virtual void clear(size_t remain)  override final;

private:
    const std::string pvname;
    pvac::ClientProvider& provider;
    WorkQueuePVA& monwork;
    pvac::ClientChannel chan;
    pvac::Monitor mon;
    Collector& collector;
    size_t idx;

    epicsMutex mutex;

    pvd::BitSet valid; // only access for process()

    epicsTimeStamp last_event;

    std::deque<pvd::PVStructurePtr> values;

    pvd::PVStructurePtr createDefaultPV();

    void _push(pvd::PVStructurePtr v);

    // override ConnectCallback
    virtual void connectEvent(const pvac::ConnectEvent& evt) override final;
    // override MonitorCallback
    virtual void monitorEvent(const pvac::MonitorEvent& evt) override final;
    // override Worker
    virtual void process(const pvac::MonitorEvent& evt) override final;

    // properties
    DEFINE_IMPL_PROP_METHODS(connected, size_t)
    DEFINE_IMPL_PROP_METHODS2(Disconnects, size_t, n, l)
    DEFINE_IMPL_PROP_METHODS2(Errors, size_t, n, l)
    DEFINE_IMPL_PROP_METHODS2(Updates, size_t, n, l)
    DEFINE_IMPL_PROP_METHODS2(UpdateBytes, size_t, n, l)
    DEFINE_IMPL_PROP_METHODS2(Overflows, size_t, n, l)
    DEFINE_IMPL_PROP_METHODS(limit, size_t)

    EPICS_NOT_COPYABLE(SubscriptionPVA)
};

#endif // COLLECT_PVA_H
