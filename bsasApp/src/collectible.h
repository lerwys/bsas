#ifndef COLLECTIBLE_H
#define COLLECTIBLE_H

#include <string>

#include <epicsMutex.h>

struct RValue {
    virtual ~RValue() {}

    virtual epicsTimeStamp get_ts() = 0; // timestamp in epics epoch
    virtual void set_ts(epicsTimeStamp ts) = 0;
    virtual epicsUInt16 get_sevr() = 0;  // [0-3] or 4 (Disconnect)
    virtual void set_sevr(epicsUInt16 sevr) = 0;
    virtual epicsUInt16 get_stat() = 0;  // status code a la Base alarm.h
    virtual void set_stat(epicsUInt16 stat) = 0;
    virtual epicsUInt32 get_count() = 0;
    virtual void set_count(epicsUInt32 count) = 0;
    virtual const epics::pvData::shared_vector<const void>& get_buffer() = 0;
    virtual void set_buffer(const epics::pvData::shared_vector<const void>& buffer) = 0;

    virtual bool valid() const = 0;
    virtual void swap(RValue& o) = 0;
    virtual void reset() = 0;
};

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
    // get column
    virtual const size_t get_column() = 0;
    // get pvname
    virtual const std::string get_pvname() = 0;
    // clear subscription
    virtual void close() = 0;
    // dequeue one update
    virtual std::tr1::shared_ptr<RValue> pop() = 0;
    // queue one update. ONLY FOR TESTS
    virtual void push(const RValue& v) = 0;
    // flush values
    virtual void clear(size_t remain) = 0;
    // get values
    virtual const std::deque<std::tr1::shared_ptr<RValue>>& get_values() = 0;

    mutable epicsMutex mutex;

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

#endif // COLLECTIBLE_H
