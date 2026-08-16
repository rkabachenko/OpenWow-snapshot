#include "openwow/ui/lua_c_api_convenience.h"

#include "openwow/ui/game/tooltip_system.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/arena_system.h"
#include "openwow/game/action_validation_utils.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/gossip_manager.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/localization.h"
#include "openwow/game/commerce/merchants/merchant_requirements.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_cooldown_state.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/unit_level_display.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/tooltip_builders.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/loot_tooltip_support.h"
#include "openwow/ui/game/tooltip_builder.h"
#include "openwow/ui/ui_enum_helpers.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <chrono>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <string_view>
#include <utility>

namespace {

constexpr float kFrameStackHeaderRightColorR = 1.0f;
constexpr float kFrameStackHeaderRightColorG = 210.0f / 255.0f;
constexpr float kFrameStackHeaderRightColorB = 0.0f;
constexpr float kPendingItemInfoColor = 32.0f / 255.0f;
constexpr std::uintptr_t kTooltipItemTemplateCallbackFunctionId = 0x626650u;
constexpr std::uintptr_t kTooltipWorldGameObjectCallbackFunctionId = 0x626720u;
constexpr std::uint32_t kTooltipGoldArgb = 0xFFFFD200u;
constexpr std::uint32_t kTooltipWhiteArgb = 0xFFFFFFFFu;
constexpr std::uint32_t kTooltipGreyArgb = 0xFF808080u;
constexpr std::uint32_t kTooltipGreenArgb = 0xFF40C040u;
constexpr std::uint32_t kTooltipYellowArgb = 0xFFFFFF00u;
constexpr std::uint32_t kTooltipOrangeArgb = 0xFFFF8040u;
constexpr std::uint32_t kTooltipRedArgb = 0xFFFF2020u;
constexpr std::uint32_t kTooltipStatusBarGoType = 33u;
constexpr std::uint32_t kSpellEffectOpenLock = 33u;
constexpr std::size_t kMaxTooltipTextures = 10u;

struct TooltipStatusBarSnapshot {
  bool shown = false;
  float min = 0.0f;
  float max = 1.0f;
  float value = 0.0f;
};

struct WorldGameObjectTooltipRequirementState {
  bool has_known_requirement = false;
  bool unsatisfied_requirement = false;
  bool has_carried_key = false;
  std::uint32_t carried_key_item_entry = 0;
  bool has_open_lock_skill = false;
  std::uint32_t current_skill = 0;
  std::uint32_t required_skill = 0;
};

struct WorldGameObjectTooltipSecondaryLock {
  std::uint32_t type = 0;
  std::uint32_t index = 0;
  std::uint32_t skill = 0;
  std::uint32_t action = 0;
};

openwow::game::AsyncQueryChannel::CallbackKey BuildTooltipItemTemplateCallbackKey(
    const std::uint32_t callback_cookie) {
  return openwow::game::AsyncQueryChannel::CallbackKey(
      kTooltipItemTemplateCallbackFunctionId, callback_cookie);
}

std::uint32_t AllocateTooltipCallbackCookie() noexcept {
  static std::atomic<std::uint32_t> next_cookie{1u};
  std::uint32_t cookie = next_cookie.fetch_add(1u, std::memory_order_relaxed);
  if (cookie == 0u) {
    cookie = next_cookie.fetch_add(1u, std::memory_order_relaxed);
  }
  return cookie;
}

std::string ExpandSimpleRenderTooltipText(std::string_view text) {
  if (text.empty()) {
    return {};
  }

  return openwow::game::ExpandLocalizedTextTags(text,
                                                openwow::game::Localization::Get().GetLocale());
}

constexpr std::uint32_t kFrameStackVisibleMouseEnabledColors[2] = {
    0xFFFF9966u,
    0xFFDDDD88u,
};
constexpr std::uint32_t kFrameStackVisibleColors[2] = {
    0xFF6699FFu,
    0xFF88DDDDu,
};
constexpr std::uint32_t kFrameStackHiddenColors[2] = {
    0xFF666666u,
    0xFF999999u,
};

float ArgbChannelToFloat(const std::uint32_t argb, const unsigned shift) {
  return static_cast<float>((argb >> shift) & 0xFFu) / 255.0f;
}

float QuantizeTooltipColorChannel(const float value) {

  if (!(value > 0.0f)) {
    return 0.0f;
  }
  if (value >= 1.0f) {
    return 1.0f;
  }

  return static_cast<float>(
             std::floor(static_cast<double>(value) * 255.0 + 0.5)) /
         255.0f;
}

openwow::game::AsyncQueryChannel::CallbackKey
BuildTooltipWorldGameObjectCallbackKey(const std::uint32_t generation) {
  return openwow::game::AsyncQueryChannel::CallbackKey(
      kTooltipWorldGameObjectCallbackFunctionId, generation);
}

std::string FormatMoneyText(const std::string &prefix, const std::uint32_t money,
                            const std::string &suffix) {
  const std::uint32_t gold = money / 10000;
  const std::uint32_t silver = (money / 100) % 100;
  const std::uint32_t copper = money % 100;

  char buffer[128];
  if (gold > 0) {
    std::snprintf(buffer, sizeof(buffer), "%s%ug %us %uc%s", prefix.c_str(), gold, silver, copper,
                  suffix.c_str());
  } else if (silver > 0) {
    std::snprintf(buffer, sizeof(buffer), "%s%us %uc%s", prefix.c_str(), silver, copper,
                  suffix.c_str());
  } else {
    std::snprintf(buffer, sizeof(buffer), "%s%uc%s", prefix.c_str(), copper, suffix.c_str());
  }

  return buffer;
}

void AddArgbLine(openwow::ui::game::TooltipSystem &tooltip, const std::string &text,
                 const std::uint32_t argb) {
  tooltip.AddLine(text, ArgbChannelToFloat(argb, 16), ArgbChannelToFloat(argb, 8),
                  ArgbChannelToFloat(argb, 0));
}

void AddArgbLine(openwow::ui::game::TooltipSystem &tooltip, const std::string &text,
                 const std::uint32_t argb, const bool wrap) {
  tooltip.AddLine(text, ArgbChannelToFloat(argb, 16), ArgbChannelToFloat(argb, 8),
                  ArgbChannelToFloat(argb, 0), wrap);
}

std::string ToLower(std::string value) {
  for (char &ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

openwow::game::ObjectGuid ResolveTooltipUnitGuid(const openwow::game::ObjectManager &objects,
                                                 const std::string &unit_token) {
  const std::string token = ToLower(unit_token);
  if (token == "mouseover") {
    return objects.GetMouseoverGuid();
  }
  if (token == "target") {
    return objects.GetTargetGuid();
  }
  if (token == "focus") {
    return objects.GetFocusTargetGuid();
  }
  if (token == "player") {
    return objects.GetLocalPlayerGuid();
  }
  if (token == "pet") {
    const auto *player = objects.GetLocalPlayer();
    if (player != nullptr) {
      return player->GetGuidField(openwow::game::UNIT_FIELD_SUMMON);
    }
  }
  return {};
}

float ParseColorChannel(std::string_view hex, std::size_t offset) {
  const auto nibble = [](char ch) -> unsigned int {
    if (ch >= '0' && ch <= '9') {
      return static_cast<unsigned int>(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
      return static_cast<unsigned int>(ch - 'a' + 10);
    }
    if (ch >= 'A' && ch <= 'F') {
      return static_cast<unsigned int>(ch - 'A' + 10);
    }
    return 0;
  };

  if (hex.size() < offset + 2) {
    return 1.0f;
  }

  const auto value = (nibble(hex[offset]) << 4) | nibble(hex[offset + 1]);
  return static_cast<float>(value) / 255.0f;
}

float ClampStatusBarValue(const float min_value, const float max_value,
                         const float value) {
  return std::clamp(value, min_value, max_value);
}

bool CallLuaFrameMethod(lua_State *L, const char *global_name, const char *method_name,
                        const std::function<int(lua_State *)> &push_args) {
  if (L == nullptr || global_name == nullptr || method_name == nullptr) {
    return false;
  }

  const int base = lua_gettop(L);
  lua_getglobal(L, global_name);
  if (lua_istable(L, -1) == 0) {
    lua_settop(L, base);
    return false;
  }

  lua_getfield(L, -1, method_name);
  if (lua_isfunction(L, -1) == 0) {
    lua_settop(L, base);
    return false;
  }

  lua_pushvalue(L, -2);
  int arg_count = 1;
  if (push_args) {
    arg_count += push_args(L);
  }

  const bool ok = lua_pcall(L, arg_count, 0, 0) == LUA_OK;
  lua_settop(L, base);
  return ok;
}

void SyncLiveWorldGameObjectTooltipFrame(const std::string &owner, const std::string &anchor,
                                         const std::vector<openwow::ui::game::TooltipLine> &lines,
                                         const TooltipStatusBarSnapshot &status_bar) {
  auto *const manager = openwow::ui::game::runtime::WorldUiRuntimeContext::FromActiveLua();
  if (manager == nullptr || !manager->is_initialized()) {
    return;
  }

  lua_State *const lua = manager->lua_state();
  if (lua == nullptr) {
    return;
  }

  const std::string effective_owner = owner.empty() ? "WorldFrame" : owner;
  (void)CallLuaFrameMethod(lua, "GameTooltip", "SetOwner",
                           [&](lua_State *state) {
                             lua_pushlstring(state, effective_owner.c_str(),
                                             effective_owner.size());
                             lua_pushlstring(state, anchor.c_str(), anchor.size());
                             return 2;
                           });
  (void)CallLuaFrameMethod(lua, "GameTooltip", "ClearLines", nullptr);

  for (const auto &line : lines) {
    if (!line.right_text.empty()) {
      (void)CallLuaFrameMethod(lua, "GameTooltip", "AddDoubleLine",
                               [&](lua_State *state) {
                                 lua_pushlstring(state, line.left_text.c_str(),
                                                 line.left_text.size());
                                 lua_pushlstring(state, line.right_text.c_str(),
                                                 line.right_text.size());
                                 lua_pushnumber(state, line.left_r);
                                 lua_pushnumber(state, line.left_g);
                                 lua_pushnumber(state, line.left_b);
                                 lua_pushnumber(state, line.right_r);
                                 lua_pushnumber(state, line.right_g);
                                 lua_pushnumber(state, line.right_b);
                                 return 8;
                               });
      continue;
    }

    (void)CallLuaFrameMethod(lua, "GameTooltip", "AddLine",
                             [&](lua_State *state) {
                               lua_pushlstring(state, line.left_text.c_str(),
                                               line.left_text.size());
                               lua_pushnumber(state, line.left_r);
                               lua_pushnumber(state, line.left_g);
                               lua_pushnumber(state, line.left_b);
                               lua_pushboolean(state, line.wrap ? 1 : 0);
                               return 5;
                             });
  }

  (void)CallLuaFrameMethod(lua, "GameTooltip", "Show", nullptr);

  if (status_bar.shown) {
    (void)CallLuaFrameMethod(lua, "GameTooltipStatusBar", "SetMinMaxValues",
                             [&](lua_State *state) {
                               lua_pushnumber(state, status_bar.min);
                               lua_pushnumber(state, status_bar.max);
                               return 2;
                             });
    (void)CallLuaFrameMethod(lua, "GameTooltipStatusBar", "SetValue",
                             [&](lua_State *state) {
                               lua_pushnumber(state, status_bar.value);
                               return 1;
                             });
    (void)CallLuaFrameMethod(lua, "GameTooltipStatusBar", "Show", nullptr);
  } else {
    (void)CallLuaFrameMethod(lua, "GameTooltipStatusBar", "Hide", nullptr);
  }
}

void SyncLiveWorldGameObjectStatusBar(const TooltipStatusBarSnapshot &status_bar) {
  auto *const manager = openwow::ui::game::runtime::WorldUiRuntimeContext::FromActiveLua();
  if (manager == nullptr || !manager->is_initialized()) {
    return;
  }

  lua_State *const lua = manager->lua_state();
  if (lua == nullptr) {
    return;
  }

  if (status_bar.shown) {
    (void)CallLuaFrameMethod(lua, "GameTooltipStatusBar", "SetMinMaxValues",
                             [&](lua_State *state) {
                               lua_pushnumber(state, status_bar.min);
                               lua_pushnumber(state, status_bar.max);
                               return 2;
                             });
    (void)CallLuaFrameMethod(lua, "GameTooltipStatusBar", "SetValue",
                             [&](lua_State *state) {
                               lua_pushnumber(state, status_bar.value);
                               return 1;
                             });
    (void)CallLuaFrameMethod(lua, "GameTooltipStatusBar", "Show", nullptr);
    return;
  }

  (void)CallLuaFrameMethod(lua, "GameTooltipStatusBar", "Hide", nullptr);
}

void HideLiveWorldGameObjectTooltipFrame() {
  auto *const manager = openwow::ui::game::runtime::WorldUiRuntimeContext::FromActiveLua();
  if (manager == nullptr || !manager->is_initialized()) {
    return;
  }

  lua_State *const lua = manager->lua_state();
  if (lua == nullptr) {
    return;
  }

  (void)CallLuaFrameMethod(lua, "GameTooltipStatusBar", "Hide", nullptr);
  (void)CallLuaFrameMethod(lua, "GameTooltip", "Hide", nullptr);
}

bool IsLockActionApplicable(const openwow::game::CGGameObject_C &game_object,
                            const std::uint32_t action) {
  const auto state_byte = static_cast<std::uint8_t>(game_object.GetGoState());
  const auto ready_state =
      static_cast<std::uint8_t>(openwow::game::GOState::Ready);
  const auto active_state =
      static_cast<std::uint8_t>(openwow::game::GOState::Active);
  const auto active_alternative_state =
      static_cast<std::uint8_t>(openwow::game::GOState::ActiveAlternative);
  const auto is_locked = (game_object.GetFlags() & openwow::game::GO_FLAG_LOCKED) != 0u;

  if (action == 4u) {
    return state_byte == active_alternative_state;
  }

  if (state_byte == active_alternative_state ||
      (((action < 2u) || action == 3u) && state_byte != ready_state)) {
    return false;
  }

  if (action == 0u) {
    return !is_locked;
  }

  if (action == 1u) {
    return is_locked;
  }

  if (action == 2u && state_byte != active_state) {
    return false;
  }

  return true;
}

const openwow::data::dbc::SpellEntry *LookupSpellEntry(
    const openwow::data::dbc::DbcLoader &dbc, const std::uint32_t spell_id) {
  if (spell_id == 0u) {
    return nullptr;
  }

  return dbc.spell().LookupEntry(spell_id);
}

std::uint32_t GetSpellEffectMagnitude(
    const openwow::data::dbc::SpellEntry &spell, const std::size_t effect_index) {
  if (effect_index >= spell.effect_base_points.size()) {
    return 0u;
  }

  return static_cast<std::uint32_t>(
      std::max(0, spell.effect_base_points[effect_index] + 1));
}

bool SpellSatisfiesOpenLockRequirement(
    const openwow::data::dbc::SpellEntry &spell, const std::uint32_t lock_index,
    const std::uint32_t required_value, std::uint32_t *const current_value_out) {
  for (std::size_t effect_index = 0; effect_index < spell.effect.size(); ++effect_index) {
    if (spell.effect[effect_index] != kSpellEffectOpenLock ||
        static_cast<std::uint32_t>(spell.effect_misc_value[effect_index]) != lock_index) {
      continue;
    }

    const auto current_value = GetSpellEffectMagnitude(spell, effect_index);
    if (current_value_out != nullptr) {
      *current_value_out = current_value;
    }
    if (current_value >= required_value) {
      return true;
    }
  }

  return false;
}

bool SpellHasAnyOpenLockEffect(const openwow::data::dbc::SpellEntry &spell) {
  for (const auto effect : spell.effect) {
    if (effect == kSpellEffectOpenLock) {
      return true;
    }
  }

  return false;
}

bool ResolveAvailableOpenLockSkill(const openwow::game::CGGameObject_C &game_object,
                                   const openwow::data::dbc::DbcLoader &dbc,
                                   const openwow::game::SpellCastRuntime &spells,
                                   const std::uint32_t lock_index,
                                   const std::uint32_t required_skill,
                                   std::uint32_t *const current_value_out,
                                   std::uint32_t *const required_value_out) {
  const auto required_value =
      required_skill != 0u ? required_skill : 5u * game_object.GetLevel();
  if (required_value_out != nullptr) {
    *required_value_out = required_value;
  }

  for (const auto &known_spell :
       openwow::game::SpellbookSystem::Get().GetKnownSpellList()) {
    const auto *const spell = LookupSpellEntry(dbc, known_spell.spell_id);
    if (spell == nullptr) {
      continue;
    }

    std::uint32_t current_value = 0u;
    if (SpellSatisfiesOpenLockRequirement(*spell, lock_index, required_value,
                                          &current_value)) {
      if (current_value_out != nullptr) {
        *current_value_out = current_value;
      }
      return true;
    }
  }

  if (spells.GetTargeting().GetSpellId() == 0u) {
    return false;
  }

  const auto *const current_spell =
      LookupSpellEntry(dbc, spells.GetCurrentSpellId());
  if (current_spell == nullptr) {
    return false;
  }

  std::uint32_t current_value = 0u;
  if (!SpellSatisfiesOpenLockRequirement(*current_spell, lock_index, required_value,
                                         &current_value)) {
    return false;
  }

  if (current_value_out != nullptr) {
    *current_value_out = current_value;
  }
  return true;
}

WorldGameObjectTooltipRequirementState BuildWorldGameObjectTooltipRequirementState(
    const openwow::game::CGGameObject_C &game_object,
    const openwow::game::PlayerInventoryReplica& inventory,
    const openwow::data::dbc::DbcLoader &dbc,
    const openwow::game::SpellCastRuntime &spells) {
  WorldGameObjectTooltipRequirementState state;
  const auto *const lock_entry = game_object.GetLockEntry();
  if (lock_entry == nullptr) {
    return state;
  }

  for (std::size_t index = 0; index < lock_entry->type.size(); ++index) {
    const auto type = lock_entry->type[index];
    if (type == 0u) {
      continue;
    }

    switch (type) {
    case 1u:
      state.has_known_requirement = true;
      if (IsLockActionApplicable(game_object, lock_entry->action[index]) &&
          inventory.FindItemByEntry(lock_entry->index[index]) >= 0) {
        state.has_carried_key = true;
        state.carried_key_item_entry = lock_entry->index[index];
        return state;
      }
      break;

    case 2u:
      state.has_known_requirement = true;
      if (IsLockActionApplicable(game_object, lock_entry->action[index])) {
        std::uint32_t current_skill = 0u;
        std::uint32_t required_skill = 0u;
        if (ResolveAvailableOpenLockSkill(game_object, dbc, spells,
                                          lock_entry->index[index], lock_entry->skill[index],
                                          &current_skill, &required_skill)) {
          state.has_open_lock_skill = true;
          state.current_skill = current_skill;
          state.required_skill = required_skill;
          return state;
        }
      }
      break;

    case 3u: {
      state.has_known_requirement = true;
      if (!IsLockActionApplicable(game_object, lock_entry->action[index])) {
        break;
      }

      const auto *const spell = LookupSpellEntry(dbc, lock_entry->index[index]);
      if (spell != nullptr && SpellHasAnyOpenLockEffect(*spell)) {
        return state;
      }
      break;
    }

    default:
      break;
    }
  }

  state.unsatisfied_requirement = state.has_known_requirement;
  return state;
}

std::uint32_t ResolveLockedRequirementColor(
    const WorldGameObjectTooltipRequirementState &state) {
  if (state.unsatisfied_requirement) {
    return kTooltipRedArgb;
  }
  if (state.has_open_lock_skill) {
    const auto current = state.current_skill;
    const auto required = state.required_skill;
    if (current >= required + 100u) {
      return kTooltipGreyArgb;
    }
    if (current >= required + 50u) {
      return kTooltipGreenArgb;
    }
    if (current >= required + 25u) {
      return kTooltipYellowArgb;
    }
    if (current >= required) {
      return kTooltipOrangeArgb;
    }
    return kTooltipRedArgb;
  }
  return kTooltipGreenArgb;
}

std::optional<WorldGameObjectTooltipSecondaryLock>
ResolveWorldGameObjectTooltipSecondaryLock(
    const openwow::data::dbc::LockEntry *const lock_entry) {
  if (lock_entry == nullptr) {
    return std::nullopt;
  }

  for (std::size_t index = 0; index < lock_entry->type.size(); ++index) {
    if (lock_entry->type[index] == 0u) {
      continue;
    }

    return WorldGameObjectTooltipSecondaryLock{
        .type = lock_entry->type[index],
        .index = lock_entry->index[index],
        .skill = lock_entry->skill[index],
        .action = lock_entry->action[index],
    };
  }

  return std::nullopt;
}

void AppendBuilderLine(openwow::ui::game::TooltipSystem &tooltip,
                       const openwow::ui::TooltipLine &line) {
  const float left_r = ParseColorChannel(line.color, 2);
  const float left_g = ParseColorChannel(line.color, 4);
  const float left_b = ParseColorChannel(line.color, 6);

  if (!line.right_text.empty()) {
    const float right_r = ParseColorChannel(line.right_color, 2);
    const float right_g = ParseColorChannel(line.right_color, 4);
    const float right_b = ParseColorChannel(line.right_color, 6);
    tooltip.AddDoubleLine(line.text, line.right_text, left_r, left_g, left_b, right_r, right_g,
                          right_b);
    return;
  }

  tooltip.AddLine(line.text, left_r, left_g, left_b, line.wrap);
}

const openwow::game::VendorItem *FindVendorItemByItemId(const openwow::game::WorldSession &session,
                                                        const std::uint32_t item_id) {
  if (item_id == 0 || !session.gossip().merchant().active()) {
    return nullptr;
  }

  const auto &vendor = session.gossip().merchant().snapshot();
  for (const auto &vendor_item : vendor.items) {
    if (vendor_item.item_id == item_id) {
      return &vendor_item;
    }
  }

  return nullptr;
}

void AppendEquipmentSetMembershipLine(openwow::ui::game::TooltipSystem &tooltip,
                                      const openwow::game::EquipmentSets* equipment,
                                      const std::uint64_t item_guid) {
  if (equipment == nullptr || item_guid == 0) {
    return;
  }

  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  if (!cvars.GetCVarBool("equipmentManager") ||
      cvars.GetCVarBool("dontShowEquipmentSetsOnItems")) {
    return;
  }

  auto &localization = openwow::game::Localization::Get();
  const std::string delimiter = localization.GetString("PLAYER_LIST_DELIMITER", "");
  const std::string set_names =
      equipment->names_containing(openwow::game::ObjectGuid(item_guid), delimiter, 1024);
  if (set_names.empty()) {
    return;
  }

  const std::string format = localization.GetString("EQUIPMENT_SETS", "EQUIPMENT_SETS");
  tooltip.AddLine(localization.FormatString(format, {set_names}), 1.0f, 1.0f, 1.0f, true);
}

void AddMerchantRequirementLine(openwow::ui::game::TooltipSystem &tooltip, const std::string &text,
                                const bool satisfied) {
  if (text.empty()) {
    return;
  }

  if (satisfied) {
    tooltip.AddLine(text, 1.0f, 1.0f, 1.0f);
    return;
  }

  tooltip.AddLine(text, 1.0f, 0.1f, 0.1f);
}

std::string FormatMerchantRequirementLine(const char *key, const std::string &argument) {
  auto &localization = openwow::game::Localization::Get();
  return localization.FormatString(localization.GetString(key, key), {argument});
}

void AppendMerchantRequirementLines(openwow::ui::game::TooltipSystem &tooltip,
                                    const std::uint32_t item_id,
                                    const openwow::data::dbc::DbcLoader *fallback_dbc) {
  const auto *session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return;
  }

  const auto *vendor_item = FindVendorItemByItemId(*session, item_id);
  if (vendor_item == nullptr || vendor_item->extended_cost == 0) {
    return;
  }

  const auto *dbc = session->GetDbcLoader();
  if (dbc == nullptr) {
    dbc = fallback_dbc;
  }
  if (dbc == nullptr) {
    return;
  }

  const auto *extended_cost = dbc->item_extended_cost().LookupEntry(vendor_item->extended_cost);
  if (extended_cost == nullptr) {
    return;
  }

  if (extended_cost->personal_arena_rating != 0) {
    const char *format_key = "ITEM_REQ_ARENA_RATING";
    if (extended_cost->arena_slot == 1) {
      format_key = "ITEM_REQ_ARENA_RATING_3V3";
    } else if (extended_cost->arena_slot == 2) {
      format_key = "ITEM_REQ_ARENA_RATING_5V5";
    }

    AddMerchantRequirementLine(
        tooltip,
        FormatMerchantRequirementLine(format_key,
                                      std::to_string(extended_cost->personal_arena_rating)),
        openwow::game::MeetsMerchantArenaRatingRequirement(
            *extended_cost, session->merchant_arena_team_query()));
  }

  if (extended_cost->item_purchase_group == 0) {
    return;
  }

  const auto *purchase_group =
      dbc->item_purchase_group().LookupEntry(extended_cost->item_purchase_group);
  if (purchase_group == nullptr) {
    return;
  }

  bool has_group_item = false;
  for (const auto required_item_id : purchase_group->item_id) {
    if (required_item_id == 0) {
      continue;
    }

    if (session->inventory_replica().FindItemByEntry(required_item_id) >= 0) {
      has_group_item = true;
      break;
    }
  }

  const std::string group_name =
      purchase_group->name.empty()
          ? std::to_string(purchase_group->id)
          : std::string(purchase_group->name.data(), purchase_group->name.size());
  AddMerchantRequirementLine(tooltip,
                             FormatMerchantRequirementLine("ITEM_REQ_PURCHASE_GROUP", group_name),
                             has_group_item);
}

std::string ResolveWorldGameObjectTooltipName(
    const openwow::game::CGGameObject_C &game_object,
    const openwow::game::GameObjectTemplateInfo *const template_info) {
  if (template_info != nullptr && !template_info->name.empty()) {
    return template_info->name;
  }

  if (const char *const stats_name = game_object.GetStatsName();
      stats_name != nullptr && stats_name[0] != '\0') {
    return stats_name;
  }

  return game_object.GetTooltipText();
}

void AppendWorldGameObjectRequirementLine(openwow::ui::game::TooltipSystem &tooltip,
                                          const std::string &text,
                                          const std::uint32_t argb,
                                          const bool wrap = false) {
  if (text.empty()) {
    return;
  }

  AddArgbLine(tooltip, text, argb, wrap);
}

void AppendWorldGameObjectLockLines(
    openwow::ui::game::TooltipSystem &tooltip,
    const openwow::game::CGGameObject_C &game_object,
    openwow::game::WorldSession &session, const std::uint32_t generation,
    bool *const requested_async) {
  const auto *const dbc = session.GetDbcLoader();
  const auto *const lock_entry = game_object.GetLockEntry();
  const auto request_options = openwow::game::AsyncQueryChannel::RequestOptions{
      .context = game_object.GetGuid().GetRawValue(),
      .callback_key = BuildTooltipWorldGameObjectCallbackKey(generation),
      .dedupe_callbacks = true,
      .callback = tooltip.BindAsyncCallback(
          [generation](openwow::ui::game::TooltipSystem& current, const bool success) {
            current.HandlePendingWorldGameObjectRefresh(generation, success);
          }),
  };

  WorldGameObjectTooltipRequirementState requirement_state;
  if ((game_object.GetFlags() & openwow::game::GO_FLAG_LOCKED) != 0u) {
    const auto requirement_color =
        dbc != nullptr ? ResolveLockedRequirementColor(
                             requirement_state =
                                  BuildWorldGameObjectTooltipRequirementState(
                                      game_object,
                                      session.inventory_replica(), *dbc, session.spells()))
                       : kTooltipRedArgb;
    AppendWorldGameObjectRequirementLine(
        tooltip,
        openwow::game::Localization::Get().GetString("LOCKED", "LOCKED"),
        requirement_color);
  }

  if (dbc == nullptr) {
    return;
  }

  const auto secondary_lock = ResolveWorldGameObjectTooltipSecondaryLock(lock_entry);
  if (!secondary_lock.has_value() ||
      !IsLockActionApplicable(game_object, secondary_lock->action)) {
    return;
  }

  if (!requirement_state.has_known_requirement &&
      (game_object.GetFlags() & openwow::game::GO_FLAG_LOCKED) != 0u) {
    requirement_state = BuildWorldGameObjectTooltipRequirementState(
        game_object, session.inventory_replica(), *dbc, session.spells());
  }

  switch (secondary_lock->type) {
  case 1u: {
    const auto item_entry = secondary_lock->index;
    const auto *const item_template =
        session.query_cache().GetOrRequestItemTemplate(item_entry, request_options);
    if (item_template == nullptr) {
      if (requested_async != nullptr) {
        *requested_async = true;
      }
      return;
    }

    auto &localization = openwow::game::Localization::Get();
    AppendWorldGameObjectRequirementLine(
        tooltip,
        localization.FormatString(
            localization.GetString("LOCKED_WITH_ITEM", "LOCKED_WITH_ITEM"),
            {item_template->name}),
        kTooltipWhiteArgb);
    return;
  }

  case 2u: {
    const auto *const lock_type = dbc->lock_type().LookupEntry(secondary_lock->index);
    if (lock_type == nullptr || lock_type->name.empty()) {
      return;
    }

    auto &localization = openwow::game::Localization::Get();
    if (requirement_state.has_open_lock_skill) {
      AppendWorldGameObjectRequirementLine(
          tooltip,
          localization.FormatString(
              localization.GetString("LOCKED_WITH_SPELL_KNOWN",
                                     "LOCKED_WITH_SPELL_KNOWN"),
              {std::string(lock_type->name)}),
          ResolveLockedRequirementColor(requirement_state));
      return;
    }

    if (!game_object.IsLocked()) {
      AppendWorldGameObjectRequirementLine(
          tooltip,
          localization.FormatString(
              localization.GetString("LOCKED_WITH_SPELL", "LOCKED_WITH_SPELL"),
              {std::string(lock_type->name)}),
          kTooltipYellowArgb);
    }
    return;
  }

  case 3u: {
    const auto *const spell = LookupSpellEntry(*dbc, secondary_lock->index);
    if (spell == nullptr) {
      return;
    }

    std::string reagent_list;
    bool first = true;
    for (std::size_t reagent_index = 0; reagent_index < spell->reagent.size();
         ++reagent_index) {
      const auto reagent_item_id = spell->reagent[reagent_index];
      const auto reagent_count = spell->reagent_count[reagent_index];
      if (reagent_item_id <= 0) {
        continue;
      }

      const auto *const item_template =
          session.query_cache().GetOrRequestItemTemplate(
              static_cast<std::uint32_t>(reagent_item_id), request_options);
      if (item_template == nullptr) {
        if (requested_async != nullptr) {
          *requested_async = true;
        }
        continue;
      }

      if (!first) {
        reagent_list += ", ";
      }
      first = false;

      std::string item_text = item_template->name;
      if (reagent_count > 1u) {
        item_text += " (" + std::to_string(reagent_count) + ")";
      }

      if (openwow::game::CountCarriedItemsOfEntry(
              session.inventory_replica(),
              static_cast<std::uint32_t>(reagent_item_id)) <
              reagent_count) {
        reagent_list += "|cffff2020";
        reagent_list += item_text;
        reagent_list += "|r";
      } else {
        reagent_list += item_text;
      }
    }

    if (reagent_list.empty()) {
      return;
    }

    auto &localization = openwow::game::Localization::Get();
    AppendWorldGameObjectRequirementLine(
        tooltip,
        localization.FormatString(
            localization.GetString("LOCKED_WITH_SPELL", "LOCKED_WITH_SPELL"),
            {reagent_list}),
        kTooltipWhiteArgb, true);
    return;
  }

  default:
    return;
  }
}

bool IsQuestComplete(const openwow::game::WorldSession &session,
                     const std::uint32_t quest_id) {
  const auto *const quest_entry = session.quests().FindQuestLogEntry(quest_id);
  return quest_entry != nullptr &&
         (quest_entry->status == openwow::game::QuestStatus::kComplete ||
          quest_entry->status == openwow::game::QuestStatus::kRewarded);
}

std::string BuildQuestObjectProgressText(const openwow::game::QuestTemplate &quest_template,
                                         const openwow::game::QuestLogEntry &quest_entry,
                                         const int objective_index) {
  const auto &objective = quest_template.npc_or_go_objectives[objective_index];
  auto label = objective.text;
  if (label.empty()) {
    label = " ";
  }

  auto &localization = openwow::game::Localization::Get();
  return localization.FormatString(
      localization.GetString("QUEST_OBJECTS_FOUND", "QUEST_OBJECTS_FOUND"),
      {label, std::to_string(quest_entry.kill_counts[objective_index]),
       std::to_string(objective.required_count)});
}

std::string BuildQuestItemProgressText(openwow::game::WorldSession &session,
                                       const openwow::game::QuestTemplate &quest_template,
                                       const int item_objective_index,
                                       const std::uint32_t generation,
                                       bool *const requested_async,
                                       openwow::ui::game::TooltipSystem &tooltip) {
  const auto &objective = quest_template.item_objectives[item_objective_index];
  const auto request_options = openwow::game::AsyncQueryChannel::RequestOptions{
      .callback_key = BuildTooltipWorldGameObjectCallbackKey(generation),
      .dedupe_callbacks = true,
      .callback = tooltip.BindAsyncCallback(
          [generation](openwow::ui::game::TooltipSystem& current, const bool success) {
            current.HandlePendingWorldGameObjectRefresh(generation, success);
          }),
  };

  const auto *const item_template =
      session.query_cache().GetOrRequestItemTemplate(objective.item_id,
                                                     request_options);
  if (item_template == nullptr) {
    if (requested_async != nullptr) {
      *requested_async = true;
    }
    return {};
  }

  const auto item_count =
      session.inventory_replica().GetItemCount(objective.item_id);
  const auto progress = std::min(item_count, objective.required_count);
  auto &localization = openwow::game::Localization::Get();
  return localization.FormatString(
      localization.GetString("QUEST_ITEMS_NEEDED", "QUEST_ITEMS_NEEDED"),
      {item_template->name, std::to_string(progress),
       std::to_string(objective.required_count)});
}

void AppendWorldGameObjectQuestLines(
    openwow::ui::game::TooltipSystem &tooltip,
    const openwow::game::CGGameObject_C &game_object,
    const openwow::game::GameObjectTemplateInfo *const template_info,
    openwow::game::WorldSession &session, const std::uint32_t generation,
    bool *const requested_async) {
  if (!openwow::ui::game::CVarSystem::Instance().GetCVarBool(
          "showQuestTrackingTooltips")) {
    return;
  }

  for (const auto &quest_entry : session.quests().quest_log()) {
    if (quest_entry.quest_id == 0u) {
      continue;
    }

    const auto request_options = openwow::game::QuestManager::QueryRequestOptions{
        .callback_key = BuildTooltipWorldGameObjectCallbackKey(generation),
        .dedupe_callbacks = true,
        .callback = tooltip.BindAsyncCallback(
            [generation](openwow::ui::game::TooltipSystem& current, const bool success) {
              current.HandlePendingWorldGameObjectRefresh(generation, success);
            }),
    };

    const auto *const quest_template =
        session.quests().GetOrRequestTemplate(quest_entry.quest_id,
                                              request_options);
    if (quest_template == nullptr) {
      if (requested_async != nullptr) {
        *requested_async = true;
      }
      continue;
    }

    bool added_quest_title = false;
    for (int objective_index = 0;
         objective_index < openwow::game::kQuestObjectivesCount; ++objective_index) {
      if (quest_template->npc_or_go_objectives[objective_index].creature_or_go !=
          -static_cast<std::int32_t>(game_object.GetEntry())) {
        continue;
      }

      if (!added_quest_title) {
        AppendWorldGameObjectRequirementLine(tooltip, quest_template->title,
                                             kTooltipGoldArgb);
        added_quest_title = true;
      }

      AppendWorldGameObjectRequirementLine(
          tooltip,
          " - " + BuildQuestObjectProgressText(*quest_template, quest_entry,
                                               objective_index),
          kTooltipWhiteArgb);
    }

    if (template_info == nullptr) {
      continue;
    }

    for (const auto quest_item_id : template_info->quest_items) {
      if (quest_item_id == 0u) {
        continue;
      }

      for (int item_objective_index = 0;
           item_objective_index < openwow::game::kQuestItemObjectivesCount;
           ++item_objective_index) {
        const auto &item_objective =
            quest_template->item_objectives[item_objective_index];
        if (item_objective.item_id != quest_item_id ||
            item_objective.required_count == 0u) {
          continue;
        }

        const auto line_text = BuildQuestItemProgressText(
            session, *quest_template, item_objective_index, generation,
            requested_async, tooltip);
        if (line_text.empty()) {
          continue;
        }

        if (!added_quest_title) {
          AppendWorldGameObjectRequirementLine(tooltip, quest_template->title,
                                               kTooltipGoldArgb);
          added_quest_title = true;
        }

        AppendWorldGameObjectRequirementLine(tooltip, " - " + line_text,
                                             kTooltipWhiteArgb);
      }

      if (IsQuestComplete(session, quest_entry.quest_id)) {
        continue;
      }

      for (const auto &drop_objective : quest_template->item_drop_objectives) {
        if (drop_objective.item_id != quest_item_id || drop_objective.item_id == 0u) {
          continue;
        }

        const auto item_request_options = openwow::game::AsyncQueryChannel::RequestOptions{
            .callback_key = BuildTooltipWorldGameObjectCallbackKey(generation),
            .dedupe_callbacks = true,
            .callback = tooltip.BindAsyncCallback(
                [generation](openwow::ui::game::TooltipSystem& current, const bool success) {
                  current.HandlePendingWorldGameObjectRefresh(generation, success);
                }),
        };
        const auto *const item_template =
            session.query_cache().GetOrRequestItemTemplate(
                drop_objective.item_id, item_request_options);
        if (item_template == nullptr) {
          if (requested_async != nullptr) {
            *requested_async = true;
          }
          continue;
        }

        if (!added_quest_title) {
          AppendWorldGameObjectRequirementLine(tooltip, quest_template->title,
                                               kTooltipGoldArgb);
          added_quest_title = true;
        }

        AppendWorldGameObjectRequirementLine(
            tooltip, " - " + item_template->name, kTooltipWhiteArgb);
      }
    }
  }
}

const openwow::game::ItemTemplate *ResolveTooltipItemTemplate(
    openwow::game::ItemDefinitions& item_definitions, std::uint32_t item_id,
    const openwow::game::QueryCache::QueryRequestOptions *request_options,
    bool *requested_async, openwow::game::WorldSession* session);

const openwow::game::ItemTemplate *ResolveTooltipItemTemplate(
    openwow::game::ItemDefinitions& item_definitions, const std::uint32_t item_id,
    const openwow::game::QueryCache::QueryRequestOptions *request_options,
    bool *requested_async, openwow::game::WorldSession* session) {
  if (requested_async != nullptr) {
    *requested_async = false;
  }

  if (const auto *item = item_definitions.GetItem(item_id); item != nullptr) {
    return item;
  }

  if (session == nullptr) {
    return nullptr;
  }

  const auto *item_template =
      request_options != nullptr
          ? session->query_cache().GetOrRequestItemTemplate(
                item_id, *request_options)
          : session->query_cache().GetOrRequestItemTemplate(item_id);
  if (item_template == nullptr) {
    if (requested_async != nullptr && request_options != nullptr) {
      *requested_async = true;
    }
    return nullptr;
  }

  item_definitions.CacheItem(*item_template);
  return item_definitions.GetItem(item_id);
}

}

namespace openwow::ui::game {

namespace {
thread_local TooltipSystem* active_tooltip_system = nullptr;

TooltipSystem& DefaultTooltipSystem() {
  static TooltipSystem instance;
  return instance;
}
}

int CompareTooltipFrameStackEntry(const TooltipFrameStackEntry &left,
                                  const TooltipFrameStackEntry &right) {
  if (left.strata < right.strata) {
    return 1;
  }
  if (left.strata > right.strata) {
    return -1;
  }
  if (left.level < right.level) {
    return 1;
  }
  if (left.level > right.level) {
    return -1;
  }
  return openwow::core::SStrCmpNoCase(left.label.c_str(), right.label.c_str(), 0x7FFFFFFFu);
}

TooltipSystem &TooltipSystem::Get() {
  return active_tooltip_system != nullptr ? *active_tooltip_system
                                          : DefaultTooltipSystem();
}

void TooltipSystem::ResetDefault() {
  DefaultTooltipSystem().Reset();
}

TooltipSystem::ScopedActivation::ScopedActivation(
    TooltipSystem& tooltip) noexcept
    : previous_(active_tooltip_system) {
  active_tooltip_system = &tooltip;
}

TooltipSystem::ScopedActivation::~ScopedActivation() {
  active_tooltip_system = previous_;
}

TooltipSystem::TooltipSystem()
    : lifetime_(std::make_shared<Lifetime>(Lifetime{.owner = this})) {}

TooltipSystem::~TooltipSystem() {
  lifetime_->owner = nullptr;
  lifetime_.reset();
  content_changed_callback_ = {};
  ResetPendingItemTemplateRefresh();
  ResetPendingWorldGameObjectRefresh();
}

void TooltipSystem::SetContentChangedCallback(
    std::function<void()> callback) {
  content_changed_callback_ = std::move(callback);
}

void TooltipSystem::NotifyContentChanged() {
  if (!content_changed_callback_) {
    return;
  }
  ScopedActivation activation(*this);
  content_changed_callback_();
}

std::function<void(bool)> TooltipSystem::BindAsyncCallback(
    std::function<void(TooltipSystem&, bool)> callback) {
  std::weak_ptr<Lifetime> lifetime = lifetime_;
  return [lifetime, callback = std::move(callback)](const bool success) {
    const auto locked = lifetime.lock();
    if (locked == nullptr || locked->owner == nullptr) {
      return;
    }
    callback(*locked->owner, success);
  };
}

void TooltipSystem::BindDbcLoader(const openwow::data::dbc::DbcLoader *dbc) {
  dbc_ = dbc;
}

void TooltipSystem::BindWorldSession(openwow::game::WorldSession* session) {
  world_session_ = session;
}

void TooltipSystem::SetOwner(const std::string &frameName, const std::string &anchor) {
  Hide();
  ClearLines();
  owner_ = frameName;
  anchor_ = frameName.empty() ? "ANCHOR_NONE" : anchor;
  MarkPresentationChanged();
}

void TooltipSystem::SetAnchor(const std::string& anchor) {
  anchor_ = anchor;
  MarkPresentationChanged();
}

void TooltipSystem::ClearLines() {
  ++clear_generation_;
  MarkPresentationChanged();
  ResetPendingItemTemplateRefresh();
  ResetPendingWorldGameObjectRefresh();
  lines_.clear();
  item_id_ = 0;
  item_guid_ = 0;
  item_name_.clear();
  item_link_.clear();
  spell_id_ = 0;
  secondary_spell_id_ = 0;
  pending_spell_async_count_ = 0;
  unit_guid_ = 0;
  quest_id_ = 0;
  achievement_id_ = 0;
  achievement_player_guid_ = 0;
  achievement_completed_ = 0;
  achievement_criteria_data_.fill(0);
  achievement_criteria_bitmask_.fill(0);
  world_game_object_guid_.reset();
  world_game_object_rebuild_requested_ = false;
  has_status_bar_ = false;
  status_bar_min_ = 0.0f;
  status_bar_max_ = 1.0f;
  status_bar_value_ = 0.0f;
  pending_money_script_.reset();

  if (!force_min_width_) {
    min_width_ = 0.0f;
  }
  textures_.clear();
}

void TooltipSystem::AddLine(const std::string &text, float r, float g, float b, bool wrap) {
  if (text.empty()) {
    return;
  }

  TooltipLine line;
  line.left_text = ExpandSimpleRenderTooltipText(text);
  line.left_r = QuantizeTooltipColorChannel(r);
  line.left_g = QuantizeTooltipColorChannel(g);
  line.left_b = QuantizeTooltipColorChannel(b);
  line.wrap = wrap;
  lines_.push_back(std::move(line));
  MarkPresentationChanged();
}

void TooltipSystem::AddDoubleLine(const std::string &leftText, const std::string &rightText,
                                  float leftR, float leftG, float leftB, float rightR, float rightG,
                                  float rightB, bool wrap) {
  if (leftText.empty() && rightText.empty()) {
    return;
  }

  TooltipLine line;
  line.left_text = ExpandSimpleRenderTooltipText(leftText);
  line.right_text = ExpandSimpleRenderTooltipText(rightText);
  line.left_r = QuantizeTooltipColorChannel(leftR);
  line.left_g = QuantizeTooltipColorChannel(leftG);
  line.left_b = QuantizeTooltipColorChannel(leftB);
  line.right_r = QuantizeTooltipColorChannel(rightR);
  line.right_g = QuantizeTooltipColorChannel(rightG);
  line.right_b = QuantizeTooltipColorChannel(rightB);
  line.wrap = wrap;
  lines_.push_back(std::move(line));
  MarkPresentationChanged();
}

void TooltipSystem::AppendToFirstLine(const std::string &text) {
  if (lines_.empty()) {
    return;
  }

  lines_.front().left_text = ExpandSimpleRenderTooltipText(lines_.front().left_text + text);
  MarkPresentationChanged();
}

void TooltipSystem::AddMoneyLine(const std::string &prefix, const std::uint32_t money,
                                 const std::string &suffix) {
  AddLine(FormatMoneyText(prefix, money, suffix), 1.0f, 1.0f, 1.0f);
}

void TooltipSystem::ClearTextures() {
  if (!textures_.empty()) {
    MarkPresentationChanged();
  }
  textures_.clear();
}

void TooltipSystem::AddTexture(const std::string &filename, const float *tex_coords,
                               std::uint32_t vertex_color) {

  if (filename.empty() || textures_.size() >= kMaxTooltipTextures ||
      lines_.size() <= 1u)
    return;

  TooltipTextureData td;
  td.filename = filename;
  td.vertex_color = vertex_color;
  td.line_index = static_cast<std::uint32_t>(lines_.size() - 1u);
  if (tex_coords != nullptr) {
    td.tex_coords[0] = tex_coords[0];
    td.tex_coords[1] = tex_coords[1];
    td.tex_coords[2] = tex_coords[2];
    td.tex_coords[3] = tex_coords[3];
  } else {
    td.tex_coords[0] = 0.0f;
    td.tex_coords[1] = 0.0f;
    td.tex_coords[2] = 1.0f;
    td.tex_coords[3] = 1.0f;
  }
  textures_.push_back(std::move(td));
  MarkPresentationChanged();
}

const std::vector<TooltipTextureData> &TooltipSystem::GetTextures() const {
  return textures_;
}

void TooltipSystem::Show() {
  ClearFadeState();
  if (shown_) {
    return;
  }
  shown_ = true;
  MarkPresentationChanged();
}

void TooltipSystem::FadeOut() {
  if (anchor_ == "ANCHOR_CURSOR" || anchor_ == "ANCHOR_CURSOR_RIGHT") {
    Hide();
    fade_active_ = false;
    fade_timer_ = 0.0f;
  } else {
    fade_active_ = true;
    fade_timer_ = 2.0f;
  }
}

bool TooltipSystem::IsFading() const {
  return fade_active_;
}

float TooltipSystem::GetFadeTimer() const {
  return fade_timer_;
}

bool TooltipSystem::AdvanceFade(const float elapsed_seconds) {
  if (!fade_active_) {
    return false;
  }

  fade_timer_ -= std::max(0.0F, elapsed_seconds);
  if (fade_timer_ > 0.0F) {
    return false;
  }

  const bool was_shown = shown_;
  Hide();
  return was_shown;
}

void TooltipSystem::ClearFadeState() {
  fade_active_ = false;
  fade_timer_ = 0.0f;
}

void TooltipSystem::Hide() {
  const bool had_world_game_object_tooltip =
      world_game_object_guid_.has_value() || has_status_bar_;
  ClearFadeState();
  if (shown_) {
    shown_ = false;
    MarkPresentationChanged();
  }

  if (had_world_game_object_tooltip) {
    HideLiveWorldGameObjectTooltipFrame();
  }
}

void TooltipSystem::Reset() {
  ++clear_generation_;
  Hide();
  ResetPendingItemTemplateRefresh();
  ResetPendingWorldGameObjectRefresh();
  owner_.clear();
  anchor_ = "ANCHOR_NONE";
  lines_.clear();
  item_id_ = 0;
  item_guid_ = 0;
  item_name_.clear();
  item_link_.clear();
  spell_id_ = 0;
  secondary_spell_id_ = 0;
  pending_spell_async_count_ = 0;
  unit_guid_ = 0;
  quest_id_ = 0;
  achievement_id_ = 0;
  achievement_player_guid_ = 0;
  achievement_completed_ = 0;
  achievement_criteria_data_.fill(0);
  achievement_criteria_bitmask_.fill(0);
  world_game_object_guid_.reset();
  world_game_object_rebuild_requested_ = false;
  has_status_bar_ = false;
  status_bar_min_ = 0.0f;
  status_bar_max_ = 1.0f;
  status_bar_value_ = 0.0f;
  pending_money_script_.reset();
  min_width_ = 0.0f;
  force_min_width_ = false;
  padding_ = 0.0f;
  textures_.clear();
}

void TooltipSystem::ResetPendingItemTemplateRefresh() {
  if (!pending_item_template_refresh_.has_value()) {
    return;
  }

  if (world_session_ != nullptr) {
    world_session_->query_cache().CancelItemTemplateCallbacks(
        BuildTooltipItemTemplateCallbackKey(
            pending_item_template_refresh_->callback_cookie));
  }

  pending_item_template_refresh_.reset();
}

void TooltipSystem::BeginPendingItemTemplateRefresh(const std::uint32_t item_id,
                                                    const std::int32_t random_property_id,
                                                    const std::uint32_t suffix_factor,
                                                    const std::uint32_t player_level,
                                                    const std::uint64_t item_guid,
                                                    const openwow::ui::TooltipItemInstanceData* instance_data,
                                                    const TooltipItemDisplayOptions display_options,
                                                    const std::uint32_t callback_cookie) {
  ResetPendingItemTemplateRefresh();

  pending_item_template_refresh_ = PendingItemTemplateRefresh{
      .item_id = item_id,
      .random_property_id = random_property_id,
      .suffix_factor = suffix_factor,
      .player_level = player_level,
      .item_guid = item_guid,
      .has_instance_data = instance_data != nullptr,
      .instance_data = instance_data != nullptr ? *instance_data : openwow::ui::TooltipItemInstanceData{},
      .display_options = display_options,
      .generation = next_pending_item_template_generation_++,
      .callback_cookie = callback_cookie,
  };
}

void TooltipSystem::HandlePendingItemTemplateRefresh(const std::uint32_t generation,
                                                     const bool success) {
  if (!success || !pending_item_template_refresh_.has_value() ||
      pending_item_template_refresh_->generation != generation) {
    return;
  }

  const PendingItemTemplateRefresh pending = *pending_item_template_refresh_;
  pending_item_template_refresh_.reset();
  (void)SetItemInternal(pending.item_id, pending.random_property_id, pending.suffix_factor,
                        pending.player_level, pending.item_guid,
                        pending.has_instance_data ? &pending.instance_data : nullptr,
                        pending.display_options);
  NotifyContentChanged();
}

void TooltipSystem::ResetPendingWorldGameObjectRefresh() {
  pending_world_game_object_refresh_.reset();
}

void TooltipSystem::HandlePendingWorldGameObjectRefresh(
    const std::uint32_t generation, const bool success) {
  if (!success || !pending_world_game_object_refresh_.has_value() ||
      pending_world_game_object_refresh_->generation != generation) {
    return;
  }

  auto* const session = world_session_;
  if (session == nullptr) {
    return;
  }

  const auto guid =
      openwow::game::ObjectGuid(pending_world_game_object_refresh_->guid);
  if (world_game_object_guid_ != guid.GetRawValue()) {
    return;
  }

  const auto *const game_object = session->objects().GetGameObject(guid);
  if (game_object == nullptr) {
    return;
  }

  pending_world_game_object_refresh_.reset();
  world_game_object_rebuild_requested_ = true;
  SetWorldGameObject(*game_object);
}

void TooltipSystem::SetItemById(std::uint32_t itemId) {
  SetItemFromLoot(itemId, 0, 0);
}

void TooltipSystem::SetItemByLink(const std::string &link) {
  openwow::game::HyperlinkInfo info;
  if (openwow::game::HyperlinkParser::Parse(link, info) && info.type == "item" && info.id != 0) {
    std::int32_t random_property_id = 0;
    std::uint32_t suffix_factor = 0;
    std::uint32_t player_level = 0;
    if (info.params.size() >= 7) {
      random_property_id = std::atoi(info.params[5].c_str());
      suffix_factor = static_cast<std::uint32_t>(std::strtoul(info.params[6].c_str(), nullptr, 10));
    }
    if (info.params.size() >= 8) {
      player_level = static_cast<std::uint32_t>(
          std::strtoul(info.params[7].c_str(), nullptr, 10));
    }
    SetItemFromLoot(info.id, random_property_id, suffix_factor, player_level);
    if (!info.display_text.empty()) {
      item_name_ = info.display_text;
      item_link_ = link;
      if (!pending_item_template_refresh_.has_value() && !lines_.empty()) {
        lines_.front().left_text = info.display_text;
      }
    }
    return;
  }

  ClearLines();
  AddLine(link, 1.0f, 1.0f, 1.0f);
  Show();
}

void TooltipSystem::SetItemFromLoot(std::uint32_t itemId, std::int32_t randomPropertyId,
                                    std::uint32_t suffixFactor, std::uint32_t playerLevel,
                                    const std::uint64_t itemGuid,
                                    const TooltipItemDisplayOptions options) {
  (void)SetItemInternal(itemId, randomPropertyId, suffixFactor, playerLevel, itemGuid, nullptr,
                        options);
}

bool TooltipSystem::SetItemWithInstanceData(
    std::uint32_t itemId,
    std::int32_t randomPropertyId,
    std::uint32_t suffixFactor,
    const openwow::ui::TooltipItemInstanceData& instanceData,
    std::uint32_t playerLevel,
    std::uint64_t itemGuid,
    const TooltipItemDisplayOptions options) {
  return SetItemInternal(itemId, randomPropertyId, suffixFactor, playerLevel, itemGuid,
                         &instanceData, options);
}

bool TooltipSystem::SetItemInternal(std::uint32_t itemId, std::int32_t randomPropertyId,
                                    std::uint32_t suffixFactor, std::uint32_t playerLevel,
                                    const std::uint64_t itemGuid,
                                    const openwow::ui::TooltipItemInstanceData* instance_data,
                                    const TooltipItemDisplayOptions display_options) {
  ClearLines();

  item_id_ = itemId;
  item_guid_ = itemGuid;
  const auto *session = world_session_;
  const std::uint32_t pending_generation = next_pending_item_template_generation_;
  const std::uint32_t callback_cookie = AllocateTooltipCallbackCookie();
  const openwow::game::QueryCache::QueryRequestOptions request_options{
      .callback_key = BuildTooltipItemTemplateCallbackKey(callback_cookie),
      .dedupe_callbacks = true,
       .callback = BindAsyncCallback(
           [pending_generation](TooltipSystem& current, const bool success) {
             current.HandlePendingItemTemplateRefresh(pending_generation, success);
           }),
  };
  bool requested_async = false;
  if (item_definitions_ == nullptr) {
    return false;
  }
  const auto *item = ResolveTooltipItemTemplate(
      *item_definitions_, itemId, &request_options, &requested_async,
      world_session_);
  openwow::game::ItemTemplate object_item_template;
  if (session != nullptr && itemGuid != 0u) {
    if (const auto *object = session->objects().GetItem(openwow::game::ObjectGuid(itemGuid));
        object != nullptr && object->GetEntry() == itemId) {
      if (const auto *info = object->GetOrRequestQueryItemTemplate(); info != nullptr) {
        object_item_template = *info;
        for (std::size_t index = 0; index < object_item_template.spells.size(); ++index) {
          object_item_template.spells[index].charges = object->GetSpellCharges(
              static_cast<std::uint8_t>(index));
        }
        item = &object_item_template;
      }
    }
  }
  item_name_ = detail::ResolveLootItemDisplayName(
      dbc_, item_definitions_ != nullptr
                ? detail::ResolveLootItemBaseName(*item_definitions_, itemId)
                : std::string{},
                                                  randomPropertyId);
  const auto fallback_quality =
      item != nullptr ? static_cast<std::uint8_t>(item->quality) : 1;
  const auto quality =
      item_definitions_ != nullptr
          ? detail::ResolveLootItemQuality(
                *item_definitions_, itemId, fallback_quality)
          : fallback_quality;
  std::uint32_t effective_player_level = playerLevel;
  std::uint32_t player_class_mask = 0xFFFFu;
  std::uint32_t player_race_mask = 0xFFFFu;
  if (session != nullptr) {
    if (const auto *player = session->objects().GetActivePlayer(); player != nullptr) {
      if (effective_player_level == 0u) {
        effective_player_level = player->State().GetLevel();
      }
      const auto player_class = player->State().GetClass();
      if (player_class != 0u && player_class <= 32u) {
        player_class_mask = 1u << (player_class - 1u);
      }
      const auto player_race = player->State().GetRace();
      if (player_race != 0u && player_race <= 32u) {
        player_race_mask = 1u << (player_race - 1u);
      }
    }
  }
  item_link_ = detail::BuildLootItemLink(itemId, quality, item_name_, randomPropertyId,
                                         suffixFactor, effective_player_level);
  if (instance_data != nullptr) {
    item_link_ = openwow::game::HyperlinkParser::Build(
        "item", itemId, item_name_,
        openwow::game::HyperlinkParser::GetQualityColor(quality),
        {
            std::to_string(instance_data->permanent_enchant_id),
            std::to_string(instance_data->gem_item_ids[0]),
            std::to_string(instance_data->gem_item_ids[1]),
            std::to_string(instance_data->gem_item_ids[2]),
            "0",
            std::to_string(randomPropertyId),
            std::to_string(suffixFactor),
            std::to_string(effective_player_level),
        });
  }

  bool destroying_socket_gem = false;
  if (display_options.socket_to_destroy) {
    destroying_socket_gem =
        item != nullptr && item->required_disenchant_skill != 0u;
    const char* const token =
        destroying_socket_gem ? "DESTROY_GEM" : "CURRENTLY_EQUIPPED";
    const std::string marker =
        openwow::game::Localization::Get().GetString(token, token);
    if (destroying_socket_gem) {
      AddLine(marker, 1.0f, 0.1f, 0.1f);
    } else {
      AddLine(marker, 1.0f, 1.0f, 1.0f);
    }
  }

  if (item != nullptr && display_options.name_only) {

    AddLine(item_name_, 1.0f, 1.0f, 1.0f);
  } else if (item != nullptr) {
    const std::size_t title_line_index = lines_.size();
    auto lines = openwow::ui::TooltipBuilder::BuildItemTooltip(
        *item, *item_definitions_, inventory_, effective_player_level,
        player_class_mask, player_race_mask, dbc_, effective_player_level,
        instance_data, world_session_);
    if (!lines.empty()) {
      lines.front().text = item_name_;
    }
    for (const auto &line : lines) {
      AppendBuilderLine(*this, line);
    }
    if (destroying_socket_gem && title_line_index < lines_.size()) {

      lines_[title_line_index].left_r = 1.0f;
      lines_[title_line_index].left_g = 1.0f;
      lines_[title_line_index].left_b = 1.0f;
    }
    if (item->sell_price != 0u) {
      QueueMoneyScript(item->sell_price);
    }
  } else if (requested_async) {
    BeginPendingItemTemplateRefresh(itemId, randomPropertyId, suffixFactor, playerLevel, itemGuid,
                                    instance_data, display_options,
                                    callback_cookie);
    AddLine(openwow::game::Localization::Get().GetString("RETRIEVING_ITEM_INFO",
                                                         "RETRIEVING_ITEM_INFO"),
            kPendingItemInfoColor, kPendingItemInfoColor, kPendingItemInfoColor);
  } else {
    AddLine(item_name_, 1.0f, 1.0f, 1.0f);
  }

  if (!display_options.name_only) {
    AppendEquipmentSetMembershipLine(*this, equipment_, itemGuid);
    AppendMerchantRequirementLines(*this, itemId, dbc_);
  }
  Show();
  return item != nullptr;
}

void TooltipSystem::QueueMoneyScript(const std::uint32_t cost,
                                     std::optional<std::uint32_t> max_cost) {
  pending_money_script_ = TooltipMoneyScriptArgs{cost, max_cost};
}

std::optional<TooltipMoneyScriptArgs> TooltipSystem::TakePendingMoneyScript() {
  auto pending = pending_money_script_;
  pending_money_script_.reset();
  return pending;
}

void TooltipSystem::OverrideItemIdentity(std::string itemName, std::string itemLink) {
  item_name_ = std::move(itemName);
  item_link_ = std::move(itemLink);
  if (!item_name_.empty() && !lines_.empty()) {
    lines_.front().left_text = item_name_;
  }
}

bool TooltipSystem::SetSpellById(const std::uint32_t spellId,
                                 const bool showRank,
                                 const bool isPet) {
  ScopedActivation activation(*this);
  if (spellId == 0u ||
      spellId > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    ClearLines();
    return false;
  }

  int cooldown_remaining = 0;
  if (const auto *session = world_session_;
      session != nullptr) {
    const auto cooldown =
        isPet ? openwow::game::ResolvePetBarSpellCooldown(
                    session->pet().pet_bar(), spellId,
                    session->GetDbcLoader())
              : openwow::game::ResolveSpellbookCooldown(
                    session->spell_book(), spellId);
    if (cooldown.has_value()) {
      cooldown_remaining =
          openwow::game::RemainingCooldownMilliseconds(*cooldown);
    }
  }

  return BuildSpellTooltip({
      .tooltip = *this,
      .spell_id = spellId,
      .cooldown_remaining = std::chrono::milliseconds(cooldown_remaining),
      .show_rank = showRank,
  });
}

void TooltipSystem::SetSpellId(const std::uint32_t spellId) {
  spell_id_ = spellId;
}

void TooltipSystem::SetSecondarySpellId(const std::uint32_t spellId) {
  secondary_spell_id_ = spellId;
}

bool TooltipSystem::SetUnit(const std::string &unitToken, bool hideStatus) {
  ClearLines();
  const auto *objects = GetObjectManager();
  if (objects == nullptr) {
    unit_guid_ = 0;
    return false;
  }

  const auto guid = ResolveTooltipUnitGuid(*objects, unitToken);
  unit_guid_ = guid.GetRawValue();

  constexpr std::uint64_t kInvalidGuid = static_cast<std::uint64_t>(-2);
  if (unit_guid_ == 0 || unit_guid_ == kInvalidGuid) {
    unit_guid_ = 0;
    return false;
  }

  const auto *unit = objects->GetUnit(guid);
  if (unit == nullptr) {
    unit_guid_ = 0;
    return false;
  }

  const auto *dbc = dbc_;
  if (dbc == nullptr) {
    if (const auto *session = world_session_;
        session != nullptr) {
      dbc = session->GetDbcLoader();
    }
  }

  return BuildUnitTooltipForUnit(*this, *unit, objects, dbc, hideStatus);
}

void TooltipSystem::SetWorldGameObject(const openwow::game::CGGameObject_C &game_object) {
  if (world_game_object_guid_ == game_object.GetGuid().GetRawValue() &&
      !world_game_object_rebuild_requested_ && shown_) {
    if (game_object.GetGoType() == static_cast<openwow::game::GameObjectType>(
                                        kTooltipStatusBarGoType)) {
      has_status_bar_ = true;
      status_bar_min_ = 0.0f;
      status_bar_max_ = 1.0f;
      status_bar_value_ = ClampStatusBarValue(
          status_bar_min_, status_bar_max_,
          static_cast<float>(game_object.GetGoAnimProgress()) / 255.0f);
      SyncLiveWorldGameObjectStatusBar(TooltipStatusBarSnapshot{
          .shown = has_status_bar_,
          .min = status_bar_min_,
          .max = status_bar_max_,
          .value = status_bar_value_,
      });
    }

    PublishToLiveGameTooltipFrame();
    return;
  }

  auto* const session = world_session_;
  const auto *const dbc = session != nullptr ? session->GetDbcLoader() : dbc_;
  const auto generation = next_pending_world_game_object_generation_++;
  bool requested_async = false;

  ResetPendingItemTemplateRefresh();
  ResetPendingWorldGameObjectRefresh();
  lines_.clear();
  shown_ = false;
  item_id_ = 0;
  item_guid_ = 0;
  item_name_.clear();
  item_link_.clear();
  spell_id_ = 0;
  secondary_spell_id_ = 0;
  pending_spell_async_count_ = 0;
  unit_guid_ = 0;
  pending_money_script_.reset();
  world_game_object_guid_ = game_object.GetGuid().GetRawValue();
  world_game_object_rebuild_requested_ = false;
  owner_ = "WorldFrame";
  anchor_ = "ANCHOR_TOPRIGHT";
  has_status_bar_ =
      game_object.GetGoType() ==
      static_cast<openwow::game::GameObjectType>(kTooltipStatusBarGoType);
  status_bar_min_ = 0.0f;
  status_bar_max_ = 1.0f;
  status_bar_value_ = ClampStatusBarValue(
      status_bar_min_, status_bar_max_,
      static_cast<float>(game_object.GetGoAnimProgress()) / 255.0f);

  const auto request_callback = BindAsyncCallback(
      [generation](TooltipSystem& current, const bool success) {
        current.HandlePendingWorldGameObjectRefresh(generation, success);
      });

  const auto request_options = openwow::game::AsyncQueryChannel::RequestOptions{
      .context = game_object.GetGuid().GetRawValue(),
      .callback_key = BuildTooltipWorldGameObjectCallbackKey(generation),
      .dedupe_callbacks = true,
      .callback = request_callback,
  };

  const openwow::game::GameObjectTemplateInfo *template_info =
      game_object.GetTemplateInfo();
  if (template_info == nullptr && session != nullptr) {
    template_info = session->query_cache().GetOrRequestGameObjectTemplate(
        game_object.GetEntry(), request_options);
    if (template_info == nullptr) {
      requested_async = true;
    }
  }

  AddArgbLine(*this, ResolveWorldGameObjectTooltipName(game_object, template_info),
              kTooltipGoldArgb);

  if (dbc != nullptr && session != nullptr) {
    AppendWorldGameObjectLockLines(*this, game_object, *session, generation,
                                   &requested_async);
    AppendWorldGameObjectQuestLines(*this, game_object, template_info, *session,
                                    generation, &requested_async);
  }

  if (dbc != nullptr && game_object.IsMeetingStone()) {
    const auto area_id = game_object.GetMeetingStoneAreaId();
    if (area_id != 0u) {
      if (const auto *area = dbc->area_table().LookupEntry(area_id);
          area != nullptr && !area->name.empty()) {
        AddLine(std::string(area->name), 1.0f, 1.0f, 1.0f);
      }
    }
  }

  Show();
  SyncLiveWorldGameObjectTooltipFrame(owner_, anchor_, lines_,
                                      TooltipStatusBarSnapshot{
                                          .shown = has_status_bar_,
                                          .min = status_bar_min_,
                                          .max = status_bar_max_,
                                          .value = status_bar_value_,
                                      });
  owner_ = "WorldFrame";
  anchor_ = "ANCHOR_TOPRIGHT";
  world_game_object_guid_ = game_object.GetGuid().GetRawValue();
  shown_ = true;
  has_status_bar_ =
      game_object.GetGoType() ==
      static_cast<openwow::game::GameObjectType>(kTooltipStatusBarGoType);
  status_bar_min_ = 0.0f;
  status_bar_max_ = 1.0f;
  status_bar_value_ = ClampStatusBarValue(
      status_bar_min_, status_bar_max_,
      static_cast<float>(game_object.GetGoAnimProgress()) / 255.0f);
  if (requested_async) {
    pending_world_game_object_refresh_ = PendingWorldGameObjectRefresh{
        .guid = game_object.GetGuid().GetRawValue(),
        .generation = generation,
    };
  }
}

void TooltipSystem::PublishToLiveGameTooltipFrame() {
  SyncLiveWorldGameObjectTooltipFrame(
      owner_, anchor_, lines_,
      TooltipStatusBarSnapshot{
          .shown = has_status_bar_,
          .min = status_bar_min_,
          .max = status_bar_max_,
          .value = status_bar_value_,
      });
}

void TooltipSystem::HideLiveGameTooltipFrame() {
  HideLiveWorldGameObjectTooltipFrame();
}

void TooltipSystem::SetFrameStack(const TooltipFrameStackSnapshot &snapshot) {
  ClearLines();

  char cursor_text[64];
  std::snprintf(cursor_text, sizeof(cursor_text), "(%.2f, %.2f)", snapshot.cursor_x,
                snapshot.cursor_y);
  AddDoubleLine(
      openwow::game::Localization::Get().GetString("DEBUG_FRAMESTACK", "DEBUG_FRAMESTACK"),
      cursor_text, 1.0f, 1.0f, 1.0f, kFrameStackHeaderRightColorR, kFrameStackHeaderRightColorG,
      kFrameStackHeaderRightColorB);

  std::vector<TooltipFrameStackEntry> entries = snapshot.entries;
  std::sort(entries.begin(), entries.end(),
            [](const TooltipFrameStackEntry &left, const TooltipFrameStackEntry &right) {
              return CompareTooltipFrameStackEntry(left, right) < 0;
            });

  int current_strata = 9;
  int current_level = 0;
  int alternating_level_group = 1;

  for (const auto &entry : entries) {
    if (entry.strata != current_strata) {
      AddLine(openwow::ui::ScriptFrameStrataToString(entry.strata), 1.0f, 1.0f, 1.0f);
      current_strata = entry.strata;
      current_level = entry.level;
      alternating_level_group = 0;
    } else if (entry.level != current_level) {
      alternating_level_group = (alternating_level_group + 1) % 2;
      current_level = entry.level;
    }

    char line_text[1088];
    std::snprintf(line_text, sizeof(line_text), " <%d> %s", entry.level, entry.label.c_str());

    if (!entry.visible) {
      AddArgbLine(*this, line_text, kFrameStackHiddenColors[alternating_level_group]);
      continue;
    }

    const auto *palette =
        entry.mouse_enabled ? kFrameStackVisibleMouseEnabledColors : kFrameStackVisibleColors;
    AddArgbLine(*this, line_text, palette[alternating_level_group]);
  }
}

const std::vector<TooltipLine> &TooltipSystem::GetLines() const {
  return lines_;
}

bool TooltipSystem::IsShown() const {
  return shown_;
}

bool TooltipSystem::HasOwner() const {
  return !owner_.empty();
}

std::string TooltipSystem::GetOwnerName() const {
  return owner_;
}

std::string TooltipSystem::GetAnchor() const {
  return anchor_;
}

int TooltipSystem::GetNumLines() const {
  return static_cast<int>(lines_.size());
}

std::uint64_t TooltipSystem::GetClearGeneration() const {
  return clear_generation_;
}

std::uint64_t TooltipSystem::GetPresentationRevision() const {
  return presentation_revision_;
}

void TooltipSystem::MarkPresentationChanged() noexcept {
  ++presentation_revision_;
}

void TooltipSystem::SetMinimumWidth(float w, bool force) {
  min_width_ = w;
  force_min_width_ = force;
  MarkPresentationChanged();
}

float TooltipSystem::GetMinimumWidth() const {
  return min_width_;
}

bool TooltipSystem::IsForceMinWidth() const {
  return force_min_width_;
}

void TooltipSystem::SetPadding(float p) {
  padding_ = p;
  MarkPresentationChanged();
}

float TooltipSystem::GetPadding() const {
  return padding_;
}

std::string TooltipSystem::GetLineText(int line) const {
  if (line < 1 || line > static_cast<int>(lines_.size()))
    return {};
  return lines_[static_cast<std::size_t>(line - 1)].left_text;
}

std::uint32_t TooltipSystem::GetItemId() const {
  return item_id_;
}

std::uint64_t TooltipSystem::GetItemGuid() const {
  return item_guid_;
}

std::string TooltipSystem::GetItemName() const {
  return item_name_;
}

std::string TooltipSystem::GetItemLink() const {
  return item_link_;
}

std::uint32_t TooltipSystem::GetSpellId() const {
  return spell_id_;
}

std::uint32_t TooltipSystem::GetSecondarySpellId() const {
  return secondary_spell_id_;
}

int TooltipSystem::GetPendingSpellAsyncCount() const {
  return pending_spell_async_count_;
}

void TooltipSystem::SetPendingSpellAsyncCount(int count) {
  pending_spell_async_count_ = count;
}

std::uint64_t TooltipSystem::GetUnitGuid() const {
  return unit_guid_;
}

void TooltipSystem::SetUnitGuid(std::uint64_t guid) {
  unit_guid_ = guid;
}

std::uint32_t TooltipSystem::GetQuestId() const {
  return quest_id_;
}

void TooltipSystem::SetQuestId(std::uint32_t questId) {
  quest_id_ = questId;
}

std::uint32_t TooltipSystem::GetAchievementId() const {
  return achievement_id_;
}

void TooltipSystem::SetAchievementId(std::uint32_t id) {
  achievement_id_ = id;
}

std::uint64_t TooltipSystem::GetAchievementPlayerGuid() const {
  return achievement_player_guid_;
}

void TooltipSystem::SetAchievementPlayerGuid(std::uint64_t guid) {
  achievement_player_guid_ = guid;
}

std::uint8_t TooltipSystem::GetAchievementCompleted() const {
  return achievement_completed_;
}

void TooltipSystem::SetAchievementCompleted(std::uint8_t completed) {
  achievement_completed_ = completed;
}

const std::array<std::uint32_t, 8>& TooltipSystem::GetAchievementCriteriaData() const {
  return achievement_criteria_data_;
}

void TooltipSystem::SetAchievementCriteriaData(const std::array<std::uint32_t, 8>& data) {
  achievement_criteria_data_ = data;
}

const std::array<std::uint32_t, 4>& TooltipSystem::GetAchievementCriteriaBitmask() const {
  return achievement_criteria_bitmask_;
}

void TooltipSystem::SetAchievementCriteriaBitmask(const std::array<std::uint32_t, 4>& mask) {
  achievement_criteria_bitmask_ = mask;
}

std::optional<std::uint64_t> TooltipSystem::GetWorldGameObjectGuid() const {
  return world_game_object_guid_;
}

bool TooltipSystem::HasStatusBar() const {
  return has_status_bar_;
}

float TooltipSystem::GetStatusBarMin() const {
  return status_bar_min_;
}

float TooltipSystem::GetStatusBarMax() const {
  return status_bar_max_;
}

float TooltipSystem::GetStatusBarValue() const {
  return status_bar_value_;
}

const openwow::data::dbc::DbcLoader *TooltipSystem::GetDbcLoader() const {
  return dbc_;
}

const openwow::game::ObjectManager *TooltipSystem::GetObjectManager() const {

  return world_session_ != nullptr ? &world_session_->objects() : nullptr;
}

openwow::game::WorldSession* TooltipSystem::GetWorldSession() const {
  return world_session_;
}

}
