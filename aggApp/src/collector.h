#ifndef COLLECTOR_H
#define COLLECTOR_H

#include <string>
#include <deque>

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

private:
    struct PV {
        std::shared_ptr<SubscriptionPVA> sub;
        bool ready;
        bool connected;
        PV() :ready(false), connected(false) {}
    };
    std::vector<PV> pvs;

    epicsMutex mutex;
    epicsEvent wakeup;

    pvac::ClientProvider& cliprovider;
    std::vector<std::unique_ptr<WorkQueuePVA>> work_queue;
    bool waiting;
    bool running;

    epics::pvData::Thread processor;

    epicsTimeStamp now;
    epicsUInt64 now_key,
                oldest_key; // oldest key sent to Receviers

    // override Worker
    virtual void run() override final;

    EPICS_NOT_COPYABLE(Collector)
};

#endif // COLLECTOR_H
