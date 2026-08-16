
#pragma once

#include "openwow/ui/framexml/ui_frame.h"
#include "openwow/ui/framexml/xml_script_cache.h"

#include <optional>
#include <utility>

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::anim {

void PushAnimGroupTable(lua_State* L, int parent_frame_idx);

int CreateAnimationGroupOnRegion(lua_State* L, int region_idx, const char* name);

void PushAnimTable(lua_State* L, const char* anim_type, int group_idx);

void UnregisterAnimationScriptMethods(lua_State* L);

int PushRegionAnimationGroups(lua_State* L, int region_idx);

void StopRegionAnimationGroups(lua_State* L, int region_idx);

void UpdateRegionAnimationGroups(lua_State* L, int region_idx, float elapsed_seconds);
struct RegionAnimationState {
  float translation_x{0.0f};
  float translation_y{0.0f};
  float scale_x{1.0f};
  float scale_y{1.0f};
  float rotation_radians{0.0f};
  std::optional<float> alpha;
  float alpha_change{0.0f};
};
[[nodiscard]] RegionAnimationState GetRegionAnimationState(lua_State* L, int region_idx);
[[nodiscard]] std::pair<float, float> GetRegionAnimationTranslation(lua_State* L,
                                                                    int region_idx);

void ApplyAnimationRegionMethods(lua_State* L);

void ApplyAnimationFrameMethods(lua_State* L);

void MaterializeAnimationGroups(
    lua_State* L,
    int frame_idx,
    const std::vector<openwow::ui::framexml::UiAnimationGroup>& groups,
    openwow::ui::framexml::XmlScriptCache* script_cache = nullptr);

void ApplyFrameXmlLoadBehavior(
    lua_State* L,
    int frame_idx,
    int parent_idx,
    const openwow::ui::framexml::UiFrame& frame,
    openwow::ui::framexml::XmlScriptCache* script_cache = nullptr);

}
