#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/rune_handler.h"
#include "openwow/game/spells/model/spell_values.h"
#include "openwow/game/unit_defines.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class ObjectManager;
class WorldSession;

struct SpellQueryResult {
  std::uint32_t spellId{0};
  std::string name;
  std::string subtext;
  std::string description;
  std::uint32_t iconId{0};
  float castTime{0.0f};
  float cooldown{0.0f};
  float range{0.0f};
  std::uint32_t manaCost{0};
  std::string costType;
  RuneCost runeCost{};
  bool hasRuneCost{false};
  bool isPassive{false};
  bool isChanneled{false};
  bool isKnown{false};
  std::uint8_t rank{0};
  std::uint8_t dispelType{0};
  std::uint32_t spellFamilyName{0};
  std::uint32_t skillLineId{0};
  std::uint32_t proficiencyId{0};

  PowerType powerType{PowerType::kMana};
  std::uint32_t requiredLevel{0};
  std::uint32_t stances{0};
  std::uint32_t stancesHigh{0};
  std::uint32_t stancesNot{0};
  std::uint32_t stancesNotHigh{0};
  std::int32_t equippedItemClass{-1};
  std::int32_t equippedItemSubclassMask{0};
  std::int32_t equippedItemInvTypeMask{0};
  std::uint32_t attributes{0};
  std::uint32_t attributesEx{0};
  std::uint32_t attributesEx2{0};
  std::uint32_t attributesEx3{0};
  std::uint32_t attributesEx4{0};
  std::uint32_t attributesEx5{0};
  std::uint32_t attributesEx6{0};
  std::uint32_t attributes2{0};
  std::uint32_t attributes3{0};
  std::array<std::uint32_t, 3> spellFamilyFlags{};
  std::uint32_t schoolMask{0};
  std::array<std::uint32_t, 3> effectIds{};
  std::array<std::uint32_t, 3> effectApplyAura{};
  std::array<std::int32_t, 3> effectMiscValue{};

  std::uint8_t requiredVisibilityState{0};
  bool cursorAutoTarget{false};
  bool cursorAreaTarget{false};
  bool restrictsMultiCastActionBarPlacement{false};
  std::array<spells::TotemCategoryId, 2> multiCastTotemCategories{
      spells::TotemCategoryId{0}, spells::TotemCategoryId{0}};
  bool usableInShapeshift{true};
  bool castableWhileMounted{false};
  bool castableWhileMoving{false};
};

class SpellQueryBridge {
public:

  static SpellQueryBridge &Get();

  [[nodiscard]] std::optional<SpellQueryResult> Query(std::uint32_t spellId) const;

  [[nodiscard]] std::string GetSpellName(std::uint32_t spellId) const;
  [[nodiscard]] std::uint32_t GetSpellIcon(std::uint32_t spellId) const;
  [[nodiscard]] float GetSpellCooldown(std::uint32_t spellId) const;
  [[nodiscard]] float GetSpellRange(std::uint32_t spellId) const;
  [[nodiscard]] std::string GetSpellRank(std::uint32_t spellId) const;
  [[nodiscard]] std::uint8_t GetSpellDispelType(std::uint32_t spellId) const;

  [[nodiscard]] static const char *DispelTypeName(std::uint8_t dispelType);

  [[nodiscard]] bool IsSpellKnown(std::uint32_t spellId) const;

  [[nodiscard]] bool IsSpellUsable(std::uint32_t spellId) const;

  [[nodiscard]] std::string GetSpellDescription(std::uint32_t spellId) const;

  void SetSpellData(std::uint32_t spellId, SpellQueryResult data);
  void SetSpellKnownState(std::uint32_t spellId, bool is_known);

  void Reset();

  [[nodiscard]] std::uint32_t GetCachedCount() const;

  [[nodiscard]] bool HasSpellData(std::uint32_t spellId) const;

  void RemoveSpellData(std::uint32_t spellId);

  [[nodiscard]] std::vector<std::uint32_t> GetKnownSpellIds() const;

  [[nodiscard]] std::vector<std::uint32_t> GetPassiveSpellIds() const;

  [[nodiscard]] std::uint32_t GetManaCost(std::uint32_t spellId) const;

  [[nodiscard]] float GetCastTime(std::uint32_t spellId) const;

private:
  SpellQueryBridge() = default;

  mutable std::mutex mutex_;
  std::unordered_map<std::uint32_t, SpellQueryResult> cache_;
};

[[nodiscard]] bool SpellPassesScriptVisibilityFilter(const WorldSession& session,
                                                     std::uint32_t spellId,
                                                     const ObjectGuid *sourceGuid, bool isChannel);

[[nodiscard]] bool ScriptAuraCanStealOrPurge(const WorldSession& session,
                                             std::uint32_t spellId,
                                             std::uint32_t effectMask,
                                             bool isHelpfulAura, const ObjectGuid &targetGuid);

[[nodiscard]] bool SpellBook_CanDispelType(std::uint32_t dispelType);

[[nodiscard]] bool SpellBook_CanStealBuff(std::uint32_t spellId);

[[nodiscard]] bool SpellHasAttackActionEffect(std::uint32_t spellId,
                                              const openwow::data::dbc::DbcLoader *dbc = nullptr);

[[nodiscard]] bool
SpellHasRangedAttackActionFlags(std::uint32_t spellId,
                                const openwow::data::dbc::DbcLoader *dbc = nullptr);

[[nodiscard]] std::string
ResolveActiveAttackActionTexturePath(
    const WorldSession& session,
    const openwow::data::dbc::DbcLoader* dbc = nullptr);

[[nodiscard]] std::string
ResolveActiveRangedActionTexturePath(
    const WorldSession& session,
    const openwow::data::dbc::DbcLoader* dbc = nullptr);

}
