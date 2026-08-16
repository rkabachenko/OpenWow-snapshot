#pragma once

namespace openwow::render {

inline constexpr double kUnitInstanceRenderMicroseconds = 8.4;

inline constexpr double kMountInstanceRenderMicroseconds = kUnitInstanceRenderMicroseconds;

inline constexpr double kSpellVisualInstanceRenderMicroseconds =
    kUnitInstanceRenderMicroseconds;

inline constexpr double kCEffectInstanceRenderMicroseconds =
    kSpellVisualInstanceRenderMicroseconds;

inline constexpr double kAttachmentInstanceRenderMicroseconds = 1.15;

}
