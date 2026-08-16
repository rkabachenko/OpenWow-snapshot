#include "openwow/ui/game/world_ui_snapshot.h"

#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/minimap_integration.h"
#include "openwow/ui/game/stateful_widget_render.h"
#include "openwow/ui/widgets/status_bar.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/world_session.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <sstream>
#include <string_view>

namespace openwow::ui::game {
namespace {

constexpr std::array<std::string_view, 18> kRequiredStockRegions = {
    "UIParent",
    "PlayerFrame",
    "PlayerPortrait",
    "PlayerFrameHealthBar",
    "PlayerFrameManaBar",
    "TargetFrame",
    "TargetFramePortrait",
    "TargetFrameHealthBar",
    "TargetFrameManaBar",
    "MainMenuBar",
    "MainMenuBarArtFrame",
    "ActionButton1",
    "ActionButton1Icon",
    "ActionButton1Cooldown",
    "MinimapCluster",
    "Minimap",
    "ChatFrame1",
    "WorldMapFrame",
};

constexpr std::array<std::pair<std::string_view, std::string_view>, 6>
    kContainedTextOwners = {{
        {"PlayerName", "PlayerFrame"},
        {"PlayerFrameHealthBarText", "PlayerFrame"},
        {"PlayerFrameManaBarText", "PlayerFrame"},
        {"TargetFrameTextureFrameName", "TargetFrame"},
        {"TargetFrameTextureFrameHealthBarText", "TargetFrame"},
        {"TargetFrameTextureFrameManaBarText", "TargetFrame"},
    }};

constexpr std::array<std::string_view, 11> kDiagnosticStockRegions = {
    "MainMenuExpBar",
    "GameTooltip",
    "GameTooltip.__BackdropBackground",
    "GameTooltipTextLeft1",
    "GameTooltipTextRight1",
    "WorldMapPositioningGuide",
    "WorldMapDetailFrame",
    "WorldMapFrameTitle",
    "WorldMapFrameCloseButton",
    "WorldMapZoomOutButton",
    "WorldMapFrameAreaLabel",
};

bool Contains(const openwow::ui::framexml::FrameRect& outer,
              const openwow::ui::framexml::FrameRect& inner,
              const int tolerance = 3) {
  return inner.x >= outer.x - tolerance && inner.y >= outer.y - tolerance &&
         inner.x + inner.width <= outer.x + outer.width + tolerance &&
         inner.y + inner.height <= outer.y + outer.height + tolerance;
}

std::string JsonEscape(const std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(ch) >= 0x20u) {
          out.push_back(ch);
        }
        break;
    }
  }
  return out;
}

std::string ReadLuaStringField(GameUIManager& manager,
                               const std::string_view frame_name,
                               const char* field) {
  lua_State* const state = manager.lua_state();
  if (state == nullptr || field == nullptr) {
    return {};
  }
  const auto ref = manager.frame_store().FindLuaRef(frame_name);
  if (!ref.has_value()) {
    return {};
  }
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, *ref);
  lua_getfield(state, -1, field);
  const char* const value = lua_isstring(state, -1) != 0
                                ? lua_tostring(state, -1)
                                : nullptr;
  const std::string result = value != nullptr ? value : "";
  lua_settop(state, top);
  return result;
}

double ReadLuaNumberField(GameUIManager& manager,
                          const std::string_view frame_name,
                          const char* field) {
  lua_State* const state = manager.lua_state();
  if (state == nullptr || field == nullptr) {
    return 0.0;
  }
  const auto ref = manager.frame_store().FindLuaRef(frame_name);
  if (!ref.has_value()) {
    return 0.0;
  }
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, *ref);
  lua_getfield(state, -1, field);
  const double result = lua_isnumber(state, -1) != 0
                            ? lua_tonumber(state, -1)
                            : 0.0;
  lua_settop(state, top);
  return result;
}

bool ReadLuaBooleanField(GameUIManager& manager,
                         const std::string_view frame_name,
                         const char* field) {
  lua_State* const state = manager.lua_state();
  if (state == nullptr || field == nullptr) {
    return false;
  }
  const auto ref = manager.frame_store().FindLuaRef(frame_name);
  if (!ref.has_value()) {
    return false;
  }
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, *ref);
  lua_getfield(state, -1, field);
  const bool result = lua_toboolean(state, -1) != 0;
  lua_settop(state, top);
  return result;
}

std::optional<bool> CallLuaBooleanGlobalWithNumber(
    GameUIManager& manager, const char* function_name, const double argument) {
  lua_State* const state = manager.lua_state();
  if (state == nullptr || function_name == nullptr) {
    return std::nullopt;
  }
  const int top = lua_gettop(state);
  lua_getglobal(state, function_name);
  if (lua_isfunction(state, -1) == 0) {
    lua_settop(state, top);
    return std::nullopt;
  }
  lua_pushnumber(state, static_cast<lua_Number>(argument));
  if (lua_pcall(state, 1, 1, 0) != 0) {
    lua_settop(state, top);
    return std::nullopt;
  }
  const bool result = lua_toboolean(state, -1) != 0;
  lua_settop(state, top);
  return result;
}

std::optional<std::string> CallLuaStringGlobalWithNumber(
    GameUIManager& manager, const char* function_name, const double argument) {
  lua_State* const state = manager.lua_state();
  if (state == nullptr || function_name == nullptr) {
    return std::nullopt;
  }
  const int top = lua_gettop(state);
  lua_getglobal(state, function_name);
  if (lua_isfunction(state, -1) == 0) {
    lua_settop(state, top);
    return std::nullopt;
  }
  lua_pushnumber(state, static_cast<lua_Number>(argument));
  if (lua_pcall(state, 1, 1, 0) != 0 || lua_isstring(state, -1) == 0) {
    lua_settop(state, top);
    return std::nullopt;
  }
  const char* const value = lua_tostring(state, -1);
  const std::string result = value != nullptr ? value : "";
  lua_settop(state, top);
  return result;
}

bool ReadStatusBarSnapshot(
    GameUIManager& manager, const std::string_view frame_name,
    openwow::ui::widgets::StatusBarSnapshot* out_status_bar) {
  if (out_status_bar == nullptr) {
    return false;
  }
  const auto* const status_bar = manager.frame_store().FindStatusBar(frame_name);
  if (status_bar == nullptr) {
    return false;
  }
  *out_status_bar = status_bar->Snapshot();
  return true;
}

bool ReadScrollingMessageState(
    GameUIManager& manager, const std::string_view frame_name,
    stateful_widgets::ScrollingMessageFrameState* out_state) {
  lua_State* const state = manager.lua_state();
  if (state == nullptr || out_state == nullptr) {
    return false;
  }
  const auto ref = manager.frame_store().FindLuaRef(frame_name);
  if (!ref.has_value()) {
    return false;
  }
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, *ref);
  const bool result = stateful_widgets::ReadScrollingMessageFrameState(
      state, -1, out_state);
  lua_settop(state, top);
  return result;
}

bool HasVisibleStatusBarStyle(
    const openwow::ui::widgets::StatusBarSnapshot& status_bar,
    const std::array<float, 4>& color) {
  const float minimum_color = std::min({color[0], color[1], color[2]});
  const float maximum_color = std::max({color[0], color[1], color[2]});

  return status_bar.has_texture && color[3] > 0.0F &&
         maximum_color - minimum_color >= 0.2f;
}

const WorldUiRegionSnapshot* FindRegion(const WorldUiSnapshot& snapshot,
                                        const std::string_view name) {
  const auto found = std::find_if(
      snapshot.regions.begin(), snapshot.regions.end(),
      [name](const WorldUiRegionSnapshot& region) {
        return region.name == name;
      });
  return found != snapshot.regions.end() ? &*found : nullptr;
}

void AddFailure(WorldUiSnapshot& snapshot, std::string failure) {
  if (std::find(snapshot.failures.begin(), snapshot.failures.end(), failure) ==
      snapshot.failures.end()) {
    snapshot.failures.push_back(std::move(failure));
  }
}

}

bool detail::RegionIntersectsViewport(
    const openwow::ui::framexml::FrameRect& rect,
    const int viewport_width, const int viewport_height) noexcept {
  if (rect.width <= 0 || rect.height <= 0 ||
      viewport_width <= 0 || viewport_height <= 0) {
    return false;
  }

  const std::int64_t left = rect.x;
  const std::int64_t top = rect.y;
  const std::int64_t right = left + rect.width;
  const std::int64_t bottom = top + rect.height;
  return right > 0 && bottom > 0 && left < viewport_width &&
         top < viewport_height;
}

bool detail::StatusBarMatchesDescriptor(
    const StatusBarDescriptorEvidence& evidence,
    const std::uint32_t descriptor_value,
    const std::uint32_t descriptor_maximum,
    const bool require_positive_value) noexcept {
  constexpr float kDescriptorTolerance = 0.01F;
  const auto matches = [=](const float actual, const std::uint32_t expected) {
    return std::isfinite(actual) &&
           std::fabs(actual - static_cast<float>(expected)) <=
               kDescriptorTolerance;
  };
  if (!evidence.styled || !matches(evidence.minimum, 0u) ||
      !matches(evidence.maximum, descriptor_maximum) ||
      !matches(evidence.value, descriptor_value) ||
      descriptor_value > descriptor_maximum ||
      (require_positive_value && descriptor_value == 0u)) {
    return false;
  }
  if (descriptor_maximum == 0u || descriptor_value == 0u) {
    return evidence.owner_submitted;
  }
  return evidence.fill_submitted;
}

WorldUiSnapshot BuildWorldUiSnapshot(GameUIManager& manager,
                                     const MinimapIntegration& minimap,
                                     const int viewport_width,
                                     const int viewport_height) {
  WorldUiSnapshot snapshot;
  snapshot.root_scale = manager.root_scale();
  snapshot.frame_xml_loaded = manager.is_loaded();
  snapshot.tracked_frame_count = manager.frame_store().binding_count();
  snapshot.render_candidate_count =
      manager.performance_counters().last_render_candidates;
  snapshot.regions.reserve(kRequiredStockRegions.size() +
                           kContainedTextOwners.size() +
                           kDiagnosticStockRegions.size());

  const auto append_region = [&](const std::string_view name) {
    WorldUiRegionSnapshot region;
    region.name = name;
    if (const auto* const frame = manager.frame_store().FindFrame(region.name);
        frame != nullptr) {
      region.present = true;
      region.kind = frame->kind;
      region.parent = frame->parent;
      region.visible = frame->visible;
      if (const auto* const rect = manager.retained_layout().FindRect(region.name);
          rect != nullptr) {
        region.rect = *rect;
        region.within_viewport =
            detail::RegionIntersectsViewport(
                *rect, viewport_width, viewport_height);
      }
    }
    snapshot.regions.push_back(std::move(region));
  };

  for (const auto name : kRequiredStockRegions) {
    append_region(name);
  }
  for (const auto& [text, owner] : kContainedTextOwners) {
    (void)owner;
    if (FindRegion(snapshot, text) == nullptr) {
      append_region(text);
    }
  }
  for (const auto name : kDiagnosticStockRegions) {
    if (FindRegion(snapshot, name) == nullptr) {
      append_region(name);
    }
  }

  snapshot.required_regions_present = std::all_of(
      kRequiredStockRegions.begin(), kRequiredStockRegions.end(),
      [&snapshot](const std::string_view name) {
        const auto* const region = FindRegion(snapshot, name);
        return region != nullptr && region->present &&
               region->rect.width > 0 && region->rect.height > 0;
      });
  if (!snapshot.required_regions_present) {
    AddFailure(snapshot, "missing_or_empty_stock_region");
  }

  const auto* const player = FindRegion(snapshot, "PlayerFrame");
  const auto* const player_portrait = FindRegion(snapshot, "PlayerPortrait");
  const auto* const player_health =
      FindRegion(snapshot, "PlayerFrameHealthBar");
  const auto* const player_mana = FindRegion(snapshot, "PlayerFrameManaBar");
  const auto* const target = FindRegion(snapshot, "TargetFrame");
  const auto* const target_portrait =
      FindRegion(snapshot, "TargetFramePortrait");
  const auto* const target_health =
      FindRegion(snapshot, "TargetFrameHealthBar");
  const auto* const target_mana = FindRegion(snapshot, "TargetFrameManaBar");
  const auto* const action = FindRegion(snapshot, "ActionButton1");
  const auto* const action_icon = FindRegion(snapshot, "ActionButton1Icon");
  const auto* const action_cooldown =
      FindRegion(snapshot, "ActionButton1Cooldown");
  const auto* const action_owner = FindRegion(snapshot, "MainMenuBarArtFrame");
  const auto* const menu = FindRegion(snapshot, "MainMenuBar");
  const auto* const minimap_region = FindRegion(snapshot, "Minimap");
  const auto* const minimap_cluster = FindRegion(snapshot, "MinimapCluster");
  const auto* const chat = FindRegion(snapshot, "ChatFrame1");
  snapshot.stock_anchors_valid =
      player != nullptr && player->within_viewport &&
      player->rect.x <= viewport_width / 8 &&
      player->rect.y <= viewport_height / 8 &&
      player_portrait != nullptr && player_health != nullptr &&
      player_mana != nullptr && Contains(player->rect, player_portrait->rect) &&
      Contains(player->rect, player_health->rect) &&
      Contains(player->rect, player_mana->rect) && target != nullptr &&
      target_portrait != nullptr && target_health != nullptr &&
      target_mana != nullptr && Contains(target->rect, target_portrait->rect) &&
      Contains(target->rect, target_health->rect) &&
      Contains(target->rect, target_mana->rect) && menu != nullptr &&
      menu->within_viewport && menu->rect.y + menu->rect.height >=
                                    viewport_height - 4 &&
      action != nullptr && action_owner != nullptr &&
      Contains(action_owner->rect, action->rect, 8) &&
      action_icon != nullptr && action_cooldown != nullptr &&
      Contains(action->rect, action_icon->rect, 2) &&
      Contains(action->rect, action_cooldown->rect, 2) &&
      minimap_region != nullptr && minimap_cluster != nullptr &&
      Contains(minimap_cluster->rect, minimap_region->rect, 4) &&
      minimap_region->rect.x + minimap_region->rect.width >=
          viewport_width - minimap_cluster->rect.width &&
      chat != nullptr && chat->within_viewport &&
      chat->rect.x <= viewport_width / 3 &&
      chat->rect.y >= viewport_height / 2;
  if (!snapshot.stock_anchors_valid) {
    AddFailure(snapshot, "stock_anchor_contract_failed");
  }

  snapshot.named_text_contained = std::all_of(
      kContainedTextOwners.begin(), kContainedTextOwners.end(),
      [&snapshot](const auto& entry) {
        const auto* const text = FindRegion(snapshot, entry.first);
        const auto* const owner = FindRegion(snapshot, entry.second);
        return text != nullptr && owner != nullptr && text->present &&
               owner->present && Contains(owner->rect, text->rect, 4);
      });
  if (!snapshot.named_text_contained) {
    AddFailure(snapshot, "named_text_outside_owner");
  }

  const auto& render_counters = manager.performance_counters();
  snapshot.player_portrait_ready =
      ReadLuaStringField(manager, "PlayerPortrait", "__ow_portrait_unit") ==
          "player" &&
      render_counters.last_render_player_portrait_submitted;
  if (!snapshot.player_portrait_ready) {
    AddFailure(snapshot, "player_portrait_not_submitted");
  }

  openwow::ui::widgets::StatusBarSnapshot health_state;
  openwow::ui::widgets::StatusBarSnapshot power_state;
  const auto health_color = manager.frame_store().FindStatusBarColor(
      "PlayerFrameHealthBar").value_or(std::array{1.0F, 1.0F, 1.0F, 1.0F});
  const auto power_color = manager.frame_store().FindStatusBarColor(
      "PlayerFrameManaBar").value_or(std::array{1.0F, 1.0F, 1.0F, 1.0F});
  const auto* const session = manager.world_session();
  const auto* const local_player =
      session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  const std::uint32_t power_type =
      local_player != nullptr
          ? std::min<std::uint32_t>(local_player->State().GetPowerType(), 6u)
          : 0u;
  const std::uint32_t descriptor_health =
      local_player != nullptr
          ? local_player->GetUInt32(openwow::game::UNIT_FIELD_HEALTH)
          : 0u;
  const std::uint32_t descriptor_maximum_health =
      local_player != nullptr
          ? local_player->GetUInt32(openwow::game::UNIT_FIELD_MAXHEALTH)
          : 0u;
  const std::uint32_t descriptor_power =
      local_player != nullptr
          ? local_player->GetUInt32(openwow::game::UNIT_FIELD_POWER1 +
                                    power_type)
          : 0u;
  const std::uint32_t descriptor_maximum_power =
      local_player != nullptr
          ? local_player->GetUInt32(openwow::game::UNIT_FIELD_MAXPOWER1 +
                                    power_type)
          : 0u;
  const bool player_owner_submitted =
      render_counters.last_render_player_frame_background_submissions > 0u;
  const bool health_bound =
      ReadStatusBarSnapshot(manager, "PlayerFrameHealthBar", &health_state) &&
      detail::StatusBarMatchesDescriptor(
          {
              .minimum = health_state.minimum,
              .maximum = health_state.maximum,
              .value = health_state.value,
              .styled = HasVisibleStatusBarStyle(health_state, health_color),
              .fill_submitted =
                  render_counters.last_render_player_health_submitted,
              .owner_submitted = player_owner_submitted,
          },
          descriptor_health, descriptor_maximum_health, true);
  const bool power_bound =
      ReadStatusBarSnapshot(manager, "PlayerFrameManaBar", &power_state) &&
      detail::StatusBarMatchesDescriptor(
          {
              .minimum = power_state.minimum,
              .maximum = power_state.maximum,
              .value = power_state.value,
              .styled = HasVisibleStatusBarStyle(power_state, power_color),
              .fill_submitted =
                  render_counters.last_render_player_power_submitted,
              .owner_submitted = player_owner_submitted,
          },
          descriptor_power, descriptor_maximum_power, false);
  snapshot.player_status_bars.frame_unit =
      ReadLuaStringField(manager, "PlayerFrame", "unit");
  snapshot.player_status_bars.health.unit =
      ReadLuaStringField(manager, "PlayerFrameHealthBar", "unit");
  snapshot.player_status_bars.power.unit =
      ReadLuaStringField(manager, "PlayerFrameManaBar", "unit");
  snapshot.player_status_bars.health.disconnected = ReadLuaBooleanField(
      manager, "PlayerFrameHealthBar", "disconnected");
  snapshot.player_status_bars.power.disconnected = ReadLuaBooleanField(
      manager, "PlayerFrameManaBar", "disconnected");
  snapshot.player_status_bars.health.red = health_color[0];
  snapshot.player_status_bars.health.green = health_color[1];
  snapshot.player_status_bars.health.blue = health_color[2];
  snapshot.player_status_bars.power.red = power_color[0];
  snapshot.player_status_bars.power.green = power_color[1];
  snapshot.player_status_bars.power.blue = power_color[2];
  snapshot.player_status_bars.health.minimum = health_state.minimum;
  snapshot.player_status_bars.health.maximum = health_state.maximum;
  snapshot.player_status_bars.health.value = health_state.value;
  snapshot.player_status_bars.health.descriptor_value = descriptor_health;
  snapshot.player_status_bars.health.descriptor_maximum =
      descriptor_maximum_health;
  snapshot.player_status_bars.health.styled =
      HasVisibleStatusBarStyle(health_state, health_color);
  snapshot.player_status_bars.health.fill_submitted =
      render_counters.last_render_player_health_submitted;
  snapshot.player_status_bars.health.owner_submitted =
      player_owner_submitted;
  snapshot.player_status_bars.power.minimum = power_state.minimum;
  snapshot.player_status_bars.power.maximum = power_state.maximum;
  snapshot.player_status_bars.power.value = power_state.value;
  snapshot.player_status_bars.power.descriptor_value = descriptor_power;
  snapshot.player_status_bars.power.descriptor_maximum =
      descriptor_maximum_power;
  snapshot.player_status_bars.power.styled =
      HasVisibleStatusBarStyle(power_state, power_color);
  snapshot.player_status_bars.power.fill_submitted =
      render_counters.last_render_player_power_submitted;
  snapshot.player_status_bars.power.owner_submitted =
      player_owner_submitted;
  const bool status_units_live =
      !snapshot.player_status_bars.frame_unit.empty() &&
      snapshot.player_status_bars.health.unit ==
          snapshot.player_status_bars.frame_unit &&
      snapshot.player_status_bars.power.unit ==
          snapshot.player_status_bars.frame_unit &&
      !snapshot.player_status_bars.health.disconnected &&
      !snapshot.player_status_bars.power.disconnected;
  snapshot.player_status_bars_ready =
      local_player != nullptr && status_units_live && health_bound && power_bound;
  if (!snapshot.player_status_bars_ready) {
    AddFailure(snapshot, "player_status_bars_not_live");
  }

  const double action_slot =
      ReadLuaNumberField(manager, "ActionButton1", "action");
  const auto populated_action =
      CallLuaBooleanGlobalWithNumber(manager, "HasAction", 1.0);
  const auto action_texture =
      CallLuaStringGlobalWithNumber(manager, "GetActionTexture", 1.0);
  const std::string assigned_icon =
      ReadLuaStringField(manager, "ActionButton1Icon", "__ow_texture");
  snapshot.action_icon_ready =
      action_slot == 1.0 && populated_action.value_or(false) &&
      action_texture.has_value() && !action_texture->empty() &&
      !assigned_icon.empty() &&
      render_counters.last_render_action_icon_submitted;
  if (!snapshot.action_icon_ready) {
    AddFailure(snapshot, "populated_action_missing_icon");
  }

  stateful_widgets::ScrollingMessageFrameState chat_state;
  if (ReadScrollingMessageState(manager, "ChatFrame1", &chat_state)) {
    snapshot.chat_message_count = chat_state.messages.size();
    snapshot.chat_fading = chat_state.fading;
    snapshot.chat_time_visible = chat_state.time_visible;
    snapshot.chat_fade_duration = chat_state.fade_duration;
    if (!chat_state.messages.empty()) {
      snapshot.chat_latest_inserted_at_seconds =
          chat_state.messages.back().inserted_at_seconds;
    }
  }
  snapshot.chat_content_ready =
      chat != nullptr && chat->visible &&
      render_counters.last_render_chat_content_submitted;
  if (!snapshot.chat_content_ready) {
    AddFailure(snapshot, "chat_content_not_submitted");
  }

  snapshot.minimap_terrain_submission_count =
      minimap.last_render_terrain_submission_count();
  snapshot.minimap_surface_ready =
      minimap.initialized() && minimap_region != nullptr &&
      minimap_region->visible && minimap_cluster != nullptr &&
      minimap_cluster->visible && minimap.terrain_translations_loaded() &&
      minimap.resident_terrain_tile_count() > 0u &&
      snapshot.minimap_terrain_submission_count ==
          minimap.resident_terrain_tile_count();
  if (!snapshot.minimap_surface_ready) {
    AddFailure(snapshot, "minimap_terrain_surface_not_submitted");
  }
  return snapshot;
}

std::string SerializeWorldUiSnapshotJson(const WorldUiSnapshot& snapshot,
                                         const std::uint32_t now_ms,
                                         const int viewport_width,
                                         const int viewport_height) {
  std::ostringstream out;
  out << "{\n  \"domain\": \"world\",\n";
  out << "  \"now_ms\": " << now_ms << ",\n";
  out << "  \"viewport\": {\"w\": " << viewport_width
      << ", \"h\": " << viewport_height << "},\n";
  out << "  \"rootScale\": " << snapshot.root_scale << ",\n";
  out << "  \"ready\": " << (snapshot.ready() ? "true" : "false")
      << ",\n";
  out << "  \"frameXmlLoaded\": "
      << (snapshot.frame_xml_loaded ? "true" : "false") << ",\n";
  out << "  \"requiredRegionsPresent\": "
      << (snapshot.required_regions_present ? "true" : "false") << ",\n";
  out << "  \"anchorsValid\": "
      << (snapshot.stock_anchors_valid ? "true" : "false") << ",\n";
  out << "  \"namedTextContained\": "
      << (snapshot.named_text_contained ? "true" : "false") << ",\n";
  out << "  \"playerPortraitReady\": "
      << (snapshot.player_portrait_ready ? "true" : "false") << ",\n";
  out << "  \"playerStatusBarsReady\": "
      << (snapshot.player_status_bars_ready ? "true" : "false") << ",\n";
  out << "  \"playerStatusBars\": {\"frameUnit\": \""
      << JsonEscape(snapshot.player_status_bars.frame_unit)
      << "\", \"healthUnit\": \""
      << JsonEscape(snapshot.player_status_bars.health.unit)
      << "\", \"powerUnit\": \""
      << JsonEscape(snapshot.player_status_bars.power.unit)
      << "\", \"healthDisconnected\": "
      << (snapshot.player_status_bars.health.disconnected ? "true" : "false")
      << ", \"powerDisconnected\": "
      << (snapshot.player_status_bars.power.disconnected ? "true" : "false")
      << ", \"healthColor\": [" << snapshot.player_status_bars.health.red
      << ", " << snapshot.player_status_bars.health.green << ", "
      << snapshot.player_status_bars.health.blue << "], \"powerColor\": ["
      << snapshot.player_status_bars.power.red << ", "
      << snapshot.player_status_bars.power.green << ", "
      << snapshot.player_status_bars.power.blue << "], \"healthRange\": ["
      << snapshot.player_status_bars.health.minimum << ", "
      << snapshot.player_status_bars.health.maximum << ", "
      << snapshot.player_status_bars.health.value
      << "], \"healthDescriptor\": ["
      << snapshot.player_status_bars.health.descriptor_value << ", "
      << snapshot.player_status_bars.health.descriptor_maximum
      << "], \"healthRender\": ["
      << (snapshot.player_status_bars.health.styled ? "true" : "false")
      << ", "
      << (snapshot.player_status_bars.health.fill_submitted ? "true" : "false")
      << ", "
      << (snapshot.player_status_bars.health.owner_submitted ? "true" : "false")
      << "], \"powerRange\": ["
      << snapshot.player_status_bars.power.minimum << ", "
      << snapshot.player_status_bars.power.maximum << ", "
      << snapshot.player_status_bars.power.value
      << "], \"powerDescriptor\": ["
      << snapshot.player_status_bars.power.descriptor_value << ", "
      << snapshot.player_status_bars.power.descriptor_maximum
      << "], \"powerRender\": ["
      << (snapshot.player_status_bars.power.styled ? "true" : "false")
      << ", "
      << (snapshot.player_status_bars.power.fill_submitted ? "true" : "false")
      << ", "
      << (snapshot.player_status_bars.power.owner_submitted ? "true" : "false")
      << "]},\n";
  out << "  \"actionIconReady\": "
      << (snapshot.action_icon_ready ? "true" : "false") << ",\n";
  out << "  \"chatContentReady\": "
      << (snapshot.chat_content_ready ? "true" : "false") << ",\n";
  out << "  \"chatMessageCount\": " << snapshot.chat_message_count << ",\n";
  out << "  \"chatTiming\": {\"fading\": "
      << (snapshot.chat_fading ? "true" : "false")
      << ", \"timeVisible\": " << snapshot.chat_time_visible
      << ", \"fadeDuration\": " << snapshot.chat_fade_duration
      << ", \"latestInsertedAt\": "
      << snapshot.chat_latest_inserted_at_seconds << "},\n";
  out << "  \"minimapSurfaceReady\": "
      << (snapshot.minimap_surface_ready ? "true" : "false") << ",\n";
  out << "  \"minimapTerrainSubmissions\": "
      << snapshot.minimap_terrain_submission_count << ",\n";
  out << "  \"trackedFrames\": " << snapshot.tracked_frame_count
      << ",\n  \"renderCandidates\": "
      << snapshot.render_candidate_count << ",\n";
  out << "  \"failures\": [";
  for (std::size_t index = 0; index < snapshot.failures.size(); ++index) {
    if (index != 0u) out << ", ";
    out << "\"" << JsonEscape(snapshot.failures[index]) << "\"";
  }
  out << "],\n  \"regions\": [\n";
  for (std::size_t index = 0; index < snapshot.regions.size(); ++index) {
    const auto& region = snapshot.regions[index];
    out << "    {\"name\": \"" << JsonEscape(region.name)
        << "\", \"kind\": \"" << JsonEscape(region.kind)
        << "\", \"parent\": \"" << JsonEscape(region.parent)
        << "\", \"present\": " << (region.present ? "true" : "false")
        << ", \"visible\": " << (region.visible ? "true" : "false")
        << ", \"withinViewport\": "
        << (region.within_viewport ? "true" : "false")
        << ", \"x\": " << region.rect.x << ", \"y\": " << region.rect.y
        << ", \"w\": " << region.rect.width << ", \"h\": "
        << region.rect.height << "}";
    if (index + 1u != snapshot.regions.size()) out << ',';
    out << '\n';
  }
  out << "  ]\n}\n";
  return out.str();
}

}
