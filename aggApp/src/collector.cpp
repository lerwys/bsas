#include <errlog.h>
#include <pv/reftrack.h>
#include <pv/pvAccess.h>
#include <pv/pvData.h>

#include "collector.h"

#include <epicsExport.h>

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

int collectorDebug;
int collectorNumWorkQueue;

size_t Collector::num_instances;

Collector::Collector(pvac::ClientProvider& cliprovider,
                       const pvd::shared_vector<const std::string>& names,
                       unsigned int prio,
                       size_t num_work_queue)
    :cliprovider(cliprovider)
    ,waiting(false)
    ,running(true)
    ,processor(pvd::Thread::Config(this, &Collector::run)
               .name("Agg Processor")
               .prio(prio))
{
    REFTRACE_INCREMENT(num_instances);

    pvs.resize(names.size());

    for(size_t i = 0; i < num_work_queue; i++) {
        work_queue.push_back(std::unique_ptr<WorkQueuePVA>(new WorkQueuePVA(epicsThreadPriorityMedium+1)));
    }

    for(size_t i = 0, n_queue = work_queue.size(); i < names.size(); i++) {
        pvs[i].sub = std::make_shared<SubscriptionPVA>(cliprovider, *work_queue[i % n_queue], names[i],
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
    for(auto& pv : pvs) {
        pv.sub->close();
    }

    {
        Guard G(mutex);
        running = false;
    }
    wakeup.signal();
    processor.exitWait();

    work_queue.clear();
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

void Collector::addAggregator(Aggregator* agg)
{
    std::vector<std::string> names;
    {
        Guard G(mutex);
        aggregators.insert(agg);
        aggregators_changed = true;

        if(collectorDebug>2)
            errlogPrintf("Collector: aggregator added\n");

        // all PV names
        names.reserve(pvs.size());
        for(auto& pv : pvs) {
            names.push_back(pv.sub->get_pvname());
        }
    }
    agg->reset(names);
}

void Collector::removeAggregator(Aggregator* agg)
{
    Guard G(mutex);
    aggregators.erase(agg);
    aggregators_changed = true;

    if(collectorDebug>2)
        errlogPrintf("Collector: aggregator removed\n");
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

                epicsUInt64 key = 0;
                try {
                    // detect invalid PV
                    auto sec = val->getSubFieldT<pvd::PVLong>("timeStamp.secondsPastEpoch")->get();
                    if(sec == 0) {
                        pv.ready = false;
                        continue;
                    }
                    pv.ready = true;

                    auto ns = val->getSubFieldT<pvd::PVInt>("timeStamp.nanoseconds")->get();
                    key = sec;
                    key <<= 32;
                    key |= ns;

                    auto sevr = val->getSubFieldT<pvd::PVInt>("alarm.severity")->get();
                    pv.connected = sevr<=3;

                    if(collectorDebug>3) {
                        errlogPrintf("Collector: %s event:%llx sevr %u\n",
                                pv.sub->get_pvname().c_str(), key, sevr);
                    }
                }
                catch(std::exception& e) {
                    errlogPrintf("Collector: could not get value from channel %s: %s\n",
                            pv.sub->get_pvname().c_str(), e.what());
                    continue;
                }

                if(collectorDebug>2) {
                    std::ostringstream oss;
                    pvd::PVStructure::Formatter fmt(val->stream()
                            .format(pvd::PVStructure::Formatter::NT));
                    oss << fmt;
                    errlogPrintf("Collector: %s channel value:\n%s\n", pv.sub->get_pvname().c_str(),
                            oss.str().c_str());
                }

                // TODO:
                // * discard old data
                // * align timestamps
                // * decide what to do with tables not complying with the
                //     expected format (16 columns, 1k rows)

                // add data to table slice
                if(key > oldest_key) {
                    // implicitly allocs new slice, if key not present
                    auto& slice = events[key];
                    slice.resize(pvs.size());

                    // existing value, ignore duplicates
                    if(slice[pv.sub->get_idx()]) {
                        if(collectorDebug>=0) {
                            errlogPrintf("Collector: %s ignore duplicate key %llx\n", pv.sub->get_pvname().c_str(), key);
                        }
                    }
                    else {
                        slice[pv.sub->get_idx()].swap(val);
                    }
                }
                else if(collectorDebug>0) {
                    errlogPrintf("Collector: %s ignore leftovers of %llx\n", pv.sub->get_pvname().c_str(), key);
                }
            }
        }

        // wait if we emptied all queues
        waiting = nothing;

        // check if the slices are completed (i.e., have all PVs valid)
        auto first_partial(events.rend()); // first element _not_ to flush.
        try {
            auto event_it = events.rbegin();
            for(; event_it != events.rend(); ++event_it) {

                //check if all PVs are present on the slice
                bool complete = false;
                auto slice_it(event_it->second.begin()),
                      slice_end(event_it->second.end());
                auto pv_it(pvs.begin()),
                      pv_end(pvs.end());
                for( ;
                        slice_it != slice_end && pv_it != pv_end;
                        ++slice_it, ++pv_it) {
                    complete = false;
                    // slice can have null elements
                    bool pv_ready = false;
                    if(*slice_it) {
                        // pv_it->ready will likely be false due to a
                        // last loop over the connected PVs, so we need
                        // to get the actual valid from the slice
                        auto fsec = (*slice_it)->getSubField<pvd::PVLong>("timeStamp.secondsPastEpoch");
                        if(fsec) {
                            pv_ready = fsec->get() > 0;
                        }

                        complete = !pv_it->connected || pv_ready;
                    }

                    if(!complete) {
                        errlogPrintf("Collector: slice %llx found incomplete %s, %sconnected, %s\n",
                                event_it->first, pv_it->sub->get_pvname().c_str(),
                                pv_it->connected?"":"dis",
                                pv_ready?"valid":"invalid");
                        break;
                    }
                }

                // only add complete slices
                if(!complete) {
                    // reverse iterator is always one element ahead
                    first_partial = event_it;
                    break;
                }
            }
        }
        catch(std::exception& e){
            errlogPrintf("Collector: could not fill completed_events: %s\n",
                    e.what());
            first_partial = events.rbegin();
        }

        completed_events.clear();

        // move valid/completed events to completed_events vector
        for(auto event_it = events.rbegin(); event_it != first_partial; ) {
            // get a new iterator, as events.erase will invalidate iterator
            auto curr = event_it++;

            if(collectorDebug>4) {
                errlogPrintf("Collector: complete key %llx\n", curr->first);
            }
            oldest_key = curr->first;

            completed_events.push_back(*curr);
            // reverse iterator is always one more position than forward
            // iterator.
            events.erase(std::next(curr).base());
        }

        if(aggregators_changed) {
            // copy for use while unlocked
            aggregators_shadow = aggregators;
            aggregators_changed = false;
        }

        bool willwait = waiting;
        {
            UnGuard U(G);

            if(!completed_events.empty()) {
                for(auto& agg : aggregators_shadow) {
                    agg->aggregate(completed_events);
                }
            }

            if(willwait)
                wakeup.wait();

            epicsTimeGetCurrent(&now);
        }
    }
}

extern "C" {
epicsExportAddress(int, collectorDebug);
epicsExportAddress(int, collectorNumWorkQueue);
}
