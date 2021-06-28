#include <epicsStdio.h>

#include <pv/reftrack.h>
#include <pv/standardField.h>

#include "controller.h"

namespace pvd = epics::pvData;

namespace {

pvd::StructureConstPtr type_signals(pvd::getFieldCreate()->createFieldBuilder()
                                    ->setId("epics:nt/NTScalar:1.0")
                                    ->addArray("value", pvd::pvString)
                                    ->add("alarm", pvd::getStandardField()->alarm())
                                    ->add("timeStamp", pvd::getStandardField()->timeStamp())
                                    ->createStructure());

pvd::StructureConstPtr type_status(pvd::getFieldCreate()->createFieldBuilder()
                                   ->setId("epics:nt/NTTable:1.0")
                                   ->addArray("labels", pvd::pvString)
                                   ->addNestedStructure("value")
                                       ->addArray("PV", pvd::pvString)
                                       ->addArray("connected", pvd::pvBoolean)
                                       ->addArray("nEvent", pvd::pvULong)
                                       ->addArray("nBytes", pvd::pvULong)
                                       ->addArray("nDiscon", pvd::pvULong)
                                       ->addArray("nError", pvd::pvULong)
                                       ->addArray("nOFlow", pvd::pvULong)
                                   ->endNested()
                                   ->add("alarm", pvd::getStandardField()->alarm())
                                   ->add("timeStamp", pvd::getStandardField()->timeStamp())
                                   ->createStructure());

} // namespace

size_t Controller::num_instances;

Controller::Controller(const std::string& prefix, pvas::StaticProvider& provider,
        pvac::ClientProvider& cliprovider)
    :prefix(prefix)
    ,provider(provider)
    ,cliprovider(cliprovider)
    ,pv_signals(pvas::SharedPV::buildReadOnly())
    ,pv_status(pvas::SharedPV::buildReadOnly())
    ,handler(pvd::Thread::Config(this, &Controller::run)
             .prio(epicsThreadPriorityLow)
             .autostart(false)
             << "Agg " << prefix)
    ,signals_changed(true)
    ,running(true)
    ,wait_period(1.0)
{
    REFTRACE_INCREMENT(num_instances);
    pv_signals->open(type_signals);

    root_status = pvd::getPVDataCreate()->createPVStructure(type_status);
    pvd::BitSet changed;
    {
        pvd::shared_vector<std::string> labels;
        labels.push_back("PV");
        labels.push_back("connected");
        labels.push_back("#Event");
        labels.push_back("#Bytes");
        labels.push_back("#Discon");
        labels.push_back("#Error");
        labels.push_back("#OFlow");

        pvd::PVStringArrayPtr flabel(root_status->getSubFieldT<pvd::PVStringArray>("labels"));
        flabel->replace(pvd::freeze(labels));
        changed.set(flabel->getFieldOffset());
    }
    pv_status->open(*root_status, changed);

    provider.add(prefix+"SIG", pv_signals);
    provider.add(prefix+"STS", pv_status);

    handler.start();
}

Controller::~Controller()
{
    REFTRACE_DECREMENT(num_instances);
    {
        Guard G(mutex);
        running = false;
    }
    wakeup.signal();
    handler.exitWait();
}

void Controller::run()
{
    Guard G(mutex);

    bool expire = false;

    while(running) {
        bool changing = signals_changed;
        signals_changed = false;

        if(changing) {
        }

        if(expire || changing) {
            // update status table
            {
                UnGuard U(G);
                printf("Test update!\n");
            }
        }

        {
            UnGuard U(G);
            expire = !wakeup.wait(wait_period);
        }
    }
}

void Controller::SignalsHandler::onPut(const pvas::SharedPV::shared_pointer& pv, pvas::Operation& op)
{
    pvd::PVStringArray::const_shared_pointer value(op.value().getSubFieldT<pvd::PVStringArray>("value"));

    // ignore attempt to put something other than .value
    if(!op.changed().get(value->getFieldOffset()))
        return;

    auto self = controller.lock();
    if(self) {
        {
            Guard G(self->mutex);
            self->signals = value->view();
            self->signals_changed = true;
        }
        self->wakeup.signal();
    }

    pv->post(op.value(), op.changed());
    op.complete();
}
