#ifndef PVATESTSERVER_H
#define PVATESTSERVER_H

#include <vector>

#include <pv/pvAccess.h>
#include <pva/server.h>
#include <pva/sharedstate.h>

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

class PVATestServer {
public:
    POINTER_DEFINITIONS(PVATestServer);

    PVATestServer(pvas::StaticProvider& provider, pvd::shared_vector<const std::string> const& pv_names);

private:
    pvas::StaticProvider& provider;
    typedef pvd::shared_vector<const std::string> const pv_names_t;
    pv_names_t pv_names;
    typedef std::vector<std::pair<std::string, pvas::SharedPV::shared_pointer>> pvs_t;
    pvs_t pvs;
};

#endif // PVATESTSERVER_H
