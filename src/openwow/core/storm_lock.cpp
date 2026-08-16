
#include "storm_lock.h"

namespace openwow::core {

SLockDebug& SLockDebug::Instance() {
    static SLockDebug inst;
    return inst;
}

void SLockDebug::DumpAllCritSects() {
}

void SLockDebug::DumpAllRWLocks() {
}

void SLockDebug::DumpCritSect(const void* cs_ptr) {
    (void)cs_ptr;
}

void SLockDebug::DumpRWLock(const void* rw_ptr) {
    (void)rw_ptr;
}

}
