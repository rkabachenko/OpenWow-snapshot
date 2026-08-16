
#pragma once

namespace openwow::core {

class SLockDebug {
public:
    static SLockDebug& Instance();

    void DumpAllCritSects();

    void DumpAllRWLocks();

    void DumpCritSect(const void* cs_ptr);

    void DumpRWLock(const void* rw_ptr);

private:
    SLockDebug() = default;
};

}
