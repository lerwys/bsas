#ifndef AGGREGATOR_PVA_H
#define AGGREGATOR_PVA_H

#include <string>

#include <epicsTime.h>
#include <epicsMutex.h>
#include <epicsGuard.h>

#include <pv/noDefaultMethods.h>
#include <pv/sharedVector.h>
#include <pv/reftrack.h>
#include <pv/thread.h>
#include <pva/client.h>
#include <pva/sharedstate.h>

#include "collector.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

typedef epicsGuard<epicsMutex> Guard;
typedef epicsGuardRelease<epicsMutex> UnGuard;

class TColumn {
public:
    static size_t num_instances;

    TColumn(std::string dst_fname, size_t tidx, std::string src_fname);
    ~TColumn();

    void copy(pvd::PVStructurePtr root,
            const std::vector<std::pair<epicsUInt64, std::vector<pvd::PVStructurePtr>>>& events,
            pvd::BitSet& changed);

private:
    std::string dst_fname;
    size_t tidx;
    std::string src_fname;
};

class AggregatorPVA : public Aggregator {
public:
    static size_t num_instances;

    AggregatorPVA(const std::string& name,
            pvas::StaticProvider &provider,
            Collector &collector);
    ~AggregatorPVA();

    void reset(const std::vector<std::string>& names) override final;
    void aggregate(const std::vector<
            std::pair<epicsUInt64, std::vector<pvd::PVStructurePtr>>>& events) override final;

private:

    epicsEvent stateRun;

    const std::string& name;
    pvas::StaticProvider &provider;
    Collector &collector;

    // Aggregated table
    std::vector<std::string> pv_names;
    pvd::PVStructurePtr root;
    pvd::BitSet changed;
    const pvas::SharedPV::shared_pointer pv;

    // Field copier
    std::vector<TColumn> tcolumns;

    enum state_t {
        NeedRetype,
        RetypeInProg,
        Run,
    } state;

    epicsMutex mutex;

    void close();
};

#endif // AGGREGATOR_PVA_H
