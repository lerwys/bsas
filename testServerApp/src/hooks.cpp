#include <string>

#include <initHooks.h>
#include <iocsh.h>
#include <epicsExit.h>
#include <epicsStdio.h>

#include <pv/pvAccess.h>

#include "pvaTestServer.h"

#include <epicsExport.h>

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

namespace {

// PVA server
std::tr1::shared_ptr<PVATestServer> pv_server;
// list of PVs to be created
pvd::shared_vector<std::string> pv_names;
// our channel provider
pvas::StaticProvider::shared_pointer provider;
// use to prohibit adding new PVs after iocInit()
bool locked;
// update period to PVATestServer argument
double update_period = 1.0;

void testServerExit(void *)
{
    // enforce shutdown order
    // PVA server may still be running at this point
    provider->close(true); // disconnect any PVA clients
    provider.reset(); // server may still be holding a ref., but drop this one anyway
}

void testServerHook(initHookState state)
{
    if(state==initHookAtBeginning) {
        locked = true;
    }

    if(state!=initHookAfterIocRunning) {
        return;
    }

    epicsAtExit(testServerExit, 0);

    // create PVs
    pv_server.reset(new PVATestServer(*provider, pvd::freeze(pv_names),
                update_period));
}

} // namespace

/* testServerPVScalarAdd */
extern "C"
void testServerPVScalarAdd(const char *name)
{
    if(locked) {
        printf("Not allowed after iocInit()\n");
    } else {
        pv_names.push_back(std::string(name));
    }
}

static const iocshArg testServerPVScalarAddArg0 = {
    "name",
    iocshArgString
};
static const iocshArg * const testServerPVScalarAddArgs[] = {
    &testServerPVScalarAddArg0
};
static const iocshFuncDef testServerPVScalarAddFuncDef = {
    "testServerPVScalarAdd",
    1,
    testServerPVScalarAddArgs
};
static void testServerPVScalarAddCallFunc(const iocshArgBuf *args)
{
    testServerPVScalarAdd(args[0].sval);
}

/* testServerPVUpdatePeriod */
extern "C"
void testServerPVUpdatePeriod(double period)
{
    update_period = period;
    // if server already initialized we need to
    // call set_update_period(), otherwise it will
    // be constructed with this value
    if(pv_server) {
        pv_server->set_update_period(update_period);
    }
}

static const iocshArg testServerPVUpdatePeriodArg0 = {
    "period",
    iocshArgDouble
};
static const iocshArg * const testServerPVUpdatePeriodArgs[] = {
    &testServerPVUpdatePeriodArg0
};
static const iocshFuncDef testServerPVUpdatePeriodFuncDef = {
    "testServerPVUpdatePeriod",
    1,
    testServerPVUpdatePeriodArgs
};
static void testServerPVUpdatePeriodCallFunc(const iocshArgBuf *args)
{
    testServerPVUpdatePeriod(args[0].dval);
}

static void testServerRegistrar()
{
    //refcounters to help detecing slow ref./resources leaks
    epics::registerRefCounter("PVATestServer", &PVATestServer::num_instances);

    // create empty provider before the PVA server is started
    provider.reset(new pvas::StaticProvider("testServer"));
    // register pre-created server provider
    pva::ChannelProviderRegistry::servers()->addSingleton(provider->provider());

    // register PV add
    iocshRegister(&testServerPVScalarAddFuncDef, testServerPVScalarAddCallFunc);
    // register PV update period
    iocshRegister(&testServerPVUpdatePeriodFuncDef, testServerPVUpdatePeriodCallFunc);
    // initial setup
    initHookRegister(&testServerHook);
}

extern "C" {
epicsExportRegistrar(testServerRegistrar);
}
