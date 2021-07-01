#ifndef SUBSCRIBABLE_H
#define SUBSCRIBABLE_H

#include <string>

// Accessor function macros
#define SET_FUNCTION_NAME(prop, type) \
    set_##prop

#define GET_FUNCTION_NAME(prop, type) \
    get_##prop

#define DEFINE_VIRTUAL_PROP_METHODS(prop, type) \
    virtual void SET_FUNCTION_NAME(prop, type) (type prop) = 0; \
    virtual type GET_FUNCTION_NAME(prop, type) () = 0; \

#define DEFINE_VIRTUAL_PROP_METHODS2(prop, type, actual, previous) \
    DEFINE_VIRTUAL_PROP_METHODS(actual##prop, type) \
    DEFINE_VIRTUAL_PROP_METHODS(previous##prop, type)

#define DEFINE_IMPL_PROP_METHODS(prop, type) \
    public: \
        virtual void SET_FUNCTION_NAME(prop, type) (type prop) OVERRIDE FINAL { this->prop = prop; } \
        virtual type GET_FUNCTION_NAME(prop, type) () OVERRIDE FINAL { return prop; } \
    private: \
        type prop;

#define DEFINE_IMPL_PROP_METHODS2(prop, type, actual, previous) \
    DEFINE_IMPL_PROP_METHODS(actual##prop, type) \
    DEFINE_IMPL_PROP_METHODS(previous##prop, type)

struct Subscribable {
    virtual ~Subscribable() {}
    // get pvname
    virtual const std::string get_pvname() = 0;
    // clear subscription
    virtual void close() = 0;
    // get PV index
    virtual size_t get_idx() = 0;
    // dequeue one update
    virtual epics::pvData::PVStructurePtr pop() = 0;
    // flush values
    virtual void clear(size_t remain) = 0;

    // properties
    DEFINE_VIRTUAL_PROP_METHODS(connected, size_t)
    DEFINE_VIRTUAL_PROP_METHODS2(Disconnects, size_t, n, l)
    DEFINE_VIRTUAL_PROP_METHODS2(Errors, size_t, n, l)
    DEFINE_VIRTUAL_PROP_METHODS2(Updates, size_t, n, l)
    DEFINE_VIRTUAL_PROP_METHODS2(UpdateBytes, size_t, n, l)
    DEFINE_VIRTUAL_PROP_METHODS2(Overflows, size_t, n, l)
    // current buffer limit
    DEFINE_VIRTUAL_PROP_METHODS(limit, size_t)
};

#endif // SUBSCRIBABLE_H
