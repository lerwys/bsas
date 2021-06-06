#ifndef PVATESTSERVER_H
#define PVATESTSERVER_H

#include <vector>

#include <epicsMutex.h>
#include <epicsGuard.h>
#include <epicsEvent.h>
#include <epicsTime.h>

#include <pv/thread.h>
#include <pv/pvAccess.h>
#include <pv/reftrack.h>
#include <pva/server.h>
#include <pva/sharedstate.h>

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

typedef epicsGuard<epicsMutex> Guard;
typedef epicsGuardRelease<epicsMutex> UnGuard;

class PVATestServer {
public:
    POINTER_DEFINITIONS(PVATestServer);

    PVATestServer(pvas::StaticProvider& provider, pvd::shared_vector<const std::string> const& pv_names,
            double update_period = 1.0);
    ~PVATestServer();

    void set_update_period(double period);
    double get_update_period();

    // for reftrack
    static size_t num_instances;

private:
    pvas::StaticProvider& provider;
    typedef pvd::shared_vector<const std::string> const pv_names_t;
    pv_names_t pv_names;
    typedef std::vector<std::pair<std::string, pvas::SharedPV::shared_pointer>> pvs_t;
    pvs_t pvs;

    // thread handle
    pvd::Thread handler;
    void handle();

    // is thread running?
    bool running;
    // mutex for thread
    mutable epicsMutex mutex;
    // event for thread throttling
    epicsEvent wakeup;
    // PV update period
    double update_period;
};

#endif // PVATESTSERVER_H
