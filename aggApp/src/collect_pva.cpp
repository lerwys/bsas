#include <iostream>
#include <sstream>

#include <errlog.h>
#include <epicsTypes.h>
#include <epicsExport.h>

#include <pv/standardField.h>
#include <pv/createRequest.h>
#include <pv/sharedVector.h>

#include "collect_pva.h"
#include "collector.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

namespace {

pvd::StructureConstPtr type_table(pvd::getFieldCreate()->createFieldBuilder()
                                   ->setId("epics:nt/NTTable:1.0")
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
    ,retype(true)
    ,ptr(nullptr)
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
        // immediate cancelation
        mon.cancel();
        // remove connect callback
        chan.removeConnectListener(this);
        // disconnect channel
        provider.disconnect(pvname);
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
            auto val = pvd::getPVDataCreate()->createPVStructure(type_table);

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
                if(collectorPvaDebug>0)
                    errlogPrintf("SubscriptionPVA: %s Error %s\n", mon.name().c_str(), evt.message.c_str());
                break;
            case pvac::MonitorEvent::Cancel:
                if(collectorPvaDebug>0)
                    errlogPrintf("SubscriptionPVA: %s <Cancel>\n", mon.name().c_str());
                break;
            case pvac::MonitorEvent::Disconnect:
                if(collectorPvaDebug>0)
                    errlogPrintf("SubscriptionPVA: %s <Disconnect>\n", mon.name().c_str());
                break;
            case pvac::MonitorEvent::Data:
                {
                    unsigned n;
                    for(n=0; n<2 && mon.poll(); n++) {
                        try {
                            // detect type changes
                            retype = false;
                            if(ptr != mon.root.get()) {
                                retype = true;
                                ptr = mon.root.get();

                                if(collectorPvaDebug>1) {
                                    errlogPrintf("SubscriptionPVA: %s retype in progress\n",
                                            pvname.c_str());
                                }
                            }

                            if(collectorPvaDebug>1) {
                                std::ostringstream val;
                                auto& pv_type = mon.root->getStructure();
                                errlogPrintf("SubscriptionPVA: %s, ID: %s\n", pvname.c_str(),
                                        pv_type->getID().empty()? "(empty)":pv_type->getID().c_str());

                                pvd::PVStructure::Formatter fmt(mon.root->stream()
                                        .format(pvd::PVStructure::Formatter::NT));
                                val << fmt;
                                errlogPrintf("SubscriptionPVA: %s channel value:\n%s\n", pvname.c_str(),val.str().c_str());
                            }

                            if(retype) {
                                auto builder = pvd::getFieldCreate()->createFieldBuilder()
                                                                    ->setId("epics:nt/NTTable:1.0")
                                                                    ->addArray("labels", pvd::pvString)
                                                                    ->addNestedStructure("value");

                                auto val = mon.root->getSubFieldT<pvd::PVStructure>("value");
                                for(auto& it : val->getPVFields()) {
                                    auto field = it->getField();
                                    auto fname = it->getFieldName();

                                    builder = builder->add(fname, field);
                                }

                                type = builder->endNested()
                                                   ->add("alarm", pvd::getStandardField()->alarm())
                                                   ->add("timeStamp", pvd::getStandardField()->timeStamp())
                                                   ->createStructure();
                            }

                            // copy values
                            auto root = pvd::getPVDataCreate()->createPVStructure(type);

                            auto flabels = root->getSubFieldT<pvd::PVStringArray>("labels");
                            flabels->replace(mon.root->getSubFieldT<pvd::PVStringArray>("labels")->view());

                            root->getSubFieldT<pvd::PVStructure>("value")->copy(
                                    *mon.root->getSubFieldT<pvd::PVStructure>("value"));
                            root->getSubFieldT<pvd::PVStructure>("alarm")->copy(
                                    *mon.root->getSubFieldT<pvd::PVStructure>("alarm"));
                            root->getSubFieldT<pvd::PVStructure>("timeStamp")->copy(
                                    *mon.root->getSubFieldT<pvd::PVStructure>("timeStamp"));

                            bool notify;
                            {
                                Guard G(mutex);
                                notify = values.empty();

                                _push(root);
                            }

                            if(notify) {
                                collector.notEmpty(this);
                            }
                        }
                        catch(std::exception& e) {
                            errlogPrintf("SubscriptionPVA: could not copy value from channel %s: %s\n",
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
    auto ret = pvd::getPVDataCreate()->createPVStructure(type_table);
    {
        Guard G(mutex);
        if(!values.empty()) {
            ret = values.front();
            values.pop_front();
        }
    }
    return ret;
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
