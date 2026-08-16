
#include "openwow/game/unit_name_display.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/group_system.h"
#include "openwow/game/guild_manager.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/player_chat_flags.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/world_session.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace openwow::game {

namespace {

constexpr std::uint32_t kUnitFlagNotSelectable = 0x02000000u;

constexpr std::uint32_t kUnitFlagImmuneToNpc = 0x00000200u;

constexpr std::uint32_t kUnitFlagPlayerControlled = 0x00000008u;

constexpr std::uint8_t kUnitVisFlagCreep = 0x02u;

constexpr std::uint8_t kPvpFlagPvP = 0x01u;
constexpr std::uint8_t kPvpFlagSanctuary = 0x08u;

constexpr std::uint32_t kDynFlagDead = 0x20u;

constexpr std::array<std::uint32_t, 8> kReactionNameColors = {
    0xFFFF0000u,
    0xFFFF0000u,
    0xFFFF8000u,
    0xFFFFFF00u,
    0xFF00FF00u,
    0xFF00FF00u,
    0xFF00FF00u,
    0xFF00FF00u,
};

constexpr std::uint32_t kColorDefaultPlayerName = 0xFF6060FFu;

constexpr std::uint32_t kColorHostilePlayer = 0xFFFF0000u;

constexpr std::uint32_t kColorAttackablePlayer = 0xFFFFFF00u;

constexpr std::uint32_t kColorGroupMember = 0xFF53C9FFu;

constexpr std::uint32_t kColorOwnBattlefieldPvp = 0xFFAAFFAAu;
constexpr std::uint32_t kColorOwnBattlefieldNoPvp = 0xFFAAAAFFu;

constexpr std::uint32_t kColorDeadUnitName = 0x00000000u;

constexpr std::uint32_t kAttackPulsePeriodMs = 500u;
constexpr float kAttackPulseGreenMax = 128.0f;

std::uint32_t ComputeAttackPulseColor(const std::uint32_t now_ms) {
  const std::uint32_t phase = now_ms % (2u * kAttackPulsePeriodMs);
  const bool rising = phase >= kAttackPulsePeriodMs;
  const std::uint32_t within = phase % kAttackPulsePeriodMs;
  const float fraction =
      static_cast<float>(kAttackPulsePeriodMs - within) /
      static_cast<float>(kAttackPulsePeriodMs);
  const float value = rising ? 1.0f - fraction : fraction;
  const auto green = static_cast<std::uint32_t>(value * kAttackPulseGreenMax);
  return (green & 0xFFu) << 8u;
}

bool ViewerCanAttackUnit(const CGUnit_C &viewer, const CGUnit_C &unit,
                         UnitNameViewerRelation &relation) {
  if (!relation.attack_resolved) {
    relation.viewer_can_attack_unit =
        viewer.Interaction().CanAttackSpellTarget(unit);
    relation.attack_resolved = true;
  }
  return relation.viewer_can_attack_unit;
}

bool IsFriendlyByFactionGroup(const CGUnit_C &viewer, const CGUnit_C &unit,
                              UnitNameViewerRelation *const relation) {
  if (&viewer == &unit || unit.GetGuid() == viewer.GetGuid()) {
    return false;
  }
  if (!viewer.State().GetCharmedBy().IsEmpty() ||
      !unit.State().GetCharmedBy().IsEmpty()) {
    return false;
  }
  const auto *const dbc = viewer.dbc_loader();
  const auto *const viewer_faction =
      dbc != nullptr ? dbc->faction_template().LookupEntry(
                           viewer.State().GetFactionTemplate())
                     : nullptr;
  const auto *const unit_faction =
      dbc != nullptr ? dbc->faction_template().LookupEntry(
                           unit.State().GetFactionTemplate())
                     : nullptr;
  if (viewer_faction != nullptr && unit_faction != nullptr &&
      viewer_faction->faction_group != unit_faction->faction_group) {
    return false;
  }
  return relation != nullptr
             ? !ViewerCanAttackUnit(viewer, unit, *relation)
             : !viewer.Interaction().CanAttackSpellTarget(unit);
}

const CGUnit_C *ResolveOwnerUnit(const CGUnit_C &unit,
                                 const ObjectManager &objects) {

  const auto owner_guid = unit.State().GetCharmedBy().IsEmpty()
                              ? unit.State().GetCreatedBy()
                              : unit.State().GetCharmedBy();
  if (owner_guid.IsEmpty()) {
    return nullptr;
  }
  return objects.GetUnit(owner_guid);
}

std::string LookupGlobalString(const char *const token,
                               const char *const fallback) {

  return Localization::Get().GetString(token, fallback);
}

std::uint32_t BuildPlayerNameWithTitle(
    const CGUnit_C &unit, const bool use_title, WorldSession &world_session,
    const CreatureTemplateInfo *const creature_template, std::string &out) {
  if (unit.IsPlayer()) {
    return unit.FormatNameWithPvpTitle(world_session, use_title, out);
  }
  if (creature_template != nullptr) {
    out = creature_template->name;
  } else {
    out = LookupGlobalString("UNKNOWNOBJECT", "Unknown Being");
  }
  return 1u;
}

constexpr std::uint32_t kSpellEffectSummon = 28u;

std::string BuildOwnerLine(const CGUnit_C &unit, const ObjectManager &objects,
                           WorldSession &world_session,
                           const CreatureTypeId creature_type,
                           const data::dbc::DbcLoader *const dbc) {
  auto owner_guid = unit.State().GetCharmedBy().IsEmpty()
                        ? unit.State().GetCreatedBy()
                        : unit.State().GetCharmedBy();
  if (owner_guid.IsEmpty()) {
    return {};
  }

  if (const auto *const owner = objects.GetUnit(owner_guid)) {
    const auto grand_guid = owner->State().GetCharmedBy().IsEmpty()
                                ? owner->State().GetCreatedBy()
                                : owner->State().GetCharmedBy();
    if (!grand_guid.IsEmpty()) {
      owner_guid = grand_guid;
    }
  }

  std::uint32_t title_index = 0u;
  if (dbc != nullptr) {
    if (const auto *const spell = dbc->spell().LookupEntry(
            unit.GetUInt32(UNIT_CREATED_BY_SPELL))) {
      for (std::size_t i = 0; i < spell->effect.size(); ++i) {
        if (spell->effect[i] != kSpellEffectSummon) {
          continue;
        }
        if (const auto *const props = dbc->summon_properties().LookupEntry(
                static_cast<std::uint32_t>(spell->effect_misc_value_b[i]))) {

          title_index = props->type;
        }
        break;
      }
    }
  }
  char token[40] = {};
  if (title_index != 0u) {
    std::snprintf(token, sizeof(token), "UNITNAME_SUMMON_TITLE%u",
                  title_index);
  } else if (creature_type == CreatureTypeId::kBeast) {
    std::snprintf(token, sizeof(token), "UNITNAME_SUMMON_TITLE1");
  } else {
    std::snprintf(token, sizeof(token), "UNITNAME_SUMMON_TITLE3");
  }
  const auto format = LookupGlobalString(
      token,
      creature_type == CreatureTypeId::kBeast ? "%s's Pet" : "%s's Minion");
  if (format.empty()) {
    return {};
  }

  std::string owner_name;
  if (const auto *const owner_obj = objects.Get(owner_guid);
      owner_obj != nullptr && owner_obj->IsUnit()) {
    owner_name = static_cast<const CGUnit_C *>(owner_obj)
                     ->ResolveRetailName(world_session);
  }
  if (owner_name.empty()) {
    return {};
  }
  char line[256] = {};
  FormatRuntimeStringTemplateInto(line, sizeof(line), format.c_str(),
                                  owner_name.c_str());
  return line;
}

}

bool UnitName_ShouldRender(
    const CGUnit_C &unit, const CGUnit_C &viewer, const ObjectManager &objects,
    const std::uint32_t display_flags, const bool ui_visible,
    const bool unit_has_nameplate, const std::uint64_t target_guid,
    const std::uint64_t active_mover_guid, UnitNameViewerRelation &relation) {

  if ((unit.State().GetUnitFlags() & kUnitFlagNotSelectable) != 0u &&
      (unit.State().GetCreatedBy() != viewer.GetGuid() ||
       unit.GetGuid() != viewer.State().GetPetGUID())) {
    return false;
  }

  if (ui_visible && unit_has_nameplate) {
    return false;
  }

  const auto raw_guid = unit.GetGuid().GetRawValue();

  if (unit.GetGuid() == viewer.GetGuid() || raw_guid == active_mover_guid ||
      unit.State().GetCharmedBy() == viewer.GetGuid()) {
    return (display_flags & UnitNameFlag::kOwn) != 0u;
  }

  if (raw_guid == target_guid && target_guid != 0u) {
    return true;
  }

  if (unit.IsPlayer()) {
    if (IsFriendlyByFactionGroup(viewer, unit, &relation)) {
      return (display_flags & UnitNameFlag::kFriendlyPlayer) != 0u;
    }
    return (display_flags & UnitNameFlag::kEnemyPlayer) != 0u &&
           (unit.State().GetVisFlags() & kUnitVisFlagCreep) == 0u;
  }

  const auto *const owner = ResolveOwnerUnit(unit, objects);
  const bool hostile = ViewerCanAttackUnit(viewer, unit, relation);

  const auto creature_type = unit.State().GetCreatureType();

  if (owner == nullptr || !owner->IsPlayer()) {

    if ((unit.State().GetVisFlags() & kUnitVisFlagCreep) != 0u && hostile) {
      return false;
    }
    if (creature_type == CreatureTypeId::kCritter ||
        creature_type == CreatureTypeId::kNonCombatPet) {
      return (display_flags & UnitNameFlag::kNonCombatCreature) != 0u;
    }
    return (display_flags & UnitNameFlag::kNPC) != 0u;
  }

  if (hostile && (unit.State().GetVisFlags() & kUnitVisFlagCreep) != 0u) {
    return false;
  }

  if (!unit.State().GetCreatedBy().IsEmpty() &&
      unit.State().GetSummonedBy().IsEmpty() &&
      (unit.State().GetUnitFlags() & kUnitFlagImmuneToNpc) != 0u) {
    return (display_flags & UnitNameFlag::kNonCombatCreature) != 0u;
  }
  if (creature_type == CreatureTypeId::kCritter ||
      creature_type == CreatureTypeId::kNonCombatPet) {
    return (display_flags & UnitNameFlag::kNonCombatCreature) != 0u;
  }
  if (creature_type == CreatureTypeId::kTotem) {
    return (display_flags & (hostile ? UnitNameFlag::kEnemyTotem
                                     : UnitNameFlag::kFriendlyTotem)) != 0u;
  }

  if (unit.State().GetCharmedBy().IsEmpty() &&
      unit.State().GetSummonedBy().IsEmpty() &&
      !unit.State().GetCreatedBy().IsEmpty()) {
    return (display_flags & UnitNameFlag::kEnemyGuardian) != 0u;
  }

  if (IsFriendlyByFactionGroup(viewer, unit, &relation)) {
    return (display_flags & UnitNameFlag::kFriendlyPet) != 0u;
  }
  const auto *const charmer = objects.GetUnit(unit.State().GetCharmedBy());
  if (charmer != nullptr &&
      charmer->State().GetPrimaryControlledUnitGUID() == unit.GetGuid() &&
      (charmer->GetGuid() == viewer.GetGuid() ||
       IsFriendlyByFactionGroup(viewer, *charmer, nullptr))) {
    return (display_flags & UnitNameFlag::kFriendlyPet) != 0u;
  }
  return (display_flags & UnitNameFlag::kEnemyPet) != 0u;
}

UnitNameText UnitName_BuildText(
    const CGUnit_C &unit, const ObjectManager &objects,
    WorldSession &world_session, const std::uint32_t display_flags,
    const CreatureTemplateInfo *const creature_template,
    const data::dbc::DbcLoader *const dbc) {
  UnitNameText result;

  std::string prefix;
  if (unit.IsPlayer()) {
    const auto player_flags = unit.GetUInt32(PLAYER_FLAGS);
    if ((player_flags & PlayerFlagBits::kAFK) != 0u) {
      prefix += LookupGlobalString("CHAT_FLAG_AFK", "<Away>");
    }
    if ((player_flags & PlayerFlagBits::kDND) != 0u) {
      prefix += LookupGlobalString("CHAT_FLAG_DND", "<Busy>");
    }

    if ((player_flags & PlayerFlagBits::kGM) != 0u &&
        (player_flags & PlayerFlagBits::kHiddenGM) == 0u) {
      prefix += LookupGlobalString("CHAT_FLAG_GM", "<GM>");
    }
  }

  std::string name_block;
  result.lines = BuildPlayerNameWithTitle(
      unit, (display_flags & UnitNameFlag::kPlayerPVPTitle) != 0u,
      world_session, creature_template, name_block);
  result.text = prefix + name_block;

  if (unit.IsPlayer()) {

    if ((display_flags & UnitNameFlag::kPlayerGuild) != 0u) {
      const auto &player = static_cast<const CGPlayer_C &>(unit);
      if (const auto *const guild = world_session.guild().FindCachedGuildInfo(
              player.GetGuildID());
          guild != nullptr && !guild->name.empty()) {
        result.text.append("\n<").append(guild->name).append(">");
        ++result.lines;
      }
    }
    return result;
  }

  if (creature_template != nullptr && !creature_template->sub_name.empty() &&
      unit.State().GetPetNumber() == 0u) {
    result.text.append("\n<").append(creature_template->sub_name).append(">");
    ++result.lines;
  }
  const auto owner_line = BuildOwnerLine(unit, objects, world_session,
                                         unit.State().GetCreatureType(), dbc);
  if (!owner_line.empty()) {
    result.text.append("\n<").append(owner_line).append(">");
    ++result.lines;
  }
  return result;
}

std::uint32_t UnitName_ResolveColor(const CGUnit_C &unit,
                                    const CGUnit_C &viewer,
                                    const WorldSession *const world_session,
                                    const std::uint64_t target_guid,
                                    const std::uint32_t now_ms,
                                    UnitNameViewerRelation &relation) {

  if (target_guid != 0u && unit.GetGuid().GetRawValue() == target_guid &&
      viewer.Interaction().IsAutoAttacking() &&
      ViewerCanAttackUnit(viewer, unit, relation)) {
    return ComputeAttackPulseColor(now_ms);
  }

  if ((unit.State().GetUnitFlags() & kUnitFlagPlayerControlled) == 0u) {

    if (static_cast<std::int32_t>(unit.State().GetHealth()) < 1 ||
        (unit.State().GetDynamicFlags() & kDynFlagDead) != 0u) {
      return kColorDeadUnitName;
    }
    const auto reaction =
        static_cast<std::size_t>(unit.Interaction().GetReaction(viewer));
    return kReactionNameColors[std::min(reaction,
                                        kReactionNameColors.size() - 1u)];
  }

  const bool unit_attacks_viewer =
      unit.Interaction().CanAttackSpellTarget(viewer);
  const bool viewer_attacks_unit = ViewerCanAttackUnit(viewer, unit, relation);
  if (unit_attacks_viewer) {
    return viewer_attacks_unit ? kColorHostilePlayer : kColorDefaultPlayerName;
  }
  if (viewer_attacks_unit) {
    return kColorAttackablePlayer;
  }

  const auto *const session = world_session;
  bool pvp_highlight =
      (unit.State().GetPvPFlags() & kPvpFlagPvP) != 0u &&
      (unit.State().GetPvPFlags() & kPvpFlagSanctuary) == 0u &&
      (viewer.State().GetPvPFlags() & kPvpFlagSanctuary) == 0u;
  if (pvp_highlight && session != nullptr) {
    const auto *const map = session->LookupMapEntry(session->current_map_id());
    if (map != nullptr &&
        map->map_type ==
            static_cast<std::uint32_t>(data::dbc::MapType::kArena)) {
      pvp_highlight = false;
    }
  }

  const bool self_or_minion =
      unit.GetGuid() == viewer.GetGuid() ||
      unit.State().GetCharmedBy() == viewer.GetGuid() ||
      unit.State().GetCreatedBy() == viewer.GetGuid() ||
      unit.State().GetSummonedBy() == viewer.GetGuid();
  if (self_or_minion && session != nullptr &&
      session->bg_instance().IsActive()) {
    return pvp_highlight ? kColorOwnBattlefieldPvp : kColorOwnBattlefieldNoPvp;
  }

  if (GroupSystem::Get().GetMemberByGuid(unit.GetGuid().GetRawValue()) !=
      nullptr) {
    return kColorGroupMember;
  }
  if (pvp_highlight) {
    const auto reaction =
        static_cast<std::size_t>(unit.Interaction().GetReaction(viewer));
    return kReactionNameColors[std::min(reaction,
                                        kReactionNameColors.size() - 1u)];
  }
  return kColorDefaultPlayerName;
}

float UnitName_ComputeScale(const float height) {

  if (height > 4.0f) {
    return height * 0.25f * 1.5f;
  }
  return 1.0f;
}

}
