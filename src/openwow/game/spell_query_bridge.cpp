
#include "openwow/game/spell_query_bridge.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/aura_tracker.h"
#include "openwow/game/inventory/items/item_icon_resolver.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/spell_book.h"
#include "openwow/game/spell_failure_names.h"
#include "openwow/game/spell_runtime_values.h"
#include "openwow/game/spell_aura_candidate_validation.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/world_session.h"

#include <algorithm>
#include <string_view>

namespace openwow::game {

namespace {

constexpr std::uint32_t kSpellAttrEx7RestrictMultiCastActionBarPlacement = 0x20u;

constexpr std::uint32_t kAttackActionSpellEffectId = 78u;
constexpr std::uint32_t kRangedActionTextureSuppressedFlags2Bit = 0x00000400u;
constexpr std::uint32_t kSpellEffectDispel = 38u;
constexpr std::uint32_t kSpellEffectApplyAura = 6u;
constexpr std::uint32_t kSpellAuraDispel = 126u;

struct ScriptVisibilitySpellData {
  std::uint32_t attributes = 0;
  std::uint32_t attributes_ex = 0;
  std::uint32_t attributes_ex4 = 0;
  std::uint32_t attributes_ex5 = 0;
  std::uint32_t attributes_ex6 = 0;
  std::uint8_t dispel_type = 0;
  std::array<std::uint32_t, 3> effect_ids{};
  std::array<std::uint32_t, 3> effect_apply_aura{};
  std::array<std::int32_t, 3> effect_misc_value{};
  std::uint8_t required_visibility_state = 0;
};

std::uint8_t BuildVisibilityStateBit(const std::uint8_t required_visibility_state) {
  if (required_visibility_state != 0 && required_visibility_state <= 32) {
    return static_cast<std::uint8_t>(
        1u << (static_cast<std::uint32_t>(required_visibility_state) - 1u));
  }

  return 0;
}

std::uint8_t BuildScriptVisibilityMask(const ScriptVisibilitySpellData& spell) {
  std::uint8_t mask = BuildVisibilityStateBit(spell.required_visibility_state);
  for (std::size_t effect_index = 0; effect_index < spell.effect_apply_aura.size();
       ++effect_index) {
    if (spell.effect_apply_aura[effect_index] == 17u) {
      mask = static_cast<std::uint8_t>(mask | 0x20u);
      continue;
    }

    if (spell.effect_apply_aura[effect_index] == 19u) {
      const auto misc_value = spell.effect_misc_value[effect_index];
      if (misc_value == 0 || misc_value == 10) {
        mask = static_cast<std::uint8_t>(mask | 0x40u);
      }
    }
  }

  return mask;
}

std::optional<ScriptVisibilitySpellData>
ResolveScriptVisibilitySpellData(std::uint32_t spell_id) {
  if (spell_id == 0) {
    return std::nullopt;
  }

  ScriptVisibilitySpellData result{};
  bool have_data = false;

  if (const auto query = SpellQueryBridge::Get().Query(spell_id); query.has_value()) {
    result.attributes = query->attributes;
    result.attributes_ex = query->attributesEx;
    result.attributes_ex4 = query->attributesEx4;
    result.attributes_ex5 = query->attributesEx5;
    result.attributes_ex6 = query->attributesEx6;
    result.dispel_type = query->dispelType;
    result.effect_ids = query->effectIds;
    result.effect_apply_aura = query->effectApplyAura;
    result.effect_misc_value = query->effectMiscValue;
    result.required_visibility_state = query->requiredVisibilityState;
    have_data = true;
  }

  const auto* const dbc = SpellbookSystem::Get().GetDbcLoader();
  if (dbc != nullptr) {
    if (const auto* spell = dbc->spell().LookupEntry(spell_id); spell != nullptr) {
      result.attributes = spell->attributes;
      result.attributes_ex = spell->attributes_ex;
      result.attributes_ex4 = spell->attributes_ex4;
      result.attributes_ex5 = spell->attributes_ex5;
      result.attributes_ex6 = spell->attributes_ex6;
      result.dispel_type = static_cast<std::uint8_t>(spell->dispel);
      result.effect_ids = spell->effect;
      result.effect_apply_aura = spell->effect_apply_aura;
      result.effect_misc_value = spell->effect_misc_value;
      have_data = true;
    }
  }

  if (!have_data) {
    return std::nullopt;
  }

  return result;
}

std::uint64_t GetActivePetGuidForScriptVisibility(const WorldSession& session) {
  const auto pet_guid = session.pet().pet_bar().guid;
  return pet_guid.IsEmpty() ? 0 : pet_guid.GetRawValue();
}

std::uint8_t BuildActivePlayerScriptVisibilityMask(const ObjectManager& objects) {
  const auto local_player_guid = objects.GetLocalPlayerGuid();
  const auto* player = objects.GetUnit(local_player_guid);
  if (player == nullptr) {
    return 0;
  }

  auto mask = static_cast<std::uint8_t>(player->State().GetAuraState() & 0xffu);
  AuraTracker::Get().ForEachAuraAll(local_player_guid, [&](const std::uint8_t ,
                                                           const AuraData& aura) {
    const auto spell = ResolveScriptVisibilitySpellData(aura.spell_id);
    if (!spell.has_value()) {
      return;
    }

    mask = static_cast<std::uint8_t>(mask | BuildScriptVisibilityMask(*spell));
  });
  return mask;
}

std::uint32_t BuildScriptAuraDispelMask() {
  std::uint32_t mask = 0;

  for (const auto& spell : SpellbookSystem::Get().GetKnownSpellList()) {
    const auto data = ResolveScriptVisibilitySpellData(spell.spell_id);
    if (!data.has_value()) {
      continue;
    }

    for (std::size_t effect_index = 0; effect_index < data->effect_ids.size();
         ++effect_index) {
      if (data->effect_ids[effect_index] != kSpellEffectApplyAura ||
          data->effect_apply_aura[effect_index] != kSpellAuraDispel) {
        continue;
      }

      mask |= 1u << (static_cast<std::uint32_t>(
                          data->effect_misc_value[effect_index]) &
                      31u);
    }
  }

  return mask;
}

std::uint32_t BuildDebuffDispelTypeMask() {
  std::uint32_t mask = 0;

  for (const auto& spell : SpellbookSystem::Get().GetKnownSpellList()) {
    const auto data = ResolveScriptVisibilitySpellData(spell.spell_id);
    if (!data.has_value()) {
      continue;
    }

    for (std::size_t effect_index = 0; effect_index < data->effect_ids.size();
         ++effect_index) {
      if (data->effect_ids[effect_index] != kSpellEffectDispel) {
        continue;
      }

      mask |= 1u << (static_cast<std::uint32_t>(
                          data->effect_misc_value[effect_index]) &
                      31u);
    }
  }

  return mask;
}

}

SpellQueryBridge& SpellQueryBridge::Get() {
  static SpellQueryBridge instance;
  return instance;
}

std::optional<SpellQueryResult> SpellQueryBridge::Query(std::uint32_t spellId) const {
  if (spellId == 0) return std::nullopt;

  {
    std::lock_guard lock(mutex_);
    const auto it = cache_.find(spellId);
    if (it != cache_.end()) return it->second;
  }

  const auto* const dbc = SpellbookSystem::Get().GetDbcLoader();
  if (dbc == nullptr) return std::nullopt;

  const auto* const spell = dbc->spell().LookupEntry(spellId);
  if (spell == nullptr) return std::nullopt;

  SpellQueryResult result{};
  result.spellId = spellId;
  result.name.assign(spell->spell_name);
  result.subtext.assign(spell->rank);
  result.description.assign(spell->description);
  result.iconId = spell->spell_icon_id;
  result.manaCost = spell->mana_cost;
  result.powerType = spell->power_type <= static_cast<std::uint32_t>(PowerType::kRunicPower)
                         ? static_cast<PowerType>(spell->power_type)
                         : PowerType::kHealth;
  result.costType = PowerTypeToString(spell->power_type);
  result.cooldown = static_cast<float>(
                        std::max(spell->recovery_time,
                                 spell->category_recovery_time)) /
                    1000.0f;
  if (const auto* const cast_time =
          dbc->spell_cast_times().LookupEntry(spell->casting_time_index);
      cast_time != nullptr) {
    result.castTime = static_cast<float>(cast_time->base_cast_time) / 1000.0f;
  }
  if (const auto* const range =
          dbc->spell_range().LookupEntry(spell->range_index);
      range != nullptr) {
    result.range = std::max(range->range_max, range->range_max_friendly);
  }
  if (spell->rune_cost_id != 0u) {
    if (const auto* const rune_cost =
            dbc->spell_rune_cost().LookupEntry(spell->rune_cost_id);
        rune_cost != nullptr) {
      result.runeCost.blood = static_cast<std::uint8_t>(rune_cost->blood);
      result.runeCost.unholy = static_cast<std::uint8_t>(rune_cost->unholy);
      result.runeCost.frost = static_cast<std::uint8_t>(rune_cost->frost);
      result.hasRuneCost = rune_cost->blood != 0u || rune_cost->unholy != 0u ||
                           rune_cost->frost != 0u;
    }
  }

  result.isPassive = (spell->attributes & 0x40u) != 0u;
  result.isChanneled = (spell->attributes_ex & 0x44u) != 0u;
  result.isKnown = SpellbookSystem::Get().HasSpell(spellId);
  result.dispelType = static_cast<std::uint8_t>(spell->dispel);
  result.spellFamilyName = spell->spell_family_name;
  result.requiredLevel = spell->spell_level;
  result.stances = spell->stances;
  result.stancesHigh = spell->stances_high;
  result.stancesNot = spell->stances_not;
  result.stancesNotHigh = spell->stances_not_high;
  result.equippedItemClass = spell->equipped_item_class;
  result.equippedItemSubclassMask = spell->equipped_item_sub_class_mask;
  result.equippedItemInvTypeMask = spell->equipped_item_inv_type_mask;
  result.attributes = spell->attributes;
  result.attributesEx = spell->attributes_ex;
  result.attributesEx2 = spell->attributes_ex2;
  result.attributesEx3 = spell->attributes_ex3;
  result.attributesEx4 = spell->attributes_ex4;
  result.attributesEx5 = spell->attributes_ex5;
  result.attributesEx6 = spell->attributes_ex6;
  result.attributes2 = spell->attributes_ex2;
  result.attributes3 = spell->attributes_ex3;
  result.spellFamilyFlags = spell->spell_family_flags;
  result.schoolMask = spell->school_mask;
  result.effectIds = spell->effect;
  result.effectApplyAura = spell->effect_apply_aura;
  result.effectMiscValue = spell->effect_misc_value;
  result.requiredVisibilityState =
      static_cast<std::uint8_t>(spell->required_aura_vision);
  result.restrictsMultiCastActionBarPlacement =
      (spell->attributes_ex7 &
       kSpellAttrEx7RestrictMultiCastActionBarPlacement) != 0;
  result.multiCastTotemCategories = {
      spells::TotemCategoryId{spell->totem_category[0]},
      spells::TotemCategoryId{spell->totem_category[1]}};
  return result;
}

std::string SpellQueryBridge::GetSpellName(std::uint32_t spellId) const {
  auto r = Query(spellId);
  return r ? r->name : std::string{};
}

std::uint32_t SpellQueryBridge::GetSpellIcon(std::uint32_t spellId) const {
  auto r = Query(spellId);
  return r ? r->iconId : 0;
}

float SpellQueryBridge::GetSpellCooldown(std::uint32_t spellId) const {
  auto r = Query(spellId);
  return r ? r->cooldown : 0.0f;
}

float SpellQueryBridge::GetSpellRange(std::uint32_t spellId) const {
  auto r = Query(spellId);
  return r ? r->range : 0.0f;
}

std::string SpellQueryBridge::GetSpellRank(std::uint32_t spellId) const {
  auto r = Query(spellId);
  return r ? r->subtext : "";
}

std::uint8_t SpellQueryBridge::GetSpellDispelType(std::uint32_t spellId) const {
  auto r = Query(spellId);
  return r ? r->dispelType : 0;
}

const char* SpellQueryBridge::DispelTypeName(std::uint8_t dispelType) {
  switch (dispelType) {
    case 1: return "Magic";
    case 2: return "Curse";
    case 3: return "Disease";
    case 4: return "Poison";
    case 5: return "Enrage";
    default: return "";
  }
}

bool SpellQueryBridge::IsSpellKnown(std::uint32_t spellId) const {

  {
    std::lock_guard lock(mutex_);
    auto it = cache_.find(spellId);
    if (it != cache_.end()) return it->second.isKnown;
  }

  return SpellbookSystem::Get().HasSpell(spellId);
}

bool SpellQueryBridge::IsSpellUsable(std::uint32_t spellId) const {
  if (!IsSpellKnown(spellId)) return false;

  auto r = Query(spellId);
  if (!r) return false;
  return !r->isPassive;
}

std::string SpellQueryBridge::GetSpellDescription(std::uint32_t spellId) const {
  auto r = Query(spellId);
  return r ? r->description : std::string{};
}

void SpellQueryBridge::SetSpellData(std::uint32_t spellId, SpellQueryResult data) {
  std::lock_guard lock(mutex_);
  data.spellId = spellId;
  cache_[spellId] = std::move(data);
}

void SpellQueryBridge::SetSpellKnownState(std::uint32_t spellId, bool is_known) {
  std::lock_guard lock(mutex_);
  auto it = cache_.find(spellId);
  if (it == cache_.end()) {
    return;
  }

  it->second.isKnown = is_known;
}

void SpellQueryBridge::Reset() {
  std::lock_guard lock(mutex_);
  cache_.clear();
}

std::uint32_t SpellQueryBridge::GetCachedCount() const {
  std::lock_guard lock(mutex_);
  return static_cast<std::uint32_t>(cache_.size());
}

bool SpellQueryBridge::HasSpellData(std::uint32_t spellId) const {
  std::lock_guard lock(mutex_);
  return cache_.count(spellId) > 0;
}

void SpellQueryBridge::RemoveSpellData(std::uint32_t spellId) {
  std::lock_guard lock(mutex_);
  cache_.erase(spellId);
}

std::vector<std::uint32_t> SpellQueryBridge::GetKnownSpellIds() const {
  std::lock_guard lock(mutex_);
  std::vector<std::uint32_t> result;
  for (const auto& [id, data] : cache_) {
    if (data.isKnown) result.push_back(id);
  }
  return result;
}

std::vector<std::uint32_t> SpellQueryBridge::GetPassiveSpellIds() const {
  std::lock_guard lock(mutex_);
  std::vector<std::uint32_t> result;
  for (const auto& [id, data] : cache_) {
    if (data.isPassive) result.push_back(id);
  }
  return result;
}

std::uint32_t SpellQueryBridge::GetManaCost(std::uint32_t spellId) const {
  auto r = Query(spellId);
  return r ? r->manaCost : 0;
}

float SpellQueryBridge::GetCastTime(std::uint32_t spellId) const {
  auto r = Query(spellId);
  return r ? r->castTime : 0.0f;
}

bool SpellPassesScriptVisibilityFilter(const WorldSession& session,
                                       std::uint32_t spellId,
                                       const ObjectGuid* sourceGuid,
                                       bool isChannel) {
  const auto spell = ResolveScriptVisibilitySpellData(spellId);
  if (!spell.has_value()) {
    return true;
  }

  if ((spell->attributes & 0x80u) != 0 ||
      (spell->attributes_ex & 0x10000000u) != 0) {
    return false;
  }

  if ((spell->attributes & 0x40u) != 0 &&
      !std::any_of(spell->effect_ids.begin(), spell->effect_ids.end(),
                   IsAuraVisibilityOnlyEffect)) {
    return false;
  }

  const auto hidden_mask = isChannel ? 0x08000000u : 0x10000000u;
  if ((spell->attributes_ex5 & hidden_mask) != 0) {
    return false;
  }

  if ((spell->attributes_ex6 & 0x00100000u) != 0) {
    if (sourceGuid == nullptr || sourceGuid->IsEmpty()) {
      return false;
    }

    const auto local_player_guid = session.objects().GetLocalPlayerGuid();
    if (*sourceGuid != local_player_guid &&
        sourceGuid->GetRawValue() != GetActivePetGuidForScriptVisibility(session)) {
      return false;
    }
  }

  const auto required_mask = BuildVisibilityStateBit(spell->required_visibility_state);
  if (required_mask == 0) {
    return true;
  }

  return (BuildActivePlayerScriptVisibilityMask(session.objects()) & required_mask) != 0;
}

bool ScriptAuraCanStealOrPurge(const WorldSession& session,
                               std::uint32_t spellId,
                               std::uint32_t effectMask,
                               bool isHelpfulAura,
                               const ObjectGuid& targetGuid) {
  if (!isHelpfulAura) {
    return false;
  }

  const auto dispel_mask = BuildScriptAuraDispelMask();
  if (dispel_mask == 0 || targetGuid.IsEmpty()) {
    return false;
  }

  const auto& objects = session.objects();
  const auto* active_player = objects.GetActivePlayer();
  const auto* target = objects.GetUnit(targetGuid);
  if (active_player == nullptr || target == nullptr ||
      !active_player->Interaction().CanAttackSpellTarget(*target)) {
    return false;
  }

  const auto spell = ResolveScriptVisibilitySpellData(spellId);
  if (!spell.has_value()) {
    return false;
  }

  const SpellAuraCandidateData candidate_spell{
      .attributes = spell->attributes,
      .attributes_ex = spell->attributes_ex,
      .attributes_ex4 = spell->attributes_ex4,
      .dispel_type = spell->dispel_type,
      .effect_ids = spell->effect_ids,
  };
  AuraData candidate_aura;
  candidate_aura.slot = 0u;
  candidate_aura.effect_mask = effectMask;
  return IsSpellAuraCandidate(
      candidate_spell, candidate_aura,
      SpellAuraCandidateCriteria{
          .match = SpellAuraCandidateMatch::kDispelType,
          .requested_mask_or_mechanic = dispel_mask,
          .helpful_only = true,
          .reject_nonstealable = true,
      });
}

bool SpellBook_CanDispelType(std::uint32_t dispelType) {
  if (dispelType == 0) {
    return false;
  }

  const auto mask = BuildDebuffDispelTypeMask();
  return (mask & (1u << (dispelType & 31u))) != 0;
}

bool SpellBook_CanStealBuff(std::uint32_t spellId) {
  if (spellId == 0) {
    return false;
  }

  if (!SpellQueryBridge::Get().IsSpellKnown(spellId)) {
    return false;
  }

  const auto data = ResolveScriptVisibilitySpellData(spellId);
  if (!data.has_value()) {
    return false;
  }

  if ((data->attributes & 0x4000u) != 0) {
    return false;
  }

  if (data->effect_apply_aura[0] == 1u) {
    return false;
  }

  for (std::size_t i = 0; i < data->effect_ids.size(); ++i) {
    if (IsAuraVisibilityOnlyEffect(data->effect_ids[i])) {
      return false;
    }
  }

  return true;
}

bool SpellHasAttackActionEffect(std::uint32_t spellId,
                                const openwow::data::dbc::DbcLoader* dbc) {
  if (spellId == 0) {
    return false;
  }

  const auto* dbc_loader = dbc;
  if (dbc_loader != nullptr) {
    const auto* spell = dbc_loader->spell().LookupEntry(spellId);
    if (spell != nullptr) {
      return spell->effect[0] == kAttackActionSpellEffectId;
    }
  }

  if (const auto query = SpellQueryBridge::Get().Query(spellId);
      query.has_value()) {
    return query->effectIds[0] == kAttackActionSpellEffectId;
  }

  return false;
}

bool SpellHasRangedAttackActionFlags(
    std::uint32_t spellId,
    const openwow::data::dbc::DbcLoader* dbc) {
  if (spellId == 0) {
    return false;
  }

  const auto* dbc_loader = dbc;
  if (dbc_loader != nullptr) {
    const auto* spell = dbc_loader->spell().LookupEntry(spellId);
    if (spell != nullptr) {
      return IsAutoRepeatRangedSpell(spell->attributes, spell->attributes_ex2);
    }
  }

  if (const auto query = SpellQueryBridge::Get().Query(spellId);
      query.has_value()) {
    return IsAutoRepeatRangedSpell(query->attributes, query->attributes2);
  }

  return false;
}

std::string ResolveActiveAttackActionTexturePath(
    const WorldSession& session,
    const openwow::data::dbc::DbcLoader* dbc) {
  const auto* player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return {};
  }

  const auto* dbc_loader = dbc;
  if (dbc_loader == nullptr) {
    dbc_loader = session.GetDbcLoader();
  }

  if (dbc_loader != nullptr) {
    const auto form_id = static_cast<std::uint32_t>(player->Animation().GetShapeshiftForm());
    if (form_id != 0) {
      if (const auto* form = dbc_loader->spell_shapeshift_form().LookupEntry(form_id);
          form != nullptr && form->attack_icon_id != 0) {
        if (const auto* icon =
                dbc_loader->spell_icon().LookupEntry(form->attack_icon_id);
            icon != nullptr && !std::string_view(icon->icon_path).empty()) {
          return std::string(icon->icon_path);
        }
      }
    }

    if (player->IsVisibleWeaponDisplaySuppressed(0)) {
      return "Interface\\Buttons\\Spell-Reset";
    }

    if (const auto metadata =
            player->GetVisibleItemTemplateMetadata(InventorySlots::kMainHand);
        metadata.has_value() && metadata->display_id != 0) {
      return ResolveItemInventoryIconTexturePath(
          dbc_loader, metadata->display_id);
    }
  }

  return "Interface\\Buttons\\Spell-Reset";
}

std::string ResolveActiveRangedActionTexturePath(
    const WorldSession& session,
    const openwow::data::dbc::DbcLoader* dbc) {
  const auto* player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return {};
  }

  if ((player->State().GetUnitFlags2() & kRangedActionTextureSuppressedFlags2Bit) != 0u) {
    return {};
  }

  const auto metadata =
      player->GetVisibleItemTemplateMetadata(InventorySlots::kRanged);
  if (!metadata.has_value() || metadata->display_id == 0 ||
      metadata->subclass == 16u) {
    return {};
  }

  const auto* dbc_loader = dbc;
  if (dbc_loader == nullptr) {
    dbc_loader = session.GetDbcLoader();
  }

  if (dbc_loader != nullptr) {
    return ResolveItemInventoryIconTexturePath(
        dbc_loader, metadata->display_id);
  }

  return {};
}

}
