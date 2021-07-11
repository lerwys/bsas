#include <iostream>
#include <sstream>

#include <errlog.h>
#include <epicsTypes.h>
#include <epicsExport.h>

#include <pv/standardField.h>
#include <pv/createRequest.h>
#include <pv/sharedVector.h>

#include "aggregator_pva.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

size_t TColumn::num_instances;

TColumn::TColumn(std::string dst_fname,
        size_t tidx,
        std::string src_fname)
    :dst_fname(dst_fname)
    ,tidx(tidx)
    ,src_fname(src_fname)
{
    REFTRACE_INCREMENT(num_instances);
}

TColumn::~TColumn()
{
    REFTRACE_DECREMENT(num_instances);
}

void TColumn::copy(pvd::PVStructurePtr root,
        const std::vector<std::pair<epicsUInt64, std::vector<pvd::PVStructurePtr>>>& events,
        pvd::BitSet& changed)
{
    auto dst_arr = root->getSubFieldT<pvd::PVStructure>("value")
                       ->getSubFieldT<pvd::PVScalarArray>(dst_fname);

    // copy arrays from all events to the destination array
    size_t offset = 0;
    for(auto& event : events) {
        auto src_pvs = event.second.at(tidx);
        auto src_arr = src_pvs->getSubFieldT<pvd::PVStructure>("value")
                              ->getSubFieldT<pvd::PVScalarArray>(src_fname);
        auto length = src_arr->getLength();

        pvd::copy(*src_arr, 0, 1,
                *dst_arr, offset, 1, length);
        offset += length;

        changed.set(dst_arr->getFieldOffset());
    }
}

int aggregatorPvaDebug;

size_t AggregatorPVA::num_instances;

AggregatorPVA::AggregatorPVA(const std::string& name,
        pvas::StaticProvider &provider,
        Collector &collector)
    :name(name)
    ,provider(provider)
    ,collector(collector)
    ,pv(pvas::SharedPV::buildReadOnly())
    ,state(NeedRetype)
{
    REFTRACE_INCREMENT(num_instances);
    collector.addAggregator(this); // calls our names()

    provider.add(name, pv);
    errlogPrintf("AggregatorPVA: add %s\n", name.c_str());
}

AggregatorPVA::~AggregatorPVA()
{
    REFTRACE_DECREMENT(num_instances);
    close();
}

void AggregatorPVA::close()
{
    collector.removeAggregator(this);
    provider.remove(name);
    pv->close();
}

void AggregatorPVA::reset(const std::vector<std::string>& names)
{
    std::vector<std::string> temp_names;
    for(auto& name : names) {
        temp_names.push_back(name);
    }

    {
        Guard G(mutex);
        pv_names.swap(temp_names);

        root.reset();
        changed.clear();

        state = NeedRetype;
    }

    pv->close(); // paranoia?
}

void AggregatorPVA::aggregate(const std::vector<std::pair<epicsUInt64, std::vector<pvd::PVStructurePtr>>>& events)
{
    {
        Guard G(mutex);

        if(state == NeedRetype) {
            state = RetypeInProg;
            if(aggregatorPvaDebug>0) {
                errlogPrintf("AggregatorPVA: type change\n");
            }

            tcolumns.clear();

            pvd::FieldBuilderPtr builder(pvd::getFieldCreate()->createFieldBuilder()
                                         ->setId("epics:nt/NTTable:1.0")
                                         ->addArray("labels", pvd::pvString)
                                         ->addNestedStructure("value"));

            pvd::shared_vector<std::string> labels;
            try {
                // add all fields inside "value" from all tables to "value" field +
                // pv_name + field_name to "labels" field
                // TODO: assume all PVStructurePtr have the same introspection
                //     interface
                // TODO: prepend array name of each field with the original PV name
                auto pv_struct_it(events.at(0).second.begin()),
                     pv_struct_end(events.at(0).second.end());
                auto pv_name_it(pv_names.begin()),
                     pv_name_end(pv_names.end());
                size_t tidx = 0;
                for( ;
                        pv_struct_it != pv_struct_end && pv_name_it != pv_name_end;
                        ++pv_struct_it, ++pv_name_it, ++tidx) {
                    for(auto& pv_field : (*pv_struct_it)->getSubFieldT<pvd::PVStructure>("value")->getPVFields()) {
                        // skip secondsPastEpoch/seconds fields, as those will be
                        // added later
                        auto fname = pv_field->getFieldName();
                        if(fname.find("seconds") != std::string::npos) {
                            continue;
                        }

                        auto column_name = *pv_name_it + "_" + fname;
                        builder = builder->add(column_name, pv_field->getField());
                        labels.push_back(column_name);
                        // FIXME: strings are slow, use offsets?
                        tcolumns.emplace_back(column_name, tidx, fname);
                    }
                }
            }
            catch(std::exception& e) {
                errlogPrintf("AggregatorPVA: Could not build PVStructure from PV: %s\n", e.what());
                throw;
            }

            pvd::StructureConstPtr type(builder
                                            ->addArray("secondsPastEpoch", pvd::pvUInt)
                                            ->addArray("nanoseconds", pvd::pvUInt)
                                        ->endNested() // end of .value
                                        ->add("alarm", pvd::getStandardField()->alarm())
                                        ->add("timeStamp", pvd::getStandardField()->timeStamp())
                                        ->createStructure());
            root = pvd::getPVDataCreate()->createPVStructure(type);
            changed.clear();

            {
                pvd::PVStringArrayPtr flabels(root->getSubFieldT<pvd::PVStringArray>("labels"));
                flabels->replace(pvd::freeze(labels));
                changed.set(flabels->getFieldOffset());
            }

            {
                UnGuard U(G);
                pv->close();
                pv->open(*root, changed);
            }

            state = Run;
            stateRun.signal();
        }

        while(state!=Run) {
            UnGuard U(G);
            stateRun.wait();
        }

        // TODO: update timstamp/alarm

        // copy values from all tables to aggregated one
        try {
            for(auto& tcolumn : tcolumns) {
                tcolumn.copy(root, events, changed);
            }
        }
        catch(std::exception& e) {
            errlogPrintf("AggregatorPVA: Could not copy values from PVs: %s\n", e.what());
            throw;
        }

        if(aggregatorPvaDebug>2) {
            std::ostringstream oss;
            pvd::PVStructure::Formatter fmt(root->stream()
                    .format(pvd::PVStructure::Formatter::NT));
            oss << fmt;
            errlogPrintf("AggregatorPVA: %s channel value:\n%s\n", name.c_str(),
                    oss.str().c_str());
        }

        {
            UnGuard U(G);
            pv->post(*root, changed);
        }

        changed.clear();
    }
}

extern "C" {
epicsExportAddress(int, aggregatorPvaDebug);
}
