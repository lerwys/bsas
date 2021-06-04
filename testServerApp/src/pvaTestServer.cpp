#include <pv/standardField.h>

#include "pvaTestServer.h"

namespace pvd = epics::pvData;

namespace {

pvd::StructureConstPtr pv_type_scalar(
    pvd::getFieldCreate()->createFieldBuilder()
        ->setId("epics:nt/NTScalar:1.0")
        ->addArray("value", pvd::pvString)
        ->add("alarm", pvd::getStandardField()->alarm())
        ->add("timeStamp", pvd::getStandardField()->timeStamp())
        ->createStructure());

} // namespace

PVATestServer::PVATestServer(pvas::StaticProvider& provider,
        pvd::shared_vector<const std::string> const& pv_names)
    : provider(provider)
    , pv_names(pv_names)
{
    for(pv_names_t::iterator it = pv_names.begin(); it != pv_names.end(); ++it) {
        pvs.push_back(std::make_pair(*it, pvas::SharedPV::buildReadOnly()));
    }

    for(pvs_t::iterator it = pvs.begin(); it != pvs.end(); ++it) {
        it->second->open(pv_type_scalar);
        provider.add(it->first, it->second);
    }
}
