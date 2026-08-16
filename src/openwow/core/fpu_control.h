
#pragma once

#if defined(_WIN32) || defined(_WIN64)
#  define OPENWOW_FPU_WINDOWS 1
#elif defined(__linux__)
#  define OPENWOW_FPU_LINUX 1
#elif defined(__APPLE__)
#  define OPENWOW_FPU_APPLE 1
#endif

#if defined(OPENWOW_FPU_WINDOWS)
#  include <float.h>
#else
#  include <cfenv>
#  if defined(OPENWOW_FPU_LINUX) && !defined(__aarch64__) && !defined(__arm__)

#    include <fenv.h>
#  endif
#endif

namespace openwow::core {

inline bool InitFPU() noexcept {
#if defined(OPENWOW_FPU_WINDOWS)

    _clearfp();

    unsigned int desired = _EM_INVALID | _EM_DENORMAL | _EM_ZERODIVIDE
                         | _EM_OVERFLOW | _EM_UNDERFLOW | _EM_INEXACT
                         | _RC_NEAR;
#  if defined(_M_IX86)

    desired |= _PC_24;
#  endif
    _controlfp(desired, _MCW_EM | _MCW_RC
#  if defined(_M_IX86)
               | _MCW_PC
#  endif
    );
    return true;

#else
    bool ok = true;

    if (std::fesetround(FE_TONEAREST) != 0) {
        ok = false;
    }

#  if defined(OPENWOW_FPU_LINUX) && !defined(__aarch64__) && !defined(__arm__)

    if (fedisableexcept(FE_ALL_EXCEPT) == -1) {
        ok = false;
    }
#  elif defined(OPENWOW_FPU_APPLE)

    (void)ok;
#  else

    (void)ok;
#  endif

    return ok;
#endif
}

}
