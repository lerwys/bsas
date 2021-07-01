#include <errlog.h>
#include <pv/reftrack.h>
#include <pv/pvAccess.h>
#include <pv/pvData.h>

#include "collector.h"

#include <epicsExport.h>

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

int collectorDebug;

size_t Collector::num_instances;

Collector::Collector(pvac::ClientProvider& cliprovider,
                       const pvd::shared_vector<const std::string>& names,
                       unsigned int prio)
    :cliprovider(cliprovider)
    ,waiting(false)
    ,running(true)
    ,processor(pvd::Thread::Config(this, &Collector::run)
               .name("Agg Processor")
               .prio(prio))
{
    REFTRACE_INCREMENT(num_instances);

    pvs.resize(names.size());

    work_queue.reset(new WorkQueuePVA(epicsThreadPriorityMedium+1));

    for(size_t i=0, N=names.size(); i<N; i++) {
        pvs[i].sub = std::make_shared<SubscriptionPVA>(cliprovider, *work_queue, names[i],
                    pvd::createRequest(std::string("field(value)")), *this, i);
    }

    processor.start();
}

Collector::~Collector()
{
    REFTRACE_DECREMENT(num_instances);
    close();
}

void Collector::close()
{
    for(size_t i=0, N=pvs.size(); i<N; i++) {
        pvs[i].sub->close();
    }

    {
        Guard G(mutex);
        running = false;
    }
    wakeup.signal();
    processor.exitWait();

    work_queue.reset();
}

void Collector::notEmpty(Subscribable *sub)
{
    bool wakeme;
    {
        Guard G(mutex);
        pvs[sub->get_idx()].ready = true;
        wakeme = waiting;
    }
    if(collectorDebug>2)
        errlogPrintf("Collector: %s notEmpty %s\n", sub->get_pvname().c_str(), wakeme? "wakeup":"");
    if(wakeme)
        wakeup.signal();
}

void Collector::run()
{
    Guard G(mutex);

    epicsTimeGetCurrent(&now);

    while(running) {
        waiting = false; // set if input queues emptied

        if(collectorDebug>2) {
            char buf[30];
            epicsTimeToStrftime(buf, sizeof(buf), "%H:%M:%S.%f", &now);
            errlogPrintf("Collector: processor wakeup %s\n", buf);
        }

        now_key = now.secPastEpoch;
        now_key <<= 32;
        now_key |= now.nsec;

        bool nothing = false; // true if all queues empty
        while(!nothing) {
            nothing = true;

            for(auto& pv : pvs) {
                if((!pv.ready) || !pv.sub)
                    continue;

                nothing = false; // we will do something
                auto val = pv.sub->pop();

                try {
                    // detect invalid PV
                    auto val_length = val->getSubFieldT<pvd::PVUIntArray>("value.secondsPastEpoch")->
                        getLength();
                    if(!val_length) {
                        pv.ready = false;
                        continue;
                    }
                }
                catch(std::exception& e) {
                    errlogPrintf("Collector: could not get value from channel %s: %s\n",
                            pv.sub->get_pvname().c_str(), e.what());
                }

                // just print the value for now
                if(collectorDebug>2) {
                    std::ostringstream oss;
                    pvd::PVStructure::Formatter fmt(val->stream()
                            .format(pvd::PVStructure::Formatter::NT));
                    oss << fmt;
                    errlogPrintf("Collector: %s channel value:\n%s\n", pv.sub->get_pvname().c_str(),
                            oss.str().c_str());
                }
            }
        }

        waiting = nothing; // wait if we emptied all queues

        bool willwait = waiting;
        {
            UnGuard U(G);

            if(willwait)
                wakeup.wait();

            epicsTimeGetCurrent(&now);
        }
    }
}

extern "C" {
epicsExportAddress(int, collectorDebug);
}
