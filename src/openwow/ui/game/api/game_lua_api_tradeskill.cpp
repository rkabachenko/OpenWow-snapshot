
#include "openwow/ui/game/api/game_lua_api_internal.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_string.h"
#include "openwow/core/storm_utils.h"
#include "openwow/game/profession_system.h"
#include "openwow/game/skill_line_ability_lookup.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spell_text_formatter.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/ui/game/api/game_lua_api_craft.h"
#include "openwow/ui/game/api/game_lua_api_tradeskill_state.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/tooltip_formatter.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/foundation/diagnostics/logging.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openwow::ui::game::detail {

struct TradeSkillReagent {
  std::int32_t item_id = 0;
  std::uint32_t count_needed = 0;
};

struct TradeSkillRecipe {
  std::int32_t spell_id = 0;
  std::string name;
  std::string icon_path;
  std::uint8_t difficulty = 0;
  std::uint32_t num_available = 0;
  std::int32_t max_repeat_count = -1;
  bool is_header = false;
  std::string header_name;
  std::int32_t item_id = 0;
  std::uint32_t product_link_type = 0;
  std::int32_t product_link_id = 0;
  std::string product_display_name;
  std::vector<TradeSkillReagent> reagents;
  std::uint32_t num_skill_ups = 1;
  std::int32_t item_class_id = -1;
  std::int32_t item_sub_class_id = -1;

  std::uint32_t inv_slot_mask = 0x04000000u;
  std::uint32_t spell_focus_object_id = 0;
  std::array<std::uint32_t, 2> required_item_entries{};
  std::array<std::uint32_t, 2> required_totem_category_ids{};
};

struct TradeSkillSubClass {
  std::int32_t item_class_id = 0;
  std::int32_t item_sub_class_id = 0;
  std::string name;
  std::string localized_name;
  std::uint32_t inv_slot_mask = 0;
};

struct TradeSkillData {
  bool open = false;
  std::string skill_name;
  std::uint32_t current_rank = 0;
  std::uint32_t max_rank = 0;
  std::uint32_t trade_skill_spell_id = 0;
  const ::openwow::data::dbc::DbcLoader *dbc = nullptr;
  ::openwow::game::WorldSession *session = nullptr;
  std::vector<TradeSkillRecipe> recipes;
  std::vector<TradeSkillRecipe> source_recipes;
  std::uint32_t pending_result_item_queries = 0;
  std::uint32_t selected_spell_id = 0;
  bool is_linked = false;
  std::optional<std::string> item_name_filter;
  bool only_show_makeable = false;
  bool only_show_skill_ups = false;

  std::vector<TradeSkillSubClass> sub_classes;
  std::uint32_t sub_class_filter_mask = 0xFFFFFFFFu;
  std::uint32_t header_expand_mask = 0xFFFFFFFFu;
  int selected_sub_class_index = -1;
  std::uint32_t supported_inv_slot_mask = 0;
  std::uint32_t inv_slot_filter_mask = 0xFFFFFFFFu;
  int item_level_filter_min = 0;
  int item_level_filter_max = 0;
  std::vector<std::size_t> ordered_recipe_indices;
  std::size_t visible_recipe_count = 0;
  bool list_view_dirty = true;
};

static TradeSkillData s_trade_skill;

static const char *DifficultyString(std::uint8_t d) {
  switch (d) {
  case 0:
    return "optimal";
  case 1:
    return "medium";
  case 2:
    return "easy";
  case 3:
    return "trivial";
  default:
    return "trivial";
  }
}

namespace {

constexpr std::uint32_t kTradeSkillResultItemType24 = 24;
constexpr std::uint32_t kTradeSkillResultItemType59 = 59;
constexpr std::uint32_t kTradeSkillResultItemType157 = 157;
constexpr std::uint32_t kTradeSkillEnchantLinkColor = 0xFFFFD000u;
constexpr std::uint32_t kTradeSkillAllMask = 0xFFFFFFFFu;
constexpr std::uint32_t kTradeSkillInvSlotNone = 0x04000000u;
constexpr std::uint32_t kTradeSkillInvSlotWeaponEnchant = 0x00800000u;
constexpr std::uint32_t kTradeSkillInvSlotTwoHandWeaponEnchant = 0x01000000u;
constexpr std::uint32_t kTradeSkillInvSlotShield = 0x02000000u;
constexpr std::uint8_t kTradeSkillCastFlags = 0;
constexpr std::uint64_t kTradeSkillCastTargetGuid = 0;
constexpr std::string_view kTradeSkillLinkAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::array<std::uint32_t, 29> kTradeSkillInvTypeMasks = {
    0x00000000u, 0x00000001u, 0x00000002u, 0x00000004u, 0x00000008u,
    0x00000010u, 0x00000020u, 0x00000040u, 0x00000080u, 0x00000100u,
    0x00000200u, 0x00000C00u, 0x00003000u, 0x00018000u, 0x00010000u,
    0x00020000u, 0x00004000u, 0x00018000u, 0x00780000u, 0x00040000u,
    0x00000010u, 0x00018000u, 0x00018000u, 0x00010000u, 0x00000000u,
    0x00020000u, 0x00020000u, 0x00000000u, 0x00020000u,
};
constexpr std::array<std::string_view, 27> kTradeSkillInvSlotGlobalKeys = {
    "HEADSLOT",     "NECKSLOT",          "SHOULDERSLOT", "SHIRTSLOT",       "CHESTSLOT",
    "WAISTSLOT",    "LEGSSLOT",          "FEETSLOT",     "WRISTSLOT",       "HANDSSLOT",
    "FINGER0SLOT",  "FINGER1SLOT",       "TRINKET0SLOT", "TRINKET1SLOT",    "BACKSLOT",
    "MAINHANDSLOT", "SECONDARYHANDSLOT", "RANGEDSLOT",   "TABARDSLOT",      "BAGSLOT",
    "BAGSLOT",      "BAGSLOT",           "BAGSLOT",      "ENCHSLOT_WEAPON", "ENCHSLOT_2HWEAPON",
    "SHIELDSLOT",   "NONEQUIPSLOT",
};

void MarkTradeSkillListViewDirty();
void SortTradeSkillSubClassesForClient();
int FindTradeSkillSubClassIndex(const TradeSkillRecipe &recipe);
void FireTradeSkillUpdateEvent();
void RebuildTradeSkillListOnPendingResultItemCompletion();

std::string GetTradeSkillGlobalString(lua_State *L, std::string_view key) {
  const std::string key_string(key);
  lua_getglobal(L, key_string.c_str());

  std::string value;
  if (lua_isstring(L, -1) != 0) {
    value = lua_tostring(L, -1);
  }
  lua_pop(L, 1);

  if (!value.empty()) {
    return value;
  }

  return ::openwow::game::Localization::Get().GetString(key_string, key_string);
}

void OnTradeSkillResultItemQueryResolved(bool success) {
  (void)success;
  RebuildTradeSkillListOnPendingResultItemCompletion();
}

::openwow::game::AsyncQueryChannel::CallbackKey TradeSkillResultItemQueryCallbackKey() {
  return ::openwow::game::AsyncQueryChannel::CallbackKey(
      reinterpret_cast<std::uintptr_t>(&OnTradeSkillResultItemQueryResolved), 0);
}

void CancelPendingTradeSkillResultItemQueries(::openwow::game::WorldSession *session) {
  if (session == nullptr || s_trade_skill.pending_result_item_queries == 0) {
    return;
  }

  session->query_cache().CancelItemTemplateCallbacks(TradeSkillResultItemQueryCallbackKey());
  s_trade_skill.pending_result_item_queries = 0;
}

void ResetTradeSkillRecipeListState() {
  s_trade_skill.dbc = nullptr;
  s_trade_skill.session = nullptr;
  s_trade_skill.recipes.clear();
  s_trade_skill.source_recipes.clear();
  s_trade_skill.pending_result_item_queries = 0;
  s_trade_skill.sub_classes.clear();
  s_trade_skill.supported_inv_slot_mask = 0;
  s_trade_skill.selected_sub_class_index = -1;
  s_trade_skill.ordered_recipe_indices.clear();
  s_trade_skill.visible_recipe_count = 0;
  s_trade_skill.selected_spell_id = 0;
  s_trade_skill.list_view_dirty = true;
}

void ResetTradeSkillFiltersForSkillChange() {
  s_trade_skill.item_name_filter.reset();
  s_trade_skill.only_show_makeable = false;
  s_trade_skill.sub_class_filter_mask = 0xFFFFFFFFu;
  s_trade_skill.header_expand_mask = 0xFFFFFFFFu;
  s_trade_skill.inv_slot_filter_mask = 0xFFFFFFFFu;
  s_trade_skill.item_level_filter_min = 0;
  s_trade_skill.item_level_filter_max = 0;
}

std::uint32_t CanonicalizeTradeSkillInvSlotMask(std::uint32_t mask) {
  if ((mask & 0x00080000u) != 0) {
    mask &= 0xFF8FFFFFu;
  } else if ((mask & 0x00000400u) != 0) {
    mask &= ~0x00000800u;
  } else if ((mask & 0x00001000u) != 0) {
    mask &= ~0x00002000u;
  }

  return mask != 0 ? mask : kTradeSkillInvSlotNone;
}

std::uint32_t ResolveTradeSkillInvSlotMaskFromInvType(const std::uint32_t inv_type) {
  if (inv_type >= kTradeSkillInvTypeMasks.size()) {
    return kTradeSkillInvSlotNone;
  }

  if (inv_type == 18u) {
    return 0x00080000u;
  }
  if (inv_type == 11u) {
    return 0x00000400u;
  }
  if (inv_type == 12u) {
    return 0x00001000u;
  }

  return CanonicalizeTradeSkillInvSlotMask(kTradeSkillInvTypeMasks[inv_type]);
}

template <typename Callback>
bool ForEachTradeSkillToolInventoryItem(
    const ::openwow::game::PlayerInventoryReplica& inventory, Callback&& callback) {
  return inventory.VisitDefaultPlayerItems(
      [&callback](const ::openwow::game::ItemInstance& item) {
        return callback(item);
      });
}

bool HasTradeSkillToolItemInInventory(
    const ::openwow::game::PlayerInventoryReplica& inventory,
    const std::uint32_t item_id) {
  if (item_id == 0) {
    return false;
  }

  bool found = false;
  ForEachTradeSkillToolInventoryItem(inventory, [&](const ::openwow::game::ItemInstance &item) {
    if (item.entry == item_id) {
      found = true;
      return false;
    }
    return true;
  });
  return found;
}

bool HasTradeSkillTotemCategoryInInventory(::openwow::game::WorldSession *session,
                                           const ::openwow::data::dbc::DbcLoader *dbc,
                                           const std::uint32_t required_category_id) {
  if (session == nullptr || dbc == nullptr || required_category_id == 0) {
    return false;
  }

  const auto *required_category = dbc->totem_category().LookupEntry(required_category_id);
  if (required_category == nullptr) {
    return false;
  }

  std::uint32_t remaining_mask = required_category->totem_category_mask;
  bool satisfied = false;
  ForEachTradeSkillToolInventoryItem(
      session->inventory_replica(), [&](const ::openwow::game::ItemInstance &item) {
    if (item.entry == 0) {
      return true;
    }

    const auto *item_template = session->query_cache().GetOrRequestItemTemplate(item.entry);
    if (item_template == nullptr || item_template->totem_category == 0) {
      return true;
    }

    const auto *item_category = dbc->totem_category().LookupEntry(item_template->totem_category);
    if (item_category == nullptr ||
        item_category->totem_category_type != required_category->totem_category_type) {
      return true;
    }

    remaining_mask &= ~item_category->totem_category_mask;
    if (remaining_mask == 0) {
      satisfied = true;
      return false;
    }

    return true;
      });

  return satisfied;
}

std::optional<std::string> ResolveTradeSkillToolInvSlotLabel(lua_State *L,
                                                             const std::uint32_t inv_slot_mask) {
  if (inv_slot_mask == 0) {
    return std::nullopt;
  }

  for (std::size_t bit_index = 0; bit_index < kTradeSkillInvSlotGlobalKeys.size(); ++bit_index) {
    const auto bit = static_cast<std::uint32_t>(1u << bit_index);
    if ((inv_slot_mask & bit) == 0) {
      continue;
    }

    return GetTradeSkillGlobalString(L, kTradeSkillInvSlotGlobalKeys[bit_index]);
  }

  return std::nullopt;
}

void PopulateTradeSkillToolRequirementsFromSpell(const ::openwow::data::dbc::SpellEntry &spell,
                                                 TradeSkillRecipe *recipe) {
  recipe->spell_focus_object_id = spell.requires_spell_focus;
  recipe->required_item_entries[0] = spell.totem[0];
  recipe->required_item_entries[1] = spell.totem[1];
  recipe->required_totem_category_ids[0] = spell.totem_category[0];
  recipe->required_totem_category_ids[1] = spell.totem_category[1];
}

std::optional<std::uint32_t> ParseTradeSkillLinkU32(const std::string_view field) {
  std::uint32_t value = 0;
  const auto [ptr, ec] = std::from_chars(field.data(), field.data() + field.size(), value);
  if (ec != std::errc{} || ptr != field.data() + field.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::uint64_t> ParseTradeSkillLinkGuid(const std::string_view field) {
  std::uint64_t value = 0;
  const auto [ptr, ec] =
      std::from_chars(field.data(), field.data() + field.size(), value, 16);
  if (ec != std::errc{} || ptr != field.data() + field.size()) {
    return std::nullopt;
  }
  return value;
}

std::string ResolveTradeSkillLineName(const ::openwow::data::dbc::DbcLoader *dbc,
                                      const std::uint32_t skill_line_id) {
  if (dbc == nullptr || skill_line_id == 0) {
    return {};
  }

  const auto *entry = dbc->skill_line().LookupEntry(skill_line_id);
  if (entry == nullptr || entry->name.empty()) {
    return {};
  }

  return std::string(entry->name);
}

std::string ResolveActiveTradeSkillLineName(lua_State *L) {

  if (!s_trade_skill.open) {
    return {};
  }

  if (!s_trade_skill.skill_name.empty()) {
    return s_trade_skill.skill_name;
  }

  return ResolveTradeSkillLineName(GetDbcLoader(L),
                                   ::openwow::game::ProfessionSystem::Get().GetOpenSkillLine());
}

void PushEmptyTradeSkillInfoResult(lua_State *L) {
  FrameScript_PushNil(L);
  FrameScript_PushNil(L);
  FrameScript_PushNumber(L, 0.0);
  FrameScript_PushNil(L);
  FrameScript_PushNil(L);
}

void PushTradeSkillInfoAltVerb(lua_State *L, const TradeSkillRecipe &recipe) {
  if (recipe.product_link_type == kTradeSkillResultItemType24 ||
      recipe.product_link_type == kTradeSkillResultItemType59 ||
      recipe.product_link_type == kTradeSkillResultItemType157) {
    FrameScript_PushNil(L);
    return;
  }

  const std::string line_name = ResolveActiveTradeSkillLineName(L);
  if (line_name.empty()) {
    FrameScript_PushNil(L);
    return;
  }

  lua_pushstring(L, line_name.c_str());
}

struct ParsedTradeSkillHyperlink {
  std::uint32_t spell_id = 0;
  std::uint32_t current_rank = 0;
  std::uint32_t max_rank = 0;
  std::uint64_t player_guid = 0;
  std::string encoded_recipe_bits;
};

bool PopulateParsedTradeSkillHyperlink(
    const std::uint32_t spell_id, const std::string_view current_rank_field,
    const std::string_view max_rank_field, const std::string_view player_guid_field,
    const std::string_view recipe_bits, ParsedTradeSkillHyperlink *out) {
  const auto current_rank = ParseTradeSkillLinkU32(current_rank_field);
  const auto max_rank = ParseTradeSkillLinkU32(max_rank_field);
  const auto player_guid = ParseTradeSkillLinkGuid(player_guid_field);
  if (spell_id == 0 || !current_rank.has_value() || !max_rank.has_value() ||
      !player_guid.has_value() || *player_guid == 0 || recipe_bits.empty()) {
    return false;
  }

  out->spell_id = spell_id;
  out->current_rank = *current_rank;
  out->max_rank = *max_rank;
  out->player_guid = *player_guid;
  out->encoded_recipe_bits = recipe_bits;
  return true;
}

bool ParseTradeSkillHyperlink(const char *link, ParsedTradeSkillHyperlink *out) {
  if (link == nullptr || out == nullptr) {
    return false;
  }

  ::openwow::game::HyperlinkInfo hyperlink;
  if (::openwow::game::HyperlinkParser::Parse(link, hyperlink) && hyperlink.type == "trade" &&
      hyperlink.params.size() >= 4) {
    return PopulateParsedTradeSkillHyperlink(
        hyperlink.id, hyperlink.params[0], hyperlink.params[1], hyperlink.params[2],
        hyperlink.params[3], out);
  }

  std::string_view payload(link);
  const auto trade_prefix = payload.find("trade:");
  if (trade_prefix == std::string_view::npos) {
    return false;
  }
  payload.remove_prefix(trade_prefix + std::string_view("trade:").size());
  if (const auto markup = payload.find('|'); markup != std::string_view::npos) {
    payload = payload.substr(0, markup);
  }

  std::array<std::string_view, 5> fields;
  for (std::size_t index = 0; index + 1 < fields.size(); ++index) {
    const auto delimiter = payload.find(':');
    if (delimiter == std::string_view::npos) {
      return false;
    }
    fields[index] = payload.substr(0, delimiter);
    payload.remove_prefix(delimiter + 1);
  }
  fields.back() = payload;

  const auto spell_id = ParseTradeSkillLinkU32(fields[0]);
  return spell_id.has_value() &&
         PopulateParsedTradeSkillHyperlink(*spell_id, fields[1], fields[2], fields[3], fields[4],
                                           out);
}

bool TryDecodeTradeSkillLinkValue(const char encoded, std::uint8_t *value) {
  if (value == nullptr) {
    return false;
  }

  const auto encoded_index = kTradeSkillLinkAlphabet.find(encoded);
  if (encoded_index == std::string_view::npos) {
    return false;
  }

  *value = static_cast<std::uint8_t>(encoded_index);
  return true;
}

std::vector<const ::openwow::data::dbc::SkillLineAbilityEntry *>
CollectTradeSkillLinkAbilitySpan(const ::openwow::data::dbc::DbcLoader &dbc,
                                 const std::uint32_t skill_line_id) {

  std::vector<const ::openwow::data::dbc::SkillLineAbilityEntry *> abilities;
  for (const auto &ability : dbc.skill_line_ability().entries()) {
    if (ability.skill_id != skill_line_id) {
      continue;
    }
    abilities.push_back(&ability);
  }
  return abilities;
}

std::string ResolveTradeSkillSpellName(const ::openwow::data::dbc::DbcLoader &dbc,
                                       const std::uint32_t spell_id) {
  if (const auto cached_spell = ::openwow::game::SpellQueryBridge::Get().Query(spell_id);
      cached_spell.has_value() && !cached_spell->name.empty()) {
    return cached_spell->name;
  }

  const auto *spell = dbc.spell().LookupEntry(spell_id);
  if (spell == nullptr || spell->spell_name.empty()) {
    return {};
  }

  return std::string(spell->spell_name);
}

std::string ResolveTradeSkillSpellIconPath(const ::openwow::data::dbc::DbcLoader &dbc,
                                           const std::uint32_t spell_id) {
  const auto *spell = dbc.spell().LookupEntry(spell_id);
  if (spell == nullptr || spell->spell_icon_id == 0) {
    return {};
  }

  const auto *icon = dbc.spell_icon().LookupEntry(spell->spell_icon_id);
  if (icon == nullptr || icon->icon_path.empty()) {
    return {};
  }

  return std::string(icon->icon_path);
}

bool IsTradeSkillRecipeResultEffect(const std::uint32_t effect_id) {
  return effect_id == kTradeSkillResultItemType24 || effect_id == kTradeSkillResultItemType59 ||
         effect_id == kTradeSkillResultItemType157;
}

std::uint8_t ResolveTradeSkillDifficulty(
    const ::openwow::data::dbc::SkillLineAbilityEntry &ability,
    const std::uint32_t current_rank) {
  const auto high = ability.trivial_skill_hi;
  auto low = ability.trivial_skill_lo;
  if (low == 0) {
    low = high == 25 ? 0 : high - 25;
  }

  if (current_rank < low) {
    return 0;
  }
  if (current_rank < ((high + low) / 2u)) {
    return 1;
  }
  if (current_rank < high) {
    return 2;
  }
  return 3;
}

void PopulateTradeSkillReagents(const ::openwow::data::dbc::SpellEntry &spell,
                                TradeSkillRecipe *recipe) {
  if (recipe == nullptr) {
    return;
  }

  for (std::size_t index = 0; index < spell.reagent.size(); ++index) {
    if (spell.reagent[index] <= 0 || spell.reagent_count[index] == 0) {
      continue;
    }

    recipe->reagents.push_back(
        {spell.reagent[index], static_cast<std::uint32_t>(spell.reagent_count[index])});
  }
}

std::uint32_t ResolveTradeSkillNumAvailable(
    const ::openwow::game::PlayerInventoryReplica* inventory,
    const ::openwow::data::dbc::SpellEntry& spell) {
  if (inventory == nullptr) {
    return 0;
  }

  std::int32_t available = -1;
  for (std::size_t index = 0; index < spell.reagent.size(); ++index) {
    if (spell.reagent[index] <= 0 || spell.reagent_count[index] == 0) {
      continue;
    }

    const auto reagent_count = static_cast<std::uint32_t>(spell.reagent_count[index]);
    const auto owned_count = inventory->GetItemCount(
        static_cast<std::uint32_t>(spell.reagent[index]));
    const auto craft_count = static_cast<std::int32_t>(owned_count / reagent_count);
    if (available == -1 || craft_count < available) {
      available = craft_count;
    }
  }

  return available > 0 ? static_cast<std::uint32_t>(available) : 0;
}

const ::openwow::data::dbc::ItemSubClassEntry *LookupTradeSkillItemSubClass(
    const ::openwow::data::dbc::DbcLoader &dbc, const std::int32_t item_class_id,
    const std::int32_t item_sub_class_id) {
  if (item_class_id < 0 || item_sub_class_id < 0) {
    return nullptr;
  }

  for (const auto &entry : dbc.item_sub_class().entries()) {
    if (static_cast<std::int32_t>(entry.class_id) == item_class_id &&
        static_cast<std::int32_t>(entry.subclass_id) == item_sub_class_id) {
      return &entry;
    }
  }

  return nullptr;
}

static bool IsTradeSkillSpecialSubClass(std::int32_t item_class_id,
                                        std::int32_t item_sub_class_id);

void AddTradeSkillSubClassDefinition(const ::openwow::data::dbc::DbcLoader &dbc,
                                     const TradeSkillRecipe &recipe) {
  const bool special_sub_class =
      IsTradeSkillSpecialSubClass(recipe.item_class_id, recipe.item_sub_class_id);
  if (!special_sub_class && (recipe.item_class_id < 0 || recipe.item_sub_class_id < 0)) {
    return;
  }

  for (auto &sub_class : s_trade_skill.sub_classes) {
    if (sub_class.item_class_id == recipe.item_class_id &&
        sub_class.item_sub_class_id == recipe.item_sub_class_id) {
      sub_class.inv_slot_mask |= recipe.inv_slot_mask;
      return;
    }
  }

  TradeSkillSubClass sub_class;
  sub_class.item_class_id = recipe.item_class_id;
  sub_class.item_sub_class_id = recipe.item_sub_class_id;
  sub_class.inv_slot_mask = recipe.inv_slot_mask;
  if (!special_sub_class) {
    if (const auto *entry =
            LookupTradeSkillItemSubClass(dbc, recipe.item_class_id, recipe.item_sub_class_id);
        entry != nullptr) {
      if (!entry->verbose_name.empty()) {
        sub_class.localized_name = std::string(entry->verbose_name);
      }
      if (!entry->display_name.empty()) {
        sub_class.name = std::string(entry->display_name);
      }
    }
  }

  s_trade_skill.sub_classes.push_back(std::move(sub_class));
}

void FinalizeTradeSkillRecipeListFromResolvedResults(const ::openwow::data::dbc::DbcLoader &dbc) {
  s_trade_skill.sub_classes.clear();
  s_trade_skill.supported_inv_slot_mask = 0;

  for (const auto &recipe : s_trade_skill.source_recipes) {
    s_trade_skill.supported_inv_slot_mask |= recipe.inv_slot_mask;
    AddTradeSkillSubClassDefinition(dbc, recipe);
  }
  SortTradeSkillSubClassesForClient();

  std::vector<TradeSkillRecipe> final_recipes;
  final_recipes.reserve(s_trade_skill.source_recipes.size() + s_trade_skill.sub_classes.size());

  std::vector<std::pair<std::int32_t, std::int32_t>> emitted_headers;
  for (const auto &recipe : s_trade_skill.source_recipes) {
    if (FindTradeSkillSubClassIndex(recipe) >= 0) {
      const auto key = std::make_pair(recipe.item_class_id, recipe.item_sub_class_id);
      if (std::find(emitted_headers.begin(), emitted_headers.end(), key) == emitted_headers.end()) {
        TradeSkillRecipe header;
        header.spell_id = -1;
        header.is_header = true;
        header.item_class_id = recipe.item_class_id;
        header.item_sub_class_id = recipe.item_sub_class_id;
        final_recipes.push_back(std::move(header));
        emitted_headers.push_back(key);
      }
    }

    final_recipes.push_back(recipe);
  }

  s_trade_skill.recipes = std::move(final_recipes);
  MarkTradeSkillListViewDirty();
}

const ::openwow::game::ItemTemplate *RequestTradeSkillResultItemTemplate(
    ::openwow::game::WorldSession &session, const std::uint32_t item_id) {
  if (item_id == 0) {
    return nullptr;
  }

  return session.query_cache().GetOrRequestItemTemplate(
      item_id,
      ::openwow::game::QueryCache::QueryRequestOptions{
          .callback_key = TradeSkillResultItemQueryCallbackKey(),
          .callback = OnTradeSkillResultItemQueryResolved,
      });
}

void PopulateTradeSkillRecipeClassificationFromSpell(const ::openwow::data::dbc::SpellEntry &spell,
                                                     TradeSkillRecipe *recipe) {
  recipe->item_class_id = -1;
  recipe->item_sub_class_id = -1;
  recipe->inv_slot_mask = kTradeSkillInvSlotNone;

  if (spell.equipped_item_class == 2) {
    recipe->inv_slot_mask = (spell.equipped_item_sub_class_mask & 1) != 0
                                ? kTradeSkillInvSlotWeaponEnchant
                                : kTradeSkillInvSlotTwoHandWeaponEnchant;
    return;
  }

  if (spell.equipped_item_class == 4) {
    if (spell.equipped_item_inv_type_mask != 0) {
      std::uint32_t resolved_mask = 0;
      for (std::size_t bit_index = 0; bit_index <= 28; ++bit_index) {
        if ((spell.equipped_item_inv_type_mask & static_cast<std::int32_t>(1u << bit_index)) == 0) {
          continue;
        }
        resolved_mask = CanonicalizeTradeSkillInvSlotMask(kTradeSkillInvTypeMasks[bit_index]);
        break;
      }
      recipe->inv_slot_mask = resolved_mask != 0 ? resolved_mask : kTradeSkillInvSlotNone;
      return;
    }

    if ((spell.equipped_item_sub_class_mask & 0x40) != 0) {
      recipe->inv_slot_mask = kTradeSkillInvSlotShield;
    }
  }
}

void PopulateTradeSkillRecipeClassificationFromItem(
    const ::openwow::game::ItemTemplate &item_template, TradeSkillRecipe *recipe) {
  recipe->item_class_id = static_cast<std::int32_t>(item_template.item_class);
  recipe->item_sub_class_id = static_cast<std::int32_t>(item_template.subclass);
  recipe->inv_slot_mask = ResolveTradeSkillInvSlotMaskFromInvType(
      static_cast<std::uint32_t>(item_template.inventory_type));
}

void PrimeTradeSkillRecipeResultClassification(::openwow::game::WorldSession *session,
                                               const ::openwow::data::dbc::DbcLoader &dbc,
                                               std::vector<TradeSkillRecipe> *recipes) {
  std::vector<std::uint32_t> pending_item_ids;

  for (auto &recipe : *recipes) {
    const auto *spell = dbc.spell().LookupEntry(static_cast<std::uint32_t>(recipe.spell_id));
    if (spell == nullptr) {
      continue;
    }

    PopulateTradeSkillRecipeClassificationFromSpell(*spell, &recipe);
    if (recipe.product_link_type == 0 || recipe.item_id <= 0 || session == nullptr) {
      continue;
    }

    const auto item_id = static_cast<std::uint32_t>(recipe.item_id);
    if (const auto *item_template = RequestTradeSkillResultItemTemplate(*session, item_id);
        item_template != nullptr) {
      PopulateTradeSkillRecipeClassificationFromItem(*item_template, &recipe);
      continue;
    }

    if (std::find(pending_item_ids.begin(), pending_item_ids.end(), item_id) ==
        pending_item_ids.end()) {
      pending_item_ids.push_back(item_id);
    }
  }

  s_trade_skill.pending_result_item_queries =
      static_cast<std::uint32_t>(pending_item_ids.size());
}

void RebuildTradeSkillListOnPendingResultItemCompletion() {
  if (s_trade_skill.pending_result_item_queries == 0) {
    return;
  }

  --s_trade_skill.pending_result_item_queries;
  if (s_trade_skill.pending_result_item_queries != 0 || s_trade_skill.dbc == nullptr ||
      s_trade_skill.source_recipes.empty()) {
    return;
  }

  PrimeTradeSkillRecipeResultClassification(s_trade_skill.session, *s_trade_skill.dbc,
                                            &s_trade_skill.source_recipes);
  if (s_trade_skill.pending_result_item_queries != 0) {
    s_trade_skill.recipes = s_trade_skill.source_recipes;
    MarkTradeSkillListViewDirty();
    return;
  }
  FinalizeTradeSkillRecipeListFromResolvedResults(*s_trade_skill.dbc);
  FireTradeSkillUpdateEvent();
}

bool BuildLinkedTradeSkillRecipes(const ::openwow::data::dbc::DbcLoader &dbc,
                                  ::openwow::game::WorldSession *session,
                                  const std::uint32_t skill_line_id,
                                  const std::uint32_t current_rank,
                                  const std::string_view encoded_recipe_bits) {

  const auto abilities = CollectTradeSkillLinkAbilitySpan(dbc, skill_line_id);
  if (abilities.empty() ||
      encoded_recipe_bits.size() != static_cast<std::size_t>((abilities.size() + 5u) / 6u)) {
    return false;
  }

  s_trade_skill.source_recipes.clear();
  std::size_t char_index = 0;
  std::uint8_t encoded_value = 0;
  for (std::size_t ability_index = 0; ability_index < abilities.size(); ++ability_index) {
    const std::size_t bit_index = ability_index % 6u;
    if (bit_index == 0) {
      if (char_index >= encoded_recipe_bits.size() ||
          !TryDecodeTradeSkillLinkValue(encoded_recipe_bits[char_index], &encoded_value)) {
        return false;
      }
      ++char_index;
    }

    if ((encoded_value & (1u << bit_index)) == 0) {
      continue;
    }

    const auto *ability = abilities[ability_index];
    const auto *spell = dbc.spell().LookupEntry(ability->spell_id);
    if (spell == nullptr || (spell->attributes & 0x20u) == 0) {
      continue;
    }

    TradeSkillRecipe recipe;
    recipe.spell_id = static_cast<std::int32_t>(ability->spell_id);
    recipe.name = ResolveTradeSkillSpellName(dbc, ability->spell_id);
    recipe.icon_path = ResolveTradeSkillSpellIconPath(dbc, ability->spell_id);
    recipe.difficulty = ResolveTradeSkillDifficulty(*ability, current_rank);
    recipe.num_available =
        ResolveTradeSkillNumAvailable(session != nullptr ? &session->inventory_replica() : nullptr,
                                      *spell);
    recipe.num_skill_ups = ability->num_skill_ups != 0 ? ability->num_skill_ups : 1;
    recipe.product_link_id = recipe.spell_id;
    PopulateTradeSkillReagents(*spell, &recipe);
    PopulateTradeSkillToolRequirementsFromSpell(*spell, &recipe);
    if (IsTradeSkillRecipeResultEffect(spell->effect[0]) && spell->effect_item_type[0] != 0) {
      recipe.item_id = static_cast<std::int32_t>(spell->effect_item_type[0]);
      recipe.product_link_type = spell->effect[0];
    }

    s_trade_skill.source_recipes.push_back(std::move(recipe));
  }

  s_trade_skill.recipes = s_trade_skill.source_recipes;
  PrimeTradeSkillRecipeResultClassification(session, dbc, &s_trade_skill.source_recipes);
  if (s_trade_skill.pending_result_item_queries == 0) {
    FinalizeTradeSkillRecipeListFromResolvedResults(dbc);
  } else {
    MarkTradeSkillListViewDirty();
  }
  return true;
}

void FireTradeSkillUpdateEvent() {
  ScriptEventDispatch::Get().FireEvent(events::TRADE_SKILL_UPDATE);
}

void FireTradeSkillRecastEvent() {

  ScriptEventDispatch::Get().FireEvent(events::UPDATE_TRADESKILL_RECAST);
}

void FireTradeSkillFilterUpdateEvent() {
  ScriptEventDispatch::Get().FireEvent(events::TRADE_SKILL_FILTER_UPDATE);
}

char16_t LowerTradeSkillFilterCodeUnit(char16_t code_unit) {
  if ((code_unit >= u'A' && code_unit <= u'Z') || (code_unit >= 0x00C0 && code_unit <= 0x00DE) ||
      (code_unit >= 0x0410 && code_unit <= 0x042F)) {
    return static_cast<char16_t>(code_unit + 32);
  }

  if (code_unit == 0x0152) {
    return 0x0153;
  }

  if (code_unit == 0x0401) {
    return 0x0451;
  }

  return code_unit;
}

void AppendTradeSkillFilterCodeUnit(std::u16string *output, const char16_t code_unit,
                                    const ::openwow::game::Locale locale) {
  switch (locale) {
  case ::openwow::game::Locale::frFR:
    switch (code_unit) {
    case 0x00E0:
    case 0x00E2:
    case 0x00E4:
      output->push_back(u'a');
      return;
    case 0x00E6:
      output->append(u"ae");
      return;
    case 0x00E7:
      output->push_back(u'c');
      return;
    case 0x00E8:
    case 0x00E9:
    case 0x00EA:
    case 0x00EB:
      output->push_back(u'e');
      return;
    case 0x00EE:
    case 0x00EF:
      output->push_back(u'i');
      return;
    case 0x00F2:
    case 0x00F3:
    case 0x00F4:
    case 0x00F6:
      output->push_back(u'o');
      return;
    case 0x0153:
      output->append(u"oe");
      return;
    case 0x00F9:
    case 0x00FA:
    case 0x00FB:
    case 0x00FC:
      output->push_back(u'u');
      return;
    default:
      break;
    }
    break;
  case ::openwow::game::Locale::deDE:
    if (code_unit == 0x00DF) {
      output->append(u"ss");
      return;
    }
    break;
  case ::openwow::game::Locale::esES:
  case ::openwow::game::Locale::esMX:
    switch (code_unit) {
    case 0x00E1:
      output->push_back(u'a');
      return;
    case 0x00E9:
      output->push_back(u'e');
      return;
    case 0x00ED:
      output->push_back(u'i');
      return;
    case 0x00F1:
      output->push_back(u'n');
      return;
    case 0x00F3:
      output->push_back(u'o');
      return;
    case 0x00FA:
    case 0x00FC:
      output->push_back(u'u');
      return;
    default:
      break;
    }
    break;
  case ::openwow::game::Locale::ruRU:
    if (code_unit == 0x0451) {
      output->push_back(static_cast<char16_t>(0x0435));
      return;
    }
    break;
  default:
    break;
  }

  output->push_back(code_unit);
}

std::u16string DecodeTradeSkillFilterUtf16(const std::string_view input) {
  std::vector<char16_t> buffer(input.size() + 1u, u'\0');
  int code_units_written = 0;
  std::uint32_t bytes_consumed = 0;
  ::openwow::core::StormUtf8ToUtf16Bounded(buffer.data(), static_cast<int>(buffer.size()),
                                           input.data(), static_cast<int>(input.size()),
                                           &code_units_written, &bytes_consumed);
  (void)bytes_consumed;

  return std::u16string(buffer.data(),
                        buffer.data() + static_cast<std::size_t>(code_units_written));
}

std::string EncodeTradeSkillFilterUtf8(const std::u16string_view input) {
  std::vector<char> buffer(input.size() * 4u + 1u, '\0');
  std::uint32_t bytes_written = 0;
  int code_units_consumed = 0;
  ::openwow::core::StormUtf16ToUtf8Bounded(buffer.data(), static_cast<std::uint32_t>(buffer.size()),
                                           input.data(), static_cast<int>(input.size()),
                                           &bytes_written, &code_units_consumed);
  (void)code_units_consumed;

  return std::string(buffer.data(), buffer.data() + bytes_written);
}

std::optional<std::string> NormalizeTradeSkillItemNameFilter(const char *text) {
  if (text == nullptr || *text == '\0') {
    return std::nullopt;
  }

  const auto locale = ::openwow::game::Localization::Get().GetLocale();
  const std::u16string decoded = DecodeTradeSkillFilterUtf16(text);
  std::u16string normalized;
  normalized.reserve(decoded.size() * 2u);
  for (const char16_t code_unit : decoded) {
    AppendTradeSkillFilterCodeUnit(&normalized, LowerTradeSkillFilterCodeUnit(code_unit), locale);
  }

  if (normalized.empty()) {
    return std::nullopt;
  }

  return EncodeTradeSkillFilterUtf8(normalized);
}

bool IsTradeSkillSpecialSubClass(const std::int32_t item_class_id,
                                 const std::int32_t item_sub_class_id) {
  return item_class_id == -1 && item_sub_class_id == -1;
}

const char *ResolveTradeSkillSubClassDisplayName(const TradeSkillSubClass &sub_class) {
  if (IsTradeSkillSpecialSubClass(sub_class.item_class_id, sub_class.item_sub_class_id) &&
      !s_trade_skill.skill_name.empty()) {
    return s_trade_skill.skill_name.c_str();
  }

  if (!sub_class.localized_name.empty()) {
    return sub_class.localized_name.c_str();
  }

  if (!sub_class.name.empty()) {
    return sub_class.name.c_str();
  }

  return nullptr;
}

int CompareTradeSkillSubClasses(const TradeSkillSubClass &lhs, const TradeSkillSubClass &rhs) {
  if (lhs.item_class_id != rhs.item_class_id) {
    return lhs.item_class_id > rhs.item_class_id ? 1 : -1;
  }

  const char *lhs_name = ResolveTradeSkillSubClassDisplayName(lhs);
  const char *rhs_name = ResolveTradeSkillSubClassDisplayName(rhs);
  if (lhs_name != nullptr && rhs_name != nullptr) {
    return ::openwow::core::SStrCmpNoCaseCollate(lhs_name, rhs_name, 0x7FFFFFFFu);
  }

  if (lhs_name == rhs_name) {
    return 0;
  }

  return lhs_name != nullptr ? 1 : -1;
}

void SortTradeSkillSubClassesForClient() {
  std::sort(s_trade_skill.sub_classes.begin(), s_trade_skill.sub_classes.end(),
            [](const TradeSkillSubClass &lhs, const TradeSkillSubClass &rhs) {
              return CompareTradeSkillSubClasses(lhs, rhs) < 0;
            });
}

void MarkTradeSkillListViewDirty() {
  s_trade_skill.list_view_dirty = true;
}

int FindTradeSkillSubClassIndex(const TradeSkillRecipe &recipe) {
  for (std::size_t index = 0; index < s_trade_skill.sub_classes.size(); ++index) {
    const auto &sub_class = s_trade_skill.sub_classes[index];
    if (sub_class.item_class_id == recipe.item_class_id &&
        sub_class.item_sub_class_id == recipe.item_sub_class_id) {
      return static_cast<int>(index);
    }
  }

  return -1;
}

const TradeSkillSubClass *FindTradeSkillSubClass(const TradeSkillRecipe &recipe) {
  const int sub_class_index = FindTradeSkillSubClassIndex(recipe);
  if (sub_class_index < 0) {
    return nullptr;
  }

  return &s_trade_skill.sub_classes[static_cast<std::size_t>(sub_class_index)];
}

bool IsTradeSkillSubClassHeaderCollapsed(const int sub_class_index) {
  if (sub_class_index < 0 ||
      sub_class_index >= static_cast<int>(s_trade_skill.sub_classes.size())) {
    return false;
  }

  return (s_trade_skill.header_expand_mask & (1u << sub_class_index)) == 0;
}

void SetTradeSkillSubClassHeaderExpanded(const int sub_class_index, const bool expanded) {
  if (sub_class_index < 0 ||
      sub_class_index >= static_cast<int>(s_trade_skill.sub_classes.size())) {
    return;
  }

  const std::uint32_t bit = 1u << sub_class_index;
  if (expanded) {
    s_trade_skill.header_expand_mask |= bit;
  } else {
    s_trade_skill.header_expand_mask &= ~bit;
  }
}

bool TradeSkillSearchCandidateMatches(const std::string_view candidate) {
  if (!s_trade_skill.item_name_filter.has_value() || candidate.empty()) {
    return false;
  }

  const std::string candidate_text(candidate);
  const auto normalized = NormalizeTradeSkillItemNameFilter(candidate_text.c_str());
  return normalized.has_value() &&
         normalized->find(*s_trade_skill.item_name_filter) != std::string::npos;
}

bool TradeSkillLocalizedSearchCandidateMatches(const std::string_view global_key,
                                                const std::string_view fallback = {}) {
  if (global_key.empty()) {
    return false;
  }

  const std::string key(global_key);
  const std::string fallback_text = fallback.empty() ? key : std::string(fallback);
  return TradeSkillSearchCandidateMatches(
      ::openwow::game::Localization::Get().GetString(key, fallback_text));
}

bool TradeSkillFormattedSignedValueMatches(
    const std::string_view global_key,
    const std::int32_t value,
    const std::string_view trailing_argument = {}) {
  if (value == 0 || global_key.empty()) {
    return false;
  }

  const std::uint32_t value_bits = std::bit_cast<std::uint32_t>(value);
  const std::uint32_t magnitude = value < 0 ? 0u - value_bits : value_bits;
  const std::string format = ::openwow::game::Localization::Get().GetString(
      std::string(global_key), std::string(global_key));
  std::vector<std::string> arguments = {
      std::string(1, value < 0 ? '-' : '+'), std::to_string(magnitude)};
  if (!trailing_argument.empty()) {
    arguments.emplace_back(trailing_argument);
  }
  return TradeSkillSearchCandidateMatches(
      ::openwow::game::Localization::Get().FormatString(format, arguments));
}

std::string ResolveTradeSkillSearchSpellText(
    const std::uint32_t spell_id,
    const ::openwow::data::dbc::DbcLoader *dbc) {
  std::string text = ::openwow::game::ExpandSpellDescription(spell_id);
  if (text.empty()) {
    text = ::openwow::game::SpellQueryBridge::Get().GetSpellDescription(spell_id);
  }
  if (text.empty() && dbc != nullptr) {
    if (const auto *spell = dbc->spell().LookupEntry(spell_id); spell != nullptr) {
      text.assign(spell->description);
      if (text.empty()) {
        text.assign(spell->tooltip);
      }
    }
  }
  return text;
}

bool TradeSkillItemTemplateMatchesSearch(
    const ::openwow::game::ItemTemplate &item_template,
    const ::openwow::data::dbc::DbcLoader *dbc) {
  if (TradeSkillSearchCandidateMatches(item_template.name) ||
      TradeSkillSearchCandidateMatches(item_template.description)) {
    return true;
  }

  char stat_key[128] = {};
  for (const auto &stat : item_template.stats) {
    if (stat.value == 0) {
      continue;
    }
    const char *key = GetStatModifierGlobalStringName(
        stat.type + 11u, stat_key, static_cast<int>(sizeof(stat_key)));
    if (key != nullptr && *key != '\0' &&
        TradeSkillFormattedSignedValueMatches(key, stat.value)) {
      return true;
    }
  }

  constexpr std::array<std::string_view, 6> kResistanceSchoolKeys = {
      "SPELL_SCHOOL1_CAP", "SPELL_SCHOOL2_CAP", "SPELL_SCHOOL3_CAP",
      "SPELL_SCHOOL4_CAP", "SPELL_SCHOOL5_CAP", "SPELL_SCHOOL6_CAP",
  };
  const std::array resistances{
      item_template.holy_res, item_template.fire_res,
      item_template.nature_res, item_template.frost_res,
      item_template.shadow_res, item_template.arcane_res};
  const bool all_resistances_equal = std::all_of(
      resistances.begin() + 1, resistances.end(),
      [&](const std::int32_t resistance) {
        return resistance == resistances.front();
      });
  if (all_resistances_equal && resistances.front() != 0) {
    if (TradeSkillFormattedSignedValueMatches(
            "ITEM_RESIST_ALL", resistances.front())) {
      return true;
    }
  } else {
    for (std::size_t index = 0; index < resistances.size(); ++index) {
      if (resistances[index] == 0) {
        continue;
      }
      const std::string school = ::openwow::game::Localization::Get().GetString(
          std::string(kResistanceSchoolKeys[index]),
          std::string(kResistanceSchoolKeys[index]));
      if (TradeSkillFormattedSignedValueMatches(
              "ITEM_RESIST_SINGLE", resistances[index], school)) {
        return true;
      }
    }
  }

  for (const auto &item_spell : item_template.spells) {
    if (item_spell.spell_id == 0) {
      continue;
    }

    const std::string description =
        ResolveTradeSkillSearchSpellText(item_spell.spell_id, dbc);
    constexpr std::array<std::string_view, 3> kTriggerKeys = {
        "ITEM_SPELL_TRIGGER_ONUSE", "ITEM_SPELL_TRIGGER_ONEQUIP",
        "ITEM_SPELL_TRIGGER_ONPROC",
    };
    std::string rendered_text = description;
    if (item_spell.trigger < kTriggerKeys.size()) {
      rendered_text = ::openwow::game::Localization::Get().GetString(
                          std::string(kTriggerKeys[item_spell.trigger]),
                          std::string(kTriggerKeys[item_spell.trigger])) +
                      " " + description;
    }
    if (TradeSkillSearchCandidateMatches(rendered_text)) {
      return true;
    }
  }

  constexpr std::array<std::pair<std::uint32_t, std::string_view>, 4> kSocketKeys = {{
      {1u, "EMPTY_SOCKET_META"}, {2u, "EMPTY_SOCKET_RED"},
      {4u, "EMPTY_SOCKET_YELLOW"}, {8u, "EMPTY_SOCKET_BLUE"},
  }};
  for (const auto &socket : item_template.sockets) {
    for (const auto &[mask, key] : kSocketKeys) {
      if ((socket.color & mask) != 0 && TradeSkillLocalizedSearchCandidateMatches(key)) {
        return true;
      }
    }
  }

  if (dbc != nullptr && item_template.socket_bonus != 0) {
    if (const auto *enchantment =
            dbc->spell_item_enchantment().LookupEntry(item_template.socket_bonus);
        enchantment != nullptr &&
        TradeSkillSearchCandidateMatches(enchantment->description)) {
      return true;
    }
  }

  return false;
}

bool TradeSkillRecipeMatchesItemSearchFilter(const TradeSkillRecipe &recipe) {
  const auto *session = s_trade_skill.session;
  const auto *dbc = s_trade_skill.dbc;
  const ::openwow::game::ItemTemplate *result_item = nullptr;
  if (session != nullptr && recipe.item_id > 0) {
    result_item = session->query_cache().GetItemTemplate(
        static_cast<std::uint32_t>(recipe.item_id));
  }

  if (result_item != nullptr) {
    const auto item_level = static_cast<std::int64_t>(result_item->item_level);
    if ((s_trade_skill.item_level_filter_min != 0 &&
         item_level < s_trade_skill.item_level_filter_min) ||
        (s_trade_skill.item_level_filter_max != 0 &&
         item_level > s_trade_skill.item_level_filter_max)) {
      return false;
    }
  }

  if (!s_trade_skill.item_name_filter.has_value()) {
    return true;
  }

  if (result_item != nullptr && TradeSkillItemTemplateMatchesSearch(*result_item, dbc)) {
    return true;
  }
  if (TradeSkillSearchCandidateMatches(recipe.name) ||
      TradeSkillSearchCandidateMatches(recipe.product_display_name)) {
    return true;
  }

  if (recipe.spell_id > 0 &&
      TradeSkillSearchCandidateMatches(
          ResolveTradeSkillSearchSpellText(
              static_cast<std::uint32_t>(recipe.spell_id), dbc))) {
    return true;
  }

  if (session != nullptr) {
    const auto item_name_matches = [&](const std::uint32_t item_id) {
      if (item_id == 0) {
        return false;
      }
      const auto *item = session->query_cache().GetItemTemplate(item_id);
      return item != nullptr && TradeSkillSearchCandidateMatches(item->name);
    };

    for (const auto &reagent : recipe.reagents) {
      if (reagent.item_id > 0 &&
          item_name_matches(static_cast<std::uint32_t>(reagent.item_id))) {
        return true;
      }
    }
    for (const auto required_item : recipe.required_item_entries) {
      if (item_name_matches(required_item)) {
        return true;
      }
    }
  }

  if (dbc != nullptr) {
    for (const std::uint32_t category_id : recipe.required_totem_category_ids) {
      if (category_id == 0) {
        continue;
      }
      if (const auto *category = dbc->totem_category().LookupEntry(category_id);
          category != nullptr && TradeSkillSearchCandidateMatches(category->name)) {
        return true;
      }
    }
  }

  if (dbc != nullptr && recipe.spell_focus_object_id != 0) {
    if (const auto *focus =
            dbc->spell_focus_object().LookupEntry(recipe.spell_focus_object_id);
        focus != nullptr && TradeSkillSearchCandidateMatches(focus->name)) {
      return true;
    }
  }

  return false;
}

void RebuildTradeSkillListView() {
  std::vector<bool> entry_visible(s_trade_skill.recipes.size(), true);

  for (std::size_t raw_index = 0; raw_index < s_trade_skill.recipes.size(); ++raw_index) {
    const auto &recipe = s_trade_skill.recipes[raw_index];
    if (recipe.is_header) {
      continue;
    }

    const int sub_class_index = FindTradeSkillSubClassIndex(recipe);
    if (sub_class_index >= 0 &&
        (s_trade_skill.sub_class_filter_mask & (1u << sub_class_index)) == 0) {
      entry_visible[raw_index] = false;
      continue;
    }

    if ((s_trade_skill.inv_slot_filter_mask & recipe.inv_slot_mask) == 0 ||
        (s_trade_skill.only_show_makeable && recipe.num_available == 0) ||
        (s_trade_skill.only_show_skill_ups && recipe.difficulty >= 3) ||
        !TradeSkillRecipeMatchesItemSearchFilter(recipe)) {
      entry_visible[raw_index] = false;
    }
  }

  for (std::size_t raw_index = 0; raw_index < s_trade_skill.recipes.size(); ++raw_index) {
    const auto &header = s_trade_skill.recipes[raw_index];
    if (!header.is_header) {
      continue;
    }

    const int sub_class_index = FindTradeSkillSubClassIndex(header);
    bool has_visible_child = false;
    for (std::size_t child_index = 0; child_index < s_trade_skill.recipes.size(); ++child_index) {
      if (child_index == raw_index) {
        continue;
      }

      const auto &child = s_trade_skill.recipes[child_index];
      if (child.is_header) {
        continue;
      }
      if (child.item_class_id != header.item_class_id ||
          child.item_sub_class_id != header.item_sub_class_id) {
        continue;
      }
      if (!entry_visible[child_index]) {
        continue;
      }

      has_visible_child = true;
      if (IsTradeSkillSubClassHeaderCollapsed(sub_class_index)) {
        entry_visible[child_index] = false;
      }
    }

    if (!has_visible_child) {
      entry_visible[raw_index] = false;
    }
  }

  s_trade_skill.ordered_recipe_indices.resize(s_trade_skill.recipes.size());
  for (std::size_t raw_index = 0; raw_index < s_trade_skill.recipes.size(); ++raw_index) {
    s_trade_skill.ordered_recipe_indices[raw_index] = raw_index;
  }

  const auto retail_entry_less =
      [&](const std::size_t lhs_index, const std::size_t rhs_index) -> bool {
        if (entry_visible[lhs_index] != entry_visible[rhs_index]) {
          return entry_visible[lhs_index];
        }

        const auto &lhs = s_trade_skill.recipes[lhs_index];
        const auto &rhs = s_trade_skill.recipes[rhs_index];
        const int lhs_sub_class = FindTradeSkillSubClassIndex(lhs);
        const int rhs_sub_class = FindTradeSkillSubClassIndex(rhs);
        if (lhs_sub_class != rhs_sub_class) {
          return lhs_sub_class < rhs_sub_class;
        }
        if (lhs.is_header != rhs.is_header) {
          return lhs.is_header;
        }
        if (lhs.is_header) {
          return false;
        }
        if (lhs.difficulty != rhs.difficulty) {
          return lhs.difficulty < rhs.difficulty;
        }
        if (lhs.num_skill_ups != rhs.num_skill_ups) {
          return lhs.num_skill_ups > rhs.num_skill_ups;
        }
        if (lhs.num_available != rhs.num_available) {
          return lhs.num_available > rhs.num_available;
        }
        return ::openwow::core::SStrCmpNoCaseCollate(
                   lhs.name.c_str(), rhs.name.c_str(), 0x7FFFFFFFu) < 0;
      };

  if (s_trade_skill.sub_classes.empty()) {
    std::stable_partition(
        s_trade_skill.ordered_recipe_indices.begin(),
        s_trade_skill.ordered_recipe_indices.end(),
        [&](const std::size_t index) { return entry_visible[index]; });
  } else {
    std::stable_sort(s_trade_skill.ordered_recipe_indices.begin(),
                     s_trade_skill.ordered_recipe_indices.end(),
                     retail_entry_less);
  }

  s_trade_skill.visible_recipe_count = static_cast<std::size_t>(
      std::count(entry_visible.begin(), entry_visible.end(), true));

  s_trade_skill.list_view_dirty = false;
}

void EnsureTradeSkillListView() {
  if (!s_trade_skill.list_view_dirty) {
    return;
  }

  RebuildTradeSkillListView();
}

const TradeSkillRecipe *GetTradeSkillRecipeByOrderedIndex(const int index) {
  EnsureTradeSkillListView();
  if (index < 1 || index > static_cast<int>(s_trade_skill.ordered_recipe_indices.size())) {
    return nullptr;
  }

  return &s_trade_skill
              .recipes[s_trade_skill.ordered_recipe_indices[static_cast<std::size_t>(index - 1)]];
}

std::size_t GetTradeSkillVisibleRecipeCount() {
  EnsureTradeSkillListView();
  return s_trade_skill.visible_recipe_count;
}

int FindTradeSkillSubClassIndexByZeroBasedRow(const int index) {
  EnsureTradeSkillListView();
  if (index < 0 || static_cast<std::size_t>(index) >=
                       s_trade_skill.ordered_recipe_indices.size()) {
    return -1;
  }

  const auto &recipe = s_trade_skill.recipes[
      s_trade_skill.ordered_recipe_indices[static_cast<std::size_t>(index)]];
  if (!recipe.is_header) {
    return -1;
  }

  return FindTradeSkillSubClassIndex(recipe);
}

bool IsTradeSkillHeaderExpanded(const TradeSkillRecipe &recipe) {
  if (!recipe.is_header) {
    return false;
  }

  return !IsTradeSkillSubClassHeaderCollapsed(FindTradeSkillSubClassIndex(recipe));
}

const char *ResolveTradeSkillHeaderName(const TradeSkillRecipe &recipe) {
  if (!recipe.header_name.empty()) {
    return recipe.header_name.c_str();
  }

  if (const auto *sub_class = FindTradeSkillSubClass(recipe)) {
    if (const char *display_name = ResolveTradeSkillSubClassDisplayName(*sub_class)) {
      return display_name;
    }
  }

  if (recipe.item_class_id == -1 && recipe.item_sub_class_id == -1 &&
      !s_trade_skill.skill_name.empty()) {
    return s_trade_skill.skill_name.c_str();
  }

  if (!recipe.name.empty()) {
    return recipe.name.c_str();
  }

  return nullptr;
}

std::uint32_t GetCurrentTradeSkillInvSlotMask() {
  std::uint32_t mask = s_trade_skill.supported_inv_slot_mask;
  const int selected_index = s_trade_skill.selected_sub_class_index;
  if (selected_index >= 0 && selected_index < static_cast<int>(s_trade_skill.sub_classes.size())) {
    mask &= s_trade_skill.sub_classes[static_cast<std::size_t>(selected_index)].inv_slot_mask;
  }

  return mask;
}

int ResolveVisibleInvSlotBitIndex(const int ordinal) {
  if (ordinal < 0) {
    return -1;
  }

  const std::uint32_t visible_mask = GetCurrentTradeSkillInvSlotMask();
  int visible_index = 0;
  for (std::size_t bit_index = 0; bit_index < kTradeSkillInvSlotGlobalKeys.size(); ++bit_index) {
    const std::uint32_t bit = 1u << bit_index;
    if ((visible_mask & bit) == 0) {
      continue;
    }
    if (visible_index == ordinal) {
      return static_cast<int>(bit_index);
    }
    ++visible_index;
  }

  return -1;
}

bool AreAllTradeSkillSubClassesVisible() {
  for (std::size_t index = 0; index < s_trade_skill.sub_classes.size(); ++index) {
    if ((s_trade_skill.sub_class_filter_mask & (1u << index)) == 0) {
      return false;
    }
  }

  return true;
}

void ApplyTradeSkillSubClassFilter(const std::uint32_t filter_mask,
                                   const int selected_sub_class_index) {
  s_trade_skill.sub_class_filter_mask = filter_mask;
  s_trade_skill.selected_sub_class_index = selected_sub_class_index;
  MarkTradeSkillListViewDirty();
  RebuildTradeSkillListView();
  FireTradeSkillFilterUpdateEvent();
}

void ApplyTradeSkillInvSlotFilter(const std::uint32_t filter_mask) {
  s_trade_skill.inv_slot_filter_mask = filter_mask;
  MarkTradeSkillListViewDirty();
  RebuildTradeSkillListView();
  FireTradeSkillFilterUpdateEvent();
}

bool IsTradeSkillItemLinkType(const std::uint32_t product_link_type) {
  return product_link_type == kTradeSkillResultItemType24 ||
         product_link_type == kTradeSkillResultItemType59 ||
         product_link_type == kTradeSkillResultItemType157;
}

const ::openwow::data::dbc::DbcLoader *ResolveTradeSkillDbcLoader(lua_State *L) {
  if (s_trade_skill.dbc != nullptr) {
    return s_trade_skill.dbc;
  }

  return GetDbcLoader(L);
}

std::optional<std::string> ResolveTradeSkillItemIconTexture(lua_State *L,
                                                            const TradeSkillRecipe &recipe) {
  if (!IsTradeSkillItemLinkType(recipe.product_link_type) || recipe.item_id <= 0) {
    return std::nullopt;
  }

  const auto *session = GetWorldSession(L);
  const auto *dbc = ResolveTradeSkillDbcLoader(L);
  if (session == nullptr || dbc == nullptr) {
    return std::nullopt;
  }

  const auto *item_template =
      session->query_cache().GetItemTemplate(static_cast<std::uint32_t>(recipe.item_id));
  if (item_template == nullptr) {
    return std::nullopt;
  }

  return ::openwow::game::ResolveItemInventoryIconTexturePath(
      dbc, item_template->display_id);
}

std::optional<std::string> ResolveTradeSkillIconTexture(lua_State *L,
                                                        const TradeSkillRecipe &recipe) {
  if (IsTradeSkillItemLinkType(recipe.product_link_type) && recipe.item_id > 0) {
    return ResolveTradeSkillItemIconTexture(L, recipe);
  }

  if (recipe.icon_path.empty()) {
    return std::nullopt;
  }

  return recipe.icon_path;
}

std::optional<double> GetTradeSkillCooldownRemainingSeconds(lua_State *L,
                                                            const TradeSkillRecipe &recipe) {
  if (recipe.spell_id <= 0) {
    return std::nullopt;
  }

  const auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return std::nullopt;
  }

  const auto &cooldowns = session->spell_book().cooldowns();
  const auto cooldown_it = cooldowns.find(static_cast<std::uint32_t>(recipe.spell_id));
  if (cooldown_it == cooldowns.end()) {
    return std::nullopt;
  }

  const auto &cooldown = cooldown_it->second;
  const double now_s = ::openwow::core::GameClock::GetTickCountSeconds();
  const double elapsed_ms = (now_s - cooldown.start_time_s) * 1000.0;
  const double total_ms = std::max(static_cast<double>(cooldown.cooldown_ms),
                                   static_cast<double>(cooldown.category_cooldown_ms));
  const double remaining_ms = total_ms - elapsed_ms;
  if (remaining_ms <= 0.0) {
    return std::nullopt;
  }

  return remaining_ms * 0.001;
}

const TradeSkillRecipe *GetTradeSkillRecipeByLuaIndex(const int index) {
  return GetTradeSkillRecipeByOrderedIndex(index);
}

const TradeSkillReagent *GetTradeSkillReagentByLuaIndex(const TradeSkillRecipe &recipe,
                                                        const int reagent_index) {
  if (reagent_index < 1 || reagent_index > static_cast<int>(recipe.reagents.size())) {
    return nullptr;
  }

  return &recipe.reagents[static_cast<std::size_t>(reagent_index - 1)];
}

std::uint32_t ResolveTradeSkillSelectionIndex() {
  if (s_trade_skill.selected_spell_id == 0) {
    return 0;
  }

  EnsureTradeSkillListView();
  for (std::size_t index = 0; index < s_trade_skill.ordered_recipe_indices.size(); ++index) {
    if (s_trade_skill.recipes[s_trade_skill.ordered_recipe_indices[index]].spell_id ==
        static_cast<std::int32_t>(s_trade_skill.selected_spell_id)) {
      return static_cast<std::uint32_t>(index + 1);
    }
  }

  return 0;
}

void QueueTradeSkillCraft(const std::uint32_t spell_id, const std::uint32_t repeat_count) {
  ::openwow::game::ProfessionSystem::Get().QueueTradeSkillCraft(spell_id, repeat_count);
  FireTradeSkillUpdateEvent();
}

void StopTradeSkillRepeatState() {
  if (::openwow::game::ProfessionSystem::Get().StopTradeSkillRepeat()) {
    FireTradeSkillRecastEvent();
  }
}

std::int32_t ResolveTradeSkillEnchantLinkId(const TradeSkillRecipe &recipe) {
  if (recipe.product_link_id != 0) {
    return recipe.product_link_id;
  }
  return recipe.spell_id;
}

std::string ResolveTradeSkillLinkDisplayName(const TradeSkillRecipe &recipe) {
  if (!recipe.product_display_name.empty()) {
    return recipe.product_display_name;
  }
  return recipe.name;
}

void RefreshTradeSkillReagentInfoOnAsyncItemTemplateSuccess(const bool success) {
  if (!success || s_trade_skill.recipes.empty()) {
    return;
  }

  MarkTradeSkillListViewDirty();
  FireTradeSkillUpdateEvent();
}

const ::openwow::game::ItemTemplate *GetOrRequestTradeSkillReagentItemTemplate(
    ::openwow::game::WorldSession &session, const std::uint32_t item_id) {
  if (item_id == 0) {
    return nullptr;
  }

  return session.query_cache().GetOrRequestItemTemplate(
      item_id,
      ::openwow::game::QueryCache::QueryRequestOptions{
          .callback_key = ::openwow::game::AsyncQueryChannel::CallbackKey(
              reinterpret_cast<std::uintptr_t>(&RefreshTradeSkillReagentInfoOnAsyncItemTemplateSuccess), 0),
          .callback = RefreshTradeSkillReagentInfoOnAsyncItemTemplateSuccess,
      });
}

std::optional<std::string> BuildTradeSkillCachedItemLink(lua_State *L, const std::int32_t item_id) {
  if (item_id <= 0) {
    return std::nullopt;
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return std::nullopt;
  }

  const auto *item_template =
      session->query_cache().GetOrRequestItemTemplate(static_cast<std::uint32_t>(item_id));
  if (item_template == nullptr) {
    return std::nullopt;
  }

  return ::openwow::game::HyperlinkParser::BuildItemLink(
      static_cast<std::uint32_t>(item_id), item_template->name,
      static_cast<std::uint32_t>(item_template->quality));
}

std::optional<std::string> BuildTradeSkillResultLink(lua_State *L, const TradeSkillRecipe &recipe) {
  if (recipe.is_header) {
    return std::nullopt;
  }

  if (IsTradeSkillItemLinkType(recipe.product_link_type)) {
    if (const auto item_link = BuildTradeSkillCachedItemLink(L, recipe.item_id)) {
      return item_link;
    }
  }

  const std::int32_t enchant_link_id = ResolveTradeSkillEnchantLinkId(recipe);
  if (enchant_link_id <= 0) {
    return std::nullopt;
  }

  return ::openwow::game::HyperlinkParser::Build(
      "enchant", static_cast<std::uint32_t>(enchant_link_id),
      ResolveTradeSkillLinkDisplayName(recipe), kTradeSkillEnchantLinkColor);
}

}

std::uint32_t GetTradeSkillRepeatCountState() {
  return ::openwow::game::ProfessionSystem::Get().GetTradeSkillRepeatCount(
      s_trade_skill.selected_spell_id);
}

std::optional<TradeSkillTooltipTarget>
ResolveTradeSkillTooltipTarget(const int recipe_index,
                               const std::optional<int> reagent_index) {
  const TradeSkillRecipe *recipe = GetTradeSkillRecipeByLuaIndex(recipe_index);
  if (recipe == nullptr || recipe->is_header) {
    return std::nullopt;
  }

  if (reagent_index.has_value()) {
    const TradeSkillReagent *reagent =
        GetTradeSkillReagentByLuaIndex(*recipe, *reagent_index);
    if (reagent == nullptr || reagent->item_id <= 0) {
      return std::nullopt;
    }

    return TradeSkillTooltipTarget{
        .kind = TradeSkillTooltipTargetKind::kItem,
        .id = static_cast<std::uint32_t>(reagent->item_id),
    };
  }

  if (IsTradeSkillItemLinkType(recipe->product_link_type) && recipe->item_id > 0) {
    return TradeSkillTooltipTarget{
        .kind = TradeSkillTooltipTargetKind::kItem,
        .id = static_cast<std::uint32_t>(recipe->item_id),
    };
  }

  const std::int32_t spell_id = ResolveTradeSkillEnchantLinkId(*recipe);
  if (spell_id <= 0) {
    return std::nullopt;
  }

  return TradeSkillTooltipTarget{
      .kind = TradeSkillTooltipTargetKind::kSpell,
      .id = static_cast<std::uint32_t>(spell_id),
  };
}

void OpenTradeSkillView(::openwow::game::WorldSession *session,
                        const ::openwow::data::dbc::DbcLoader *dbc,
                        const std::uint32_t skill_line_id, std::string skill_name,
                        const std::uint32_t current_rank, const std::uint32_t max_rank,
                        std::optional<std::string> linked_player_name,
                        std::optional<std::string> encoded_recipe_bits,
                        const std::uint32_t trade_skill_spell_id) {
  auto &professions = ::openwow::game::ProfessionSystem::Get();
  const bool linked_view = linked_player_name.has_value();
  if (professions.GetNpcTradeSkillSpell() != 0 ||
      professions.GetPlayerTradeSkillSpell() != 0) {
    StopTradeSkillRepeatState();
  }

  const bool toggle_existing_local_view =
      s_trade_skill.open && !linked_view && !professions.IsTradeSkillLinked() &&
      professions.GetOpenSkillLine() == skill_line_id && skill_line_id != 0;

  if (toggle_existing_local_view) {
    CloseTradeSkillView(session);
    return;
  }

  const bool skill_changed = professions.GetOpenSkillLine() != skill_line_id;

  CancelPendingTradeSkillResultItemQueries(session);
  ResetTradeSkillRecipeListState();
  if (skill_changed) {
    ResetTradeSkillFiltersForSkillChange();
  }
  s_trade_skill.dbc = dbc;
  s_trade_skill.session = session;

  if (linked_view && dbc != nullptr && encoded_recipe_bits.has_value() &&
      !BuildLinkedTradeSkillRecipes(*dbc, session, skill_line_id, current_rank,
                                    *encoded_recipe_bits)) {
    return;
  }

  professions.OpenTradeSkill(
      skill_line_id,
      linked_player_name.has_value() ? *linked_player_name : std::string{});

  s_trade_skill.open = true;
  s_trade_skill.skill_name = std::move(skill_name);
  s_trade_skill.current_rank = current_rank;
  s_trade_skill.max_rank = max_rank;
  s_trade_skill.trade_skill_spell_id = trade_skill_spell_id;
  s_trade_skill.is_linked = linked_view;

  FireTradeSkillFilterUpdateEvent();
  if (!linked_view || !s_trade_skill.recipes.empty()) {
    ScriptEventDispatch::Get().FireEvent(events::TRADE_SKILL_SHOW);
  }
}

void CloseTradeSkillView(::openwow::game::WorldSession *session) {
  auto &professions = ::openwow::game::ProfessionSystem::Get();
  if (professions.StopTradeSkillRepeat()) {
    FireTradeSkillUpdateEvent();
  }
  CancelPendingTradeSkillResultItemQueries(session);
  professions.CloseTradeSkill();

  ResetTradeSkillRecipeListState();
  s_trade_skill.open = false;
  s_trade_skill.skill_name.clear();
  s_trade_skill.current_rank = 0;
  s_trade_skill.max_rank = 0;
  s_trade_skill.trade_skill_spell_id = 0;
  s_trade_skill.is_linked = false;

  ScriptEventDispatch::Get().FireEvent(events::TRADE_SKILL_CLOSE);
}

void HandleTradeSkillWorldLogout(::openwow::game::WorldSession *session) {
  CloseTradeSkillView(session);
}

bool HandleTradeSkillHyperlink(lua_State *L, const char *link) {
  auto *session = GetWorldSession(L);
  if (session == nullptr || GetDbcLoader(L) == nullptr || link == nullptr) {
    return false;
  }

  ParsedTradeSkillHyperlink parsed;
  if (!ParseTradeSkillHyperlink(link, &parsed)) {
    return false;
  }

  session->BeginTradeSkillLinkOpen(parsed.spell_id, parsed.current_rank, parsed.max_rank,
                                   parsed.player_guid, std::move(parsed.encoded_recipe_bits));
  return true;
}

void ResetTradeSkillStateForTests() {
  s_trade_skill = TradeSkillData{};
  ::openwow::game::ProfessionSystem::Get().Reset();
}

void SetTradeSkillItemNameFilterState(const char *text) {
  const bool empty = text == nullptr || *text == '\0';
  if ((empty && !s_trade_skill.item_name_filter.has_value()) ||
      (!empty && s_trade_skill.item_name_filter.has_value() &&
       ::openwow::core::SStrCmpNoCase(
           text, s_trade_skill.item_name_filter->c_str(), 0x7FFFFFFFu) == 0)) {
    return;
  }

  const auto normalized = NormalizeTradeSkillItemNameFilter(text);
  s_trade_skill.item_name_filter = normalized;
  MarkTradeSkillListViewDirty();
  RebuildTradeSkillListView();
  FireTradeSkillUpdateEvent();
}

const char *GetTradeSkillItemNameFilterState() {
  if (!s_trade_skill.item_name_filter.has_value()) {
    return nullptr;
  }

  return s_trade_skill.item_name_filter->c_str();
}

void SetTradeSkillItemLevelFilterState(const int min_level, const int max_level) {
  if (s_trade_skill.item_level_filter_min == min_level &&
      s_trade_skill.item_level_filter_max == max_level) {
    return;
  }

  s_trade_skill.item_level_filter_min = min_level;
  s_trade_skill.item_level_filter_max = max_level;
  MarkTradeSkillListViewDirty();
  RebuildTradeSkillListView();
  FireTradeSkillFilterUpdateEvent();
}

void SetTradeSkillOnlyShowMakeableState(const bool enabled) {
  if (s_trade_skill.only_show_makeable == enabled) {
    return;
  }

  s_trade_skill.only_show_makeable = enabled;
  MarkTradeSkillListViewDirty();
  RebuildTradeSkillListView();
  FireTradeSkillFilterUpdateEvent();
}

void SetTradeSkillOnlyShowSkillUpsState(const bool enabled) {
  if (s_trade_skill.only_show_skill_ups == enabled) {
    return;
  }

  s_trade_skill.only_show_skill_ups = enabled;
  MarkTradeSkillListViewDirty();
  RebuildTradeSkillListView();
  FireTradeSkillFilterUpdateEvent();
}

int GetTradeSkillItemLevelFilterMinState() {
  return s_trade_skill.item_level_filter_min;
}

int GetTradeSkillItemLevelFilterMaxState() {
  return s_trade_skill.item_level_filter_max;
}

void SelectTradeSkillByLuaIndexState(const int index) {
  const auto *recipe = GetTradeSkillRecipeByLuaIndex(index);
  if (recipe == nullptr || recipe->spell_id <= 0) {
    s_trade_skill.selected_spell_id = 0;
    return;
  }

  s_trade_skill.selected_spell_id = static_cast<std::uint32_t>(recipe->spell_id);
}

void AddTradeSkillRecipeForTests(std::int32_t spell_id, const char *recipe_name, bool is_header,
                                 std::int32_t item_id, std::uint32_t product_link_type,
                                 std::int32_t product_link_id, const char *product_display_name,
                                 const std::int32_t item_class_id,
                                 const std::int32_t item_sub_class_id,
                                 const char *icon_path) {
  TradeSkillRecipe recipe;
  recipe.spell_id = spell_id;
  if (recipe_name != nullptr) {
    recipe.name = recipe_name;
  }
  recipe.is_header = is_header;
  recipe.item_id = item_id;
  recipe.product_link_type = product_link_type;
  recipe.product_link_id = product_link_id;
  recipe.item_class_id = item_class_id;
  recipe.item_sub_class_id = item_sub_class_id;
  if (icon_path != nullptr) {
    recipe.icon_path = icon_path;
  }
  if (product_display_name != nullptr) {
    recipe.product_display_name = product_display_name;
  }
  s_trade_skill.recipes.push_back(std::move(recipe));
  MarkTradeSkillListViewDirty();
}

void SetTradeSkillRecipeMaxRepeatCountForTests(const std::size_t recipe_index,
                                               const std::int32_t max_repeat_count) {
  if (recipe_index >= s_trade_skill.recipes.size()) {
    return;
  }

  s_trade_skill.recipes[recipe_index].max_repeat_count = max_repeat_count;
}

void SetTradeSkillRecipeToolDataForTests(const std::size_t recipe_index,
                                         const std::uint32_t spell_focus_object_id,
                                         const std::uint32_t required_item_entry_0,
                                         const std::uint32_t required_item_entry_1,
                                         const std::uint32_t required_totem_category_0,
                                         const std::uint32_t required_totem_category_1) {
  if (recipe_index >= s_trade_skill.recipes.size()) {
    return;
  }

  auto &recipe = s_trade_skill.recipes[recipe_index];
  recipe.spell_focus_object_id = spell_focus_object_id;
  recipe.required_item_entries = {required_item_entry_0, required_item_entry_1};
  recipe.required_totem_category_ids = {required_totem_category_0, required_totem_category_1};
}

void SetTradeSkillRecipeInvSlotMaskForTests(const std::size_t recipe_index,
                                            const std::uint32_t inv_slot_mask) {
  if (recipe_index >= s_trade_skill.recipes.size()) {
    return;
  }

  s_trade_skill.recipes[recipe_index].inv_slot_mask = inv_slot_mask;
  MarkTradeSkillListViewDirty();
}

void SetTradeSkillRecipeFilterDataForTests(const std::size_t recipe_index,
                                           const std::uint8_t difficulty,
                                           const std::uint32_t num_available) {
  if (recipe_index >= s_trade_skill.recipes.size()) {
    return;
  }

  auto &recipe = s_trade_skill.recipes[recipe_index];
  recipe.difficulty = difficulty;
  recipe.num_available = num_available;
  MarkTradeSkillListViewDirty();
}

void AddTradeSkillReagentForTests(std::size_t recipe_index, std::int32_t item_id,
                                  std::uint32_t count_needed) {
  if (recipe_index >= s_trade_skill.recipes.size()) {
    return;
  }

  s_trade_skill.recipes[recipe_index].reagents.push_back(TradeSkillReagent{item_id, count_needed});
}

void AddTradeSkillSubClassForTests(const char *name, std::uint32_t inv_slot_mask) {
  TradeSkillSubClass sub_class;
  if (name != nullptr) {
    sub_class.name = name;
  }
  sub_class.inv_slot_mask = inv_slot_mask;
  s_trade_skill.sub_classes.push_back(std::move(sub_class));
  SortTradeSkillSubClassesForClient();
  MarkTradeSkillListViewDirty();
}

void AddTradeSkillSubClassDefinitionForTests(const std::int32_t item_class_id,
                                             const std::int32_t item_sub_class_id, const char *name,
                                             const char *localized_name,
                                             const std::uint32_t inv_slot_mask) {
  TradeSkillSubClass sub_class;
  if (name != nullptr) {
    sub_class.name = name;
  }
  if (localized_name != nullptr) {
    sub_class.localized_name = localized_name;
  }
  sub_class.item_class_id = item_class_id;
  sub_class.item_sub_class_id = item_sub_class_id;
  sub_class.inv_slot_mask = inv_slot_mask;
  s_trade_skill.sub_classes.push_back(std::move(sub_class));
  SortTradeSkillSubClassesForClient();
  MarkTradeSkillListViewDirty();
}

void SetTradeSkillSupportedInvSlotMaskForTests(const std::uint32_t inv_slot_mask) {
  s_trade_skill.supported_inv_slot_mask = inv_slot_mask;
}

void SetTradeSkillQueueStateForTests(const std::int32_t selected_spell_id,
                                     const std::int32_t npc_spell_id,
                                     const std::uint32_t npc_repeat_count,
                                     const std::int32_t player_spell_id,
                                     const std::uint32_t player_repeat_count) {
  s_trade_skill.selected_spell_id =
      selected_spell_id > 0 ? static_cast<std::uint32_t>(selected_spell_id) : 0;
  ::openwow::game::ProfessionSystem::Get().SetTradeSkillSpellStateForTests(
      npc_spell_id > 0 ? static_cast<std::uint32_t>(npc_spell_id) : 0,
      npc_repeat_count,
      player_spell_id > 0 ? static_cast<std::uint32_t>(player_spell_id) : 0,
      player_repeat_count);
}

int LuaGetNumTradeSkills(lua_State *L) {
  lua_pushnumber(L, static_cast<lua_Integer>(GetTradeSkillVisibleRecipeCount()));
  return 1;
}

int LuaGetTradeSkillLine(lua_State *L) {
  const std::string skill_name = ResolveActiveTradeSkillLineName(L);

  if (skill_name.empty()) {
    lua_pushstring(L, "UNKNOWN");
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
  } else {
    lua_pushstring(L, skill_name.c_str());
    lua_pushnumber(L, static_cast<lua_Number>(s_trade_skill.current_rank));
    lua_pushnumber(L, static_cast<lua_Number>(s_trade_skill.max_rank));
  }
  return 3;
}

int LuaGetTradeSkillInfo(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: GetTradeSkillInfo(index)");
  }

  const int index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto *recipe = GetTradeSkillRecipeByLuaIndex(index);
  if (recipe == nullptr) {
    PushEmptyTradeSkillInfoResult(L);
    return 5;
  }

  if (recipe->is_header) {
    if (const char *header_name = ResolveTradeSkillHeaderName(*recipe)) {
      lua_pushstring(L, header_name);
    } else {
      FrameScript_PushNil(L);
    }
    lua_pushstring(L, "header");
    FrameScript_PushNumber(L, 0.0);
    if (IsTradeSkillHeaderExpanded(*recipe)) {
      FrameScript_PushNumber(L, 1.0);
    } else {
      FrameScript_PushNil(L);
    }
    FrameScript_PushNil(L);
    return 5;
  }

  lua_pushstring(L, recipe->name.c_str());
  lua_pushstring(L, DifficultyString(recipe->difficulty));
  FrameScript_PushNumber(L, static_cast<double>(recipe->num_available));
  lua_pushnil(L);
  PushTradeSkillInfoAltVerb(L, *recipe);

  return 5;
}

int LuaGetTradeSkillDescription(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetTradeSkillDescription(index)");
  }

  const int index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto *recipe = GetTradeSkillRecipeByLuaIndex(index);
  if (recipe == nullptr || recipe->spell_id <= 0) {
    lua_pushnil(L);
    return 1;
  }

  const std::string description = ResolveTradeSkillSearchSpellText(
      static_cast<std::uint32_t>(recipe->spell_id), ResolveTradeSkillDbcLoader(L));
  if (description.empty()) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushlstring(L, description.data(), description.size());
  return 1;
}

int LuaGetTradeSkillIcon(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: GetTradeSkillIcon(index)");
  }

  const int index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto *recipe = GetTradeSkillRecipeByLuaIndex(index);
  if (recipe == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  if (const auto icon_texture = ResolveTradeSkillIconTexture(L, *recipe)) {
    lua_pushstring(L, icon_texture->c_str());
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaGetTradeSkillNumReagents(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: GetTradeSkillNumReagents(index)");
  }

  const int index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto *recipe = GetTradeSkillRecipeByLuaIndex(index);
  if (recipe == nullptr) {
    lua_pushnumber(L, 0);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Integer>(recipe->reagents.size()));
  return 1;
}

int LuaGetTradeSkillReagentInfo(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L,
                      "Usage: GetTradeSkillReagentInfo(index, reagentIndex)");
  }
  const int index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const int reagent_idx = TruncateLuaNumberToSseI32(lua_tonumber(L, 2));

  const auto *recipe = GetTradeSkillRecipeByLuaIndex(index);
  if (recipe == nullptr) {

    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 4;
  }

  const auto *reagent = GetTradeSkillReagentByLuaIndex(*recipe, reagent_idx);
  if (reagent == nullptr) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 4;
  }

  auto *session = GetWorldSession(L);
  const auto *item_template =
      session != nullptr
          ? GetOrRequestTradeSkillReagentItemTemplate(
                *session, static_cast<std::uint32_t>(reagent->item_id))
          : nullptr;
  if (item_template != nullptr) {
    lua_pushstring(L, item_template->name.c_str());

    std::string reagent_texture =
        ::openwow::game::BuildItemInventoryIconTexturePath(
            ::openwow::game::kFallbackItemInventoryIconName);
    if (item_template->display_id > 0) {
      lua_getfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
      auto *dbc = static_cast<const openwow::data::dbc::DbcLoader *>(lua_touserdata(L, -1));
      lua_pop(L, 1);
      if (dbc != nullptr) {
        reagent_texture =
            ::openwow::game::ResolveItemInventoryIconTexturePath(
                dbc, item_template->display_id);
      }
    }

    lua_pushstring(L, reagent_texture.c_str());
  } else {
    lua_pushnil(L);
    lua_pushnil(L);
  }

  lua_pushnumber(L, static_cast<lua_Integer>(reagent->count_needed));

  const std::uint32_t player_count = RequirePlayerInventoryReplica(L).GetItemCount(
      static_cast<std::uint32_t>(reagent->item_id));
  lua_pushnumber(L, static_cast<lua_Integer>(player_count));

  return 4;
}

int LuaGetTradeSkillReagentItemLink(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: GetTradeReagentSkillItemLink(index, reagentIndex)");
  }

  const int index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const int reagent_index = TruncateLuaNumberToSseI32(lua_tonumber(L, 2));
  const auto *recipe = GetTradeSkillRecipeByLuaIndex(index);
  if (recipe == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto *reagent = GetTradeSkillReagentByLuaIndex(*recipe, reagent_index);
  if (reagent == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto item_link = BuildTradeSkillCachedItemLink(L, reagent->item_id);
  if (!item_link.has_value()) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushstring(L, item_link->c_str());
  return 1;
}

int LuaGetTradeSkillItemLink(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetTradeSkillItemLink(index)");
  }

  const int index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto *recipe = GetTradeSkillRecipeByLuaIndex(index);
  if (recipe == nullptr) {
    return 0;
  }

  const auto link = BuildTradeSkillResultLink(L, *recipe);
  if (!link.has_value()) {
    return 0;
  }

  lua_pushstring(L, link->c_str());
  return 1;
}

int LuaGetTradeSkillNumMade(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetTradeSkillNumMade(index)");
  }

  const int index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto *recipe = GetTradeSkillRecipeByLuaIndex(index);
  if (recipe == nullptr || recipe->is_header || recipe->spell_id <= 0) {
    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, 1.0);
    return 2;
  }

  const auto *dbc = GetDbcLoader(L);
  if (dbc != nullptr) {
    const auto *spell =
        dbc->spell().LookupEntry(static_cast<std::uint32_t>(recipe->spell_id));
    if (spell != nullptr) {

      const std::int32_t base = spell->effect_base_points[0] + 1;
      const std::int32_t die_sides = spell->effect_die_sides[0];
      int min_made = base;
      int max_made = (die_sides > 0) ? (spell->effect_base_points[0] + die_sides)
                                     : base;
      if (min_made < 1) min_made = 1;
      if (max_made < 1) max_made = 1;

      lua_pushnumber(L, static_cast<double>(min_made));
      lua_pushnumber(L, static_cast<double>(max_made));
      return 2;
    }
  }

  lua_pushnumber(L, 1.0);
  lua_pushnumber(L, 1.0);
  return 2;
}

int LuaGetTradeSkillRecipeLink(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetTradeSkillRecipeLink(index)");
  }

  const int index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto *recipe = GetTradeSkillRecipeByLuaIndex(index);
  if (recipe == nullptr || recipe->is_header || recipe->spell_id <= 0) {
    return 0;
  }

  const std::string skill_name = ResolveActiveTradeSkillLineName(L);
  const std::string &recipe_name = recipe->name;

  std::string display_text;
  if (!skill_name.empty()) {
    display_text = skill_name + ": " + recipe_name;
  } else {
    display_text = recipe_name;
  }

  const auto link = ::openwow::game::HyperlinkParser::Build(
      "enchant", static_cast<std::uint32_t>(recipe->spell_id), display_text,
      kTradeSkillEnchantLinkColor);

  lua_pushstring(L, link.c_str());
  return 1;
}

int LuaGetTradeSkillCooldown(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetTradeSkillCooldown(index)");
  }

  const int index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto *recipe = GetTradeSkillRecipeByLuaIndex(index);
  if (recipe != nullptr) {
    const auto cooldown_seconds = GetTradeSkillCooldownRemainingSeconds(L, *recipe);
    if (cooldown_seconds.has_value()) {
      lua_pushnumber(L, *cooldown_seconds);
      return 1;
    }
  }

  lua_pushnil(L);
  return 1;
}

int LuaGetTradeSkillTools(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetTradeSkillTools(index)");
  }

  const int index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto *recipe = GetTradeSkillRecipeByLuaIndex(index);
  auto *session = GetWorldSession(L);
  if (recipe == nullptr || recipe->is_header || session == nullptr) {
    return 0;
  }

  const auto *dbc = GetDbcLoader(L);
  int result_pairs = 0;
  const auto push_tool_result = [&](const std::string_view name, const bool has_tool) {
    lua_pushlstring(L, name.data(), name.size());
    if (has_tool) {
      lua_pushnumber(L, 1.0);
    } else {
      lua_pushnil(L);
    }
    ++result_pairs;
  };

  if (dbc != nullptr && recipe->spell_focus_object_id != 0) {
    if (const auto *focus = dbc->spell_focus_object().LookupEntry(recipe->spell_focus_object_id);
        focus != nullptr && !focus->name.empty()) {
      push_tool_result(focus->name, true);
    }
  }

  if (const auto inv_slot_label = ResolveTradeSkillToolInvSlotLabel(L, recipe->inv_slot_mask);
      inv_slot_label.has_value()) {
    push_tool_result(*inv_slot_label, true);
  }

  for (const auto item_id : recipe->required_item_entries) {
    if (item_id == 0) {
      continue;
    }

    const auto *item_template = session->query_cache().GetOrRequestItemTemplate(item_id);
    if (item_template == nullptr || item_template->name.empty()) {
      continue;
    }

    push_tool_result(item_template->name,
                     HasTradeSkillToolItemInInventory(session->inventory_replica(), item_id));
  }

  if (dbc != nullptr) {
    for (const auto category_id : recipe->required_totem_category_ids) {
      if (category_id == 0) {
        continue;
      }

      const auto *category = dbc->totem_category().LookupEntry(category_id);
      if (category == nullptr || category->name.empty()) {
        continue;
      }

      push_tool_result(category->name,
                       HasTradeSkillTotemCategoryInInventory(session, dbc, category_id));
    }
  }

  return result_pairs * 2;
}

int LuaDoTradeSkill(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: DoTradeSkill(index [, repeat])");
  }

  const int index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  std::uint32_t repeat_count = 1;
  if (lua_isnumber(L, 2)) {
    repeat_count = ::openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 2));
  }

  const auto *recipe = GetTradeSkillRecipeByLuaIndex(index);
  if (recipe == nullptr) {
    return 0;
  }

  if (recipe->spell_id <= 0 || recipe->is_header)
    return 0;

  if (recipe->max_repeat_count >= 0 &&
      repeat_count >= static_cast<std::uint32_t>(recipe->max_repeat_count)) {
    repeat_count = static_cast<std::uint32_t>(recipe->max_repeat_count);
  }

  const auto spell_id = static_cast<std::uint32_t>(recipe->spell_id);
  auto &professions = ::openwow::game::ProfessionSystem::Get();
  if (!professions.HasActiveTradeSkillSpell(spell_id)) {
    auto *session = GetWorldSession(L);
    if (session == nullptr) {
      return 0;
    }

    if (!session->interaction().TrySendCastSpell(
            spell_id, kTradeSkillCastFlags, kTradeSkillCastTargetGuid)) {
      return 0;
    }
  }

  QueueTradeSkillCraft(spell_id, repeat_count);
  return 0;
}

int LuaGetTradeSkillSelectionIndex(lua_State *L) {
  lua_pushnumber(L, static_cast<lua_Integer>(ResolveTradeSkillSelectionIndex()));
  return 1;
}

int LuaStopTradeSkillRepeat([[maybe_unused]] lua_State *L) {
  StopTradeSkillRepeatState();
  return 0;
}

int LuaCloseTradeSkill([[maybe_unused]] lua_State *L) {

  s_trade_skill.trade_skill_spell_id = 0;
  s_trade_skill.open = false;
  s_trade_skill.skill_name.clear();
  s_trade_skill.current_rank = 0;
  s_trade_skill.max_rank = 0;
  if (::openwow::game::ProfessionSystem::Get().ClearAllTradeSkillSpells()) {
    FireTradeSkillUpdateEvent();
  }
  ScriptEventDispatch::Get().FireEvent(events::TRADE_SKILL_CLOSE);
  return 0;
}

std::optional<std::string> BuildTradeSkillListLink(
    lua_State *L, const std::uint32_t trade_skill_spell_id) {
  auto *session = GetWorldSession(L);
  const auto *dbc = ResolveTradeSkillDbcLoader(L);
  if (session == nullptr || dbc == nullptr || trade_skill_spell_id == 0) {
    return std::nullopt;
  }

  const auto *player = session->objects().GetActivePlayer();
  const auto *trade_spell = dbc->spell().LookupEntry(trade_skill_spell_id);
  if (player == nullptr || trade_spell == nullptr || trade_spell->effect[0] != 47u ||
      trade_spell->spell_name.empty()) {
    return std::nullopt;
  }

  const auto race = player->State().GetRace();
  const auto player_class = player->State().GetClass();
  const auto *trade_ability = ::openwow::game::FindSkillLineAbilityForRaceClassSpell(
      dbc->skill_line_ability().entries(), dbc->skill_race_class_info().entries(), race,
      player_class, trade_skill_spell_id,
      ::openwow::game::SkillRaceClassIdentity{race, player_class});
  if (trade_ability == nullptr) {
    return std::nullopt;
  }

  const auto skill_line_id = trade_ability->skill_id;
  const auto *skill_line = dbc->skill_line().LookupEntry(skill_line_id);
  const auto skill_slot = player->FindActiveSkillSlot(static_cast<std::uint16_t>(skill_line_id));
  if (skill_line == nullptr || skill_line->can_link == 0 || !skill_slot.has_value()) {
    return std::nullopt;
  }

  const auto skill = player->GetSkill(*skill_slot);
  const auto current_rank = skill.value == 0
                                ? 0u
                                : static_cast<std::uint32_t>(skill.value) +
                                      static_cast<std::uint16_t>(skill.step_modifier);
  const auto max_rank = skill.max_value == 0
                            ? 0u
                            : static_cast<std::uint32_t>(skill.max_value) +
                                  static_cast<std::uint16_t>(skill.step_modifier);
  const auto player_guid = session->objects().GetActivePlayerGuid().GetRawValue();
  if (player_guid == 0) {
    return std::nullopt;
  }

  const auto abilities = CollectTradeSkillLinkAbilitySpan(*dbc, skill_line_id);
  std::string encoded((abilities.size() + 5u) / 6u, kTradeSkillLinkAlphabet.front());
  for (std::size_t group = 0; group < encoded.size(); ++group) {
    std::uint8_t value = 0;
    for (std::size_t bit = 0; bit < 6u; ++bit) {
      const auto index = group * 6u + bit;
      if (index >= abilities.size()) {
        break;
      }
      if (session->spell_book().HasSpell(abilities[index]->spell_id) &&
          ::openwow::game::SkillLineAbilityMatchesRaceClass(*abilities[index], race,
                                                            player_class)) {
        value |= static_cast<std::uint8_t>(1u << bit);
      }
    }
    encoded[group] = kTradeSkillLinkAlphabet[value];
  }

  char prefix[160]{};
  const int prefix_size = std::snprintf(
      prefix, sizeof(prefix), "|cffffd000|Htrade:%u:%u:%u:%llX:",
      trade_skill_spell_id, current_rank, max_rank,
      static_cast<unsigned long long>(player_guid));
  if (prefix_size < 0 || static_cast<std::size_t>(prefix_size) >= sizeof(prefix)) {
    return std::nullopt;
  }

  std::string result(prefix, static_cast<std::size_t>(prefix_size));
  result += encoded;
  result += "|h[";
  result.append(trade_spell->spell_name.data(), trade_spell->spell_name.size());
  result += "]|h|r";

  if (result.size() >= 256u) {
    return std::nullopt;
  }
  return result;
}

int LuaGetTradeSkillListLink(lua_State *L) {
  auto trade_skill_spell_id = s_trade_skill.trade_skill_spell_id;
  if (trade_skill_spell_id == 0 && s_trade_skill.open && s_trade_skill.session != nullptr) {
    const auto skill_line_id = ::openwow::game::ProfessionSystem::Get().GetOpenSkillLine();
    if (const auto *player = s_trade_skill.session->objects().GetActivePlayer();
        player != nullptr) {
      trade_skill_spell_id =
          player->Casts().GetTrackedTradeSkillSpell(skill_line_id).value_or(0);
    }
  }
  const auto link = BuildTradeSkillListLink(L, trade_skill_spell_id);
  if (link.has_value()) {
    lua_pushlstring(L, link->data(), link->size());
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaCollapseTradeSkillSubClass(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: CollapseTradeSkillSubClass(index)");
  }

  const int index = ::openwow::ui::SignedI32FromU32Bits(
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 1))) - 1u);
  if (index >= 0) {
    const int sub_class_index = FindTradeSkillSubClassIndexByZeroBasedRow(index);
    if (sub_class_index < 0) {
      return luaL_error(L, "Bad sub class in CollapseTradeSkillSubClass");
    }
    SetTradeSkillSubClassHeaderExpanded(sub_class_index, false);
  } else {
    s_trade_skill.header_expand_mask = 0;
  }

  MarkTradeSkillListViewDirty();
  FireTradeSkillFilterUpdateEvent();
  return 0;
}

int LuaExpandTradeSkillSubClass(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: ExpandTradeSkillSubClass(index)");
  }

  const int index = ::openwow::ui::SignedI32FromU32Bits(
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 1))) - 1u);
  if (index >= 0) {
    const int sub_class_index = FindTradeSkillSubClassIndexByZeroBasedRow(index);
    if (sub_class_index < 0) {
      return luaL_error(L, "Bad skill line in ExpandTradeSkillSubClass");
    }
    SetTradeSkillSubClassHeaderExpanded(sub_class_index, true);
  } else {
    s_trade_skill.header_expand_mask = kTradeSkillAllMask;
  }

  MarkTradeSkillListViewDirty();
  FireTradeSkillFilterUpdateEvent();
  return 0;
}

int LuaGetFirstTradeSkill(lua_State *L) {
  EnsureTradeSkillListView();
  for (std::size_t i = 0; i < s_trade_skill.visible_recipe_count; ++i) {
    const auto &recipe = s_trade_skill.recipes[s_trade_skill.ordered_recipe_indices[i]];
    if (recipe.spell_id > 0) {
      lua_pushnumber(L, static_cast<lua_Integer>(i + 1));
      return 1;
    }
  }
  lua_pushnumber(L, 0);
  return 1;
}

int LuaIsTradeSkillLinked(lua_State *L) {
  const auto linked_player_name = ::openwow::game::ProfessionSystem::Get().GetLinkedTradeSkillPlayer();
  if (linked_player_name.empty()) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  FrameScript_PushNumber(L, 1.0);
  lua_pushstring(L, linked_player_name.c_str());
  return 2;
}

int LuaGetTradeSkillInvSlots(lua_State *L) {
  const std::uint32_t visible_mask = GetCurrentTradeSkillInvSlotMask();
  (void)openwow::ui::ReserveLuaResultCapacity(
      L, kTradeSkillInvSlotGlobalKeys.size(), "trade skill inventory slots");
  int count = 0;
  for (std::size_t bit_index = 0; bit_index < kTradeSkillInvSlotGlobalKeys.size(); ++bit_index) {
    if ((visible_mask & (1u << bit_index)) == 0) {
      continue;
    }

    const std::string slot_name =
        GetTradeSkillGlobalString(L, kTradeSkillInvSlotGlobalKeys[bit_index]);
    lua_pushstring(L, slot_name.c_str());
    ++count;
  }
  return count;
}

int LuaGetTradeSkillInvSlotFilter(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetTradeSkillInvSlotFilter(index)");
  }

  const int index = ::openwow::ui::SignedI32FromU32Bits(
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 1))) - 1u);
  if (index < 0) {
    if (s_trade_skill.inv_slot_filter_mask == kTradeSkillAllMask) {
      FrameScript_PushNumber(L, 1.0);
    } else {
      FrameScript_PushNil(L);
    }
    return 1;
  }

  if (s_trade_skill.inv_slot_filter_mask == kTradeSkillAllMask) {
    FrameScript_PushNil(L);
    return 1;
  }

  const int bit_index = ResolveVisibleInvSlotBitIndex(index);
  if (bit_index < 0) {
    return luaL_error(L, "Bad inv type in GetTradeSkillInvSlotFilter");
  }

  if ((s_trade_skill.inv_slot_filter_mask & (1u << bit_index)) != 0) {
    FrameScript_PushNumber(L, 1.0);
  } else {
    FrameScript_PushNil(L);
  }

  return 1;
}

int LuaSetTradeSkillInvSlotFilter(lua_State *L) {
  if (lua_isstring(L, 1) == 0) {
    return luaL_error(L, "Usage: SetTradeSkillInvSlotFilter(index [, on\\off, exclusive])");
  }

  const int index = ::openwow::ui::SignedI32FromU32Bits(
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 1))) - 1u);
  if (index < 0) {
    ApplyTradeSkillInvSlotFilter(kTradeSkillAllMask);
    return 0;
  }

  const int bit_index = ResolveVisibleInvSlotBitIndex(index);
  if (bit_index < 0) {
    return luaL_error(L, "Bad inv slot in SetTradeSkillInvSlotFilter");
  }

  if (lua_isnumber(L, 2) == 0) {
    return luaL_error(L, "Missing on//off parameter in SetTradeSkillInvSlotFilter");
  }

  const std::uint32_t bit = 1u << bit_index;
  if (TruncateLuaNumberToSseI32(lua_tonumber(L, 2)) == 0) {
    ApplyTradeSkillInvSlotFilter(s_trade_skill.inv_slot_filter_mask & ~bit);
    return 0;
  }

  if (lua_isnumber(L, 3) != 0 &&
      TruncateLuaNumberToSseI32(lua_tonumber(L, 3)) != 0) {
    ApplyTradeSkillInvSlotFilter(bit);
    return 0;
  }

  ApplyTradeSkillInvSlotFilter(s_trade_skill.inv_slot_filter_mask | bit);
  return 0;
}

int LuaGetTradeSkillSubClasses(lua_State *L) {
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      L, s_trade_skill.sub_classes.size(), "trade skill subclasses");
  for (const auto &sc : s_trade_skill.sub_classes) {
    if (const char *display_name = ResolveTradeSkillSubClassDisplayName(sc)) {
      lua_pushstring(L, display_name);
    } else {
      lua_pushnil(L);
    }
  }
  return result_count;
}

int LuaGetTradeSkillSubClassFilter(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetTradeSkillSubClassFilter(index)");
  }

  const int index = ::openwow::ui::SignedI32FromU32Bits(
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 1))) - 1u);
  if (index < 0) {
    if (AreAllTradeSkillSubClassesVisible()) {
      FrameScript_PushNumber(L, 1.0);
    } else {
      FrameScript_PushNil(L);
    }
    return 1;
  }

  if (index >= static_cast<int>(s_trade_skill.sub_classes.size())) {
    return luaL_error(L, "Bad sub class in GetTradeSkillSubClassFilter");
  }

  if ((s_trade_skill.sub_class_filter_mask & (1u << index)) != 0) {
    FrameScript_PushNumber(L, 1.0);
  } else {
    FrameScript_PushNil(L);
  }

  return 1;
}

int LuaSetTradeSkillSubClassFilter(lua_State *L) {
  if (lua_isstring(L, 1) == 0) {
    return luaL_error(L, "Usage: SetTradeSkillSubClassFilter(index [, on\\off, exclusive])");
  }

  const int index = ::openwow::ui::SignedI32FromU32Bits(
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 1))) - 1u);
  if (index < 0) {
    ApplyTradeSkillSubClassFilter(kTradeSkillAllMask, -1);
    return 0;
  }

  if (index >= static_cast<int>(s_trade_skill.sub_classes.size())) {
    return luaL_error(L, "Bad sub class in SetTradeSkillSubClassFilter");
  }

  if (lua_isnumber(L, 2) == 0) {
    return luaL_error(L, "Missing on//off parameter in SetTradeSkillSubClassFilter");
  }

  const std::uint32_t bit = 1u << index;
  if (TruncateLuaNumberToSseI32(lua_tonumber(L, 2)) == 0) {
    ApplyTradeSkillSubClassFilter(s_trade_skill.sub_class_filter_mask & ~bit, index);
    return 0;
  }

  if (lua_isnumber(L, 3) != 0 &&
      TruncateLuaNumberToSseI32(lua_tonumber(L, 3)) != 0) {
    ApplyTradeSkillSubClassFilter(bit, index);
    return 0;
  }

  ApplyTradeSkillSubClassFilter(s_trade_skill.sub_class_filter_mask | bit, index);
  return 0;
}

}
