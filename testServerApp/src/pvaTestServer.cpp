#include <limits>

#include <errlog.h>
#include <epicsTypes.h>

#include <pv/standardField.h>

#include "pvaTestServer.h"

namespace pvd = epics::pvData;

namespace {

pvd::StructureConstPtr pv_type_scalar(
    pvd::getFieldCreate()->createFieldBuilder()
        ->setId("epics:nt/NTScalar:1.0")
        ->add("value", pvd::pvUInt)
        ->add("alarm", pvd::getStandardField()->alarm())
        ->add("timeStamp", pvd::getStandardField()->timeStamp())
        ->createStructure());

} // namespace

size_t PVATestServer::num_instances;

PVATestServer::PVATestServer(pvas::StaticProvider& provider,
        pvd::shared_vector<const std::string> const& pv_names,
        double update_period)
    : provider(provider)
    , pv_names(pv_names)
    , handler(pvd::Thread::Config(this, &PVATestServer::handle)
             .prio(epicsThreadPriorityLow)
             .autostart(false)
             << "PVATestServer")
    , running(true)
{
    REFTRACE_INCREMENT(num_instances);

    for(pv_names_t::iterator it = pv_names.begin(); it != pv_names.end(); ++it) {
        pvs.push_back(std::make_pair(*it, pvas::SharedPV::buildReadOnly()));
    }

    for(pvs_t::iterator it = pvs.begin(); it != pvs.end(); ++it) {
        it->second->open(pv_type_scalar);
        provider.add(it->first, it->second);
    }

    this->update_period = update_period;

    // start thread
    handler.start();
}

PVATestServer::~PVATestServer()
{
    REFTRACE_DECREMENT(num_instances);
    // end thread graciously
    {
        Guard G(mutex);
        running = false;
    }
    wakeup.signal();
    // wait for thread
    handler.exitWait();
}

void PVATestServer::set_update_period(double period)
{
    {
        Guard G(mutex);
        update_period = period;
    }
}

double PVATestServer::get_update_period()
{
    return update_period;
}

void PVATestServer::handle()
{
    Guard G(mutex);

    bool expired = false;
    while(running) {
        if(expired) {
            {
                UnGuard U(G);

                epicsTimeStamp now;
                epicsTimeGetCurrent(&now);

                for(pvs_t::iterator it = pvs.begin(); it != pvs.end(); ++it) {

                    pvs_t::value_type::second_type& pv = it->second;
                    // get PVStructure from SharedPV
                    pvd::PVStructurePtr root(pv->build());
                    pvd::BitSet changed;
                    pv->fetch(*root, changed);

                    // update value
                    pvd::PVScalarPtr fscalar;
                    fscalar = root->getSubFieldT<pvd::PVScalar>("value");
                    epicsUInt32 value = fscalar->getAs<pvd::uint32>();

                    if(value == std::numeric_limits<epicsUInt32>::max()) {
                        value = 0;
                    }
                    else {
                        ++value;
                    }

                    fscalar->putFrom<pvd::uint32>(value);
                    changed.set(fscalar->getFieldOffset());

                    // update timestamp
                    fscalar = root->getSubFieldT<pvd::PVScalar>("timeStamp.secondsPastEpoch");
                    fscalar->putFrom<pvd::uint32>(now.secPastEpoch + POSIX_TIME_AT_EPICS_EPOCH);
                    changed.set(fscalar->getFieldOffset());
                    fscalar = root->getSubFieldT<pvd::PVScalar>("timeStamp.nanoseconds");
                    fscalar->putFrom<pvd::uint32>(now.nsec);
                    changed.set(fscalar->getFieldOffset());

                    pv->post(*root, changed);
                }
            }
        }

        {
            UnGuard U(G);
            expired = !wakeup.wait(update_period);
        }

    }
}
