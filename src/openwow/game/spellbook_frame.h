#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game {

class CGUnit_C;
class WorldSession;

}

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

struct SpellGroupEntry {
  std::uint32_t spell_id{0};
  bool from_pet_spellbook{false};
  std::string subtext;
  std::uint32_t skill_level{0};
};

struct SpellGroup {
  std::string name;
  std::uint32_t name_hash{0};
  std::vector<SpellGroupEntry> entries;
};

struct ResolvedSpellGroupEntry {
  std::uint32_t spell_id{0};
  bool from_pet_spellbook{false};
};

struct ShapeshiftFormSpellOrderInfo {
  std::uint32_t cached_id{0};
  std::int32_t  order{-1};
};

using ShapeshiftFormSpellOrderLookup =
    std::function<std::optional<ShapeshiftFormSpellOrderInfo>(std::uint32_t)>;

[[nodiscard]] int CompareShapeshiftFormSpellOrder(
    const std::optional<ShapeshiftFormSpellOrderInfo>& info_a,
    const std::optional<ShapeshiftFormSpellOrderInfo>& info_b);

[[nodiscard]] int CompareShapeshiftFormSpellOrder(
    std::uint32_t spell_id_a,
    std::uint32_t spell_id_b,
    const ShapeshiftFormSpellOrderLookup& lookup);

class SpellBookFrame {
 public:

  [[nodiscard]] static std::uint32_t GetLearnSpellAutoPlaceActionSlotStart(
      const WorldSession& session,
      std::uint32_t spell_id);

  [[nodiscard]] static bool IsUnitMatchingTarget(
      std::uint64_t unit_guid,
      std::uint64_t target_guid);

  [[nodiscard]] static bool CanCastOnTarget(
      const WorldSession& session,
      std::uint32_t spell_id,
      std::uint64_t target_guid);

  static void DownrankSpellForTarget(
      const WorldSession& session,
      std::uint32_t* spell_id,
      const CGUnit_C* target_unit,
      const CGUnit_C* caster_unit);

  static void AddToSpellGroup(std::uint32_t spell_id, bool from_pet_spellbook);

  static void LearnSpell(WorldSession& session,
                         std::uint32_t spell_id,
                         bool show_learn_message,
                         std::uint32_t supersedes_id);

  static void FinalizeInitialCompanionCatalog(WorldSession& session);

  [[nodiscard]] static std::uint32_t MultiCastTotemCategoryToSlotMask(
      std::uint32_t totem_category);
  [[nodiscard]] static std::vector<std::uint32_t> GetMultiCastTotemSpells(
      std::uint8_t slot_index,
      const ::openwow::data::dbc::DbcLoader* dbc_loader);

  static bool SyncAutoFilledMultiCastSlots(WorldSession& session);

  static bool HandleCarriedMultiCastTotemCategory(
      WorldSession& session, std::uint32_t totem_category);

  static bool HandleLearnedMultiCastTotemSlotMask(WorldSession& session,
                                                  std::uint32_t slot_mask);
  static bool HandleTrackedMultiCastTotemItemEntry(WorldSession& session,
                                                   std::uint32_t item_entry);

  static void ForgetSpell(WorldSession& session,
                          std::uint32_t spell_id);

  [[nodiscard]] static std::optional<ResolvedSpellGroupEntry> ResolveSpellByName(
      std::string_view spell_name,
      std::string_view qualifier = {});
  static void RebuildPetSpellGroups(
      const std::vector<std::uint32_t>& spell_ids);

  [[nodiscard]] static const std::vector<SpellGroup>& GetSpellGroups();
  static void ClearSpellGroups();

 private:
  static std::vector<SpellGroup> s_spell_groups;
};

[[nodiscard]] std::int32_t SpellBook_ResolveSpellSlotByNameQuery(
    const WorldSession& session,
    std::string_view spell_name_query,
    bool& out_is_pet_book);

}
