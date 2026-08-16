#pragma once

#include "openwow/audio/playback/sound_spatial_types.h"

#include <functional>
#include <string>

namespace openwow::game {
class CGPlayer_C;
class CGUnit_C;
}

namespace openwow::audio {

using CvarSetCallback = std::function<void(const std::string &name, const std::string &value)>;
using CvarGetBoolCallback = std::function<bool(const std::string &name)>;
using CvarGetIntCallback = std::function<int(const std::string &name)>;
using ChannelMuteCallback = std::function<void(const std::string &channel, bool mute)>;
using ActivePlayerPositionCallback = std::function<bool(float *position_out)>;
using ObjectPositionCallback = std::function<bool(std::uint64_t guid, float *position_out)>;
using UnitLookupCallback = std::function<const openwow::game::CGUnit_C *(std::uint64_t guid)>;
using PlayerLookupCallback = std::function<const openwow::game::CGPlayer_C *(std::uint64_t guid)>;
using ActivePlayerCallback = std::function<const openwow::game::CGPlayer_C *()>;
using NormalizedTimeOfDayCallback = std::function<double()>;
using LiquidQueryCallback = std::function<bool(float query_radius, LiquidQueryWorldSnapshot &snapshot)>;

}
