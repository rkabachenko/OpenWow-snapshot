
#pragma once

extern "C" {
#include <lua.hpp>
}

#include <cstdint>

namespace openwow::ui::game::frame_api {

void ApplyScrollFrameWidgetExtras(lua_State* L);

inline constexpr std::uint64_t kUnpublishedScrollFrameRanges =
    ~static_cast<std::uint64_t>(0);
void RefreshScrollFrameWidgetState(lua_State* L,
                                   std::uint64_t* published_ranges_generation);

void ApplyScrollingMessageFrameWidgetExtras(lua_State* L);

}
