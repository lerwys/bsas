#ifndef COLLECTOR_H
#define COLLECTOR_H

#include <string>
#include <deque>
#include <map>
#include <set>

#include <epicsTime.h>
#include <epicsMutex.h>
#include <epicsGuard.h>

#include <pv/noDefaultMethods.h>
#include <pv/sharedVector.h>
#include <pv/reftrack.h>
#include <pv/thread.h>
#include <pva/client.h>

#include "collect_pva.h"
#include "subscribable.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

typedef epicsGuard<epicsMutex> Guard;
typedef epicsGuardRelease<epicsMutex> UnGuard;

struct Aggregator {
    virtual ~Aggregator() {}
    virtual void reset(const std::vector<std::string>& n) = 0;
    virtual void aggregate(const std::vector<
            std::pair<epicsUInt64, std::vector<pvd::PVStructurePtr>>>& events) = 0;
};

class Collector : public epicsThreadRunable {
public:
    static size_t num_instances;

    explicit Collector(pvac::ClientProvider& cliprovider,
                       const pvd::shared_vector<const std::string>& names,
                       unsigned int prio,
                       size_t num_work_queue = 4);
    ~Collector();

    void close();
    void notEmpty(Subscribable *sub);

    void addAggregator(Aggregator* agg);
    void removeAggregator(Aggregator* agg);

private:
    struct PV {
        std::shared_ptr<SubscriptionPVA> sub;
        bool ready;
        bool connected;
        PV() :ready(false), connected(false) {}
    };
    std::vector<PV> pvs;

    std::set<Aggregator *> aggregators;
    std::set<Aggregator *> aggregators_shadow;
    bool aggregators_changed;

    epicsMutex mutex;
    epicsEvent wakeup;

    pvac::ClientProvider& cliprovider;
    std::vector<std::unique_ptr<WorkQueuePVA>> work_queue;
    bool waiting;
    bool running;

    epics::pvData::Thread processor;

    // events happened on a certain timestamp, collected for all
    // registered PVs
    std::map<epicsUInt64, std::vector<pvd::PVStructurePtr>> events;
    // completed valid events: expected format, aligned, valid timestamp, all tables present
    std::vector<std::pair<epicsUInt64, std::vector<pvd::PVStructurePtr>>> completed_events;
    epicsTimeStamp now;
    epicsUInt64 now_key;
    epicsUInt64 oldest_key; // oldest key sent to Aggregators

    // override Worker
    virtual void run() override final;

    EPICS_NOT_COPYABLE(Collector)
};

#endif // COLLECTOR_H
