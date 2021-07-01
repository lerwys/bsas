#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <pv/thread.h>
#include <pv/sharedPtr.h>
#include <pv/pvData.h>
#include <pv/pvAccess.h>

#include <pva/sharedstate.h>
#include <pva/client.h>

#include <epicsMutex.h>
#include <epicsGuard.h>

typedef epicsGuard<epicsMutex> Guard;
typedef epicsGuardRelease<epicsMutex> UnGuard;

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

class Collector;

class Controller : public epicsThreadRunable,
                   public std::enable_shared_from_this<Controller>
{
public:
    static size_t num_instances;

    Controller(const std::string& prefix, pvas::StaticProvider& provider,
            pvac::ClientProvider& cliprovider);
    ~Controller();

private:
    const std::string& prefix;

    pvas::StaticProvider& provider;
    pvac::ClientProvider& cliprovider;
    pvas::SharedPV::shared_pointer pv_signals;
    pvas::SharedPV::shared_pointer pv_status;
    pvd::PVStructurePtr root_status;

    std::unique_ptr<Collector> collector;

    pvd::shared_vector<const std::string> signals;

    pvd::Thread handler;

    bool signals_changed;
    bool running;
    double wait_period;

    epicsMutex mutex;
    epicsEvent wakeup;

    // overrides epicsThreadRunable
    virtual void run() override final;

    // for handling pv_signals put
    class SignalsHandler : public pvas::SharedPV::Handler {
    public:
        SignalsHandler(const std::shared_ptr<Controller>& controller) :controller(controller) {}

        // overrides SharedPV::Handler
        virtual void onPut(const pvas::SharedPV::shared_pointer& pv, pvas::Operation& op) override;
    private:
        const std::weak_ptr<Controller> controller;
    };
};

#endif // CONTROLLER_H
