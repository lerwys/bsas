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
    pv_server.reset(new PVATestServer(*provider, pvd::freeze(pv_names)));
}

} // namespace

extern "C"
void testServerPVScalarAdd(const char *name)
{
    if(locked) {
        printf("Not allowed after iocInit()\n");
    } else {
        pv_names.push_back(std::string(name));
    }
}

/* testServerPVScalarAdd */
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

static void testServerRegistrar()
{
    // create empty provider before the PVA server is started
    provider.reset(new pvas::StaticProvider("testServer"));
    // register pre-created server provider
    pva::ChannelProviderRegistry::servers()->addSingleton(provider->provider());

    // register PV add
    iocshRegister(&testServerPVScalarAddFuncDef, testServerPVScalarAddCallFunc);
    // initial setup
    initHookRegister(&testServerHook);
}

extern "C" {
epicsExportRegistrar(testServerRegistrar);
}
