
#pragma once

#include <cstdint>

namespace openwow::ui::anim {

enum class AnimLoopType : uint8_t {
    None = 0,
    Repeat = 1,
    Bounce = 2,
};

enum class AnimCurveType : uint8_t {
    None  = 0,
    In    = 1,
    Out   = 2,
    InOut = 3,
    OutIn = 4,
};

enum class AnimLoopState : uint8_t {
    None    = 0,
    Forward = 1,
    Reverse = 2,
};

enum class AnimState : uint8_t {
    Stopped = 0,
    Playing = 1,
    Paused  = 2,
};

enum class AnimKind : uint8_t {
    Alpha       = 0,
    Scale       = 1,
    Translation = 2,
    Rotation    = 3,
    Path        = 4,
    Animation   = 5,
};

}
