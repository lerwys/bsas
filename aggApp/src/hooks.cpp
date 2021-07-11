#include <memory>
#include <string>
#include <map>
#include <stdexcept>
#include <sstream>

#include <initHooks.h>
#include <iocsh.h>
#include <epicsExit.h>
#include <epicsStdio.h>

#include <pv/pvAccess.h>
#include <pva/client.h>
#include <pv/caProvider.h>
#include <pv/reftrack.h>
#include <pva/sharedstate.h>

#include "iocshelper.h"
#include "controller.h"
#include "collector.h"
#include "collect_pva.h"
#include "aggregator_pva.h"

#include <epicsExport.h>

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

namespace {

// static after iocInit()
std::map<std::string, std::shared_ptr<Controller> > controllers;

// server/client providers
pvas::StaticProvider::shared_pointer provider;
std::shared_ptr<pvac::ClientProvider> cliprovider;

bool locked;

void aggExit(void *)
{
    // enforce shutdown order
    // PVA server may still be running at this point
    provider->close(true); // disconnect any PVA clients
    controllers.clear(); // joins workers, cancels PVA subscriptions
    provider.reset(); // server may still be holding a ref., but drop this one anyway
    cliprovider.reset(); // drop any client connections
}

void aggHook(initHookState state)
{
    if(state==initHookAtBeginning)
        locked = true;
    if(state!=initHookAfterIocRunning)
        return;

    epicsAtExit(aggExit, 0);

    try {
        for(auto& it : controllers) {
            it.second = std::shared_ptr<Controller>(new Controller(it.first, *provider,
                    *cliprovider));
        }
    }
    catch(std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
    }
}

void aggTableAdd(const char *prefix)
{
    if(locked) {
        fprintf(stderr, "Not allowed after iocInit()\n");
    } else {
        try {
            controllers.insert(std::make_pair(prefix, nullptr));
        }
        catch(std::exception& e) {
            fprintf(stderr, "Error: %s\n", e.what());
        }
    }
}

void aggRegistrar()
{
    epics::registerRefCounter("Controller", &Controller::num_instances);
    epics::registerRefCounter("Collector", &Collector::num_instances);
    epics::registerRefCounter("WorkQueuePVA", &WorkQueuePVA::num_instances);
    epics::registerRefCounter("SubscriptionPVA", &SubscriptionPVA::num_instances);
    epics::registerRefCounter("AggregatorPVA", &AggregatorPVA::num_instances);
    epics::registerRefCounter("TColumn", &TColumn::num_instances);

    // register our (empty) provider before the PVA server is started
    provider.reset(new pvas::StaticProvider("agg"));
    pva::ChannelProviderRegistry::servers()->addSingleton(provider->provider());

    // add "ca" provider to registry. pva is already includede
    pva::ca::CAClientFactory::start();

    // start client provider before the PVA server is started
    cliprovider.reset(new pvac::ClientProvider("pva"));

    epics::iocshRegister<const char*,
                         &aggTableAdd>(
                "aggTableAdd",
                "prefix");

    initHookRegister(&aggHook);
}

} // namespace

extern "C" {
epicsExportRegistrar(aggRegistrar);
}
