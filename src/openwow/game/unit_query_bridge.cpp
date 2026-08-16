
#include "openwow/game/unit_query_bridge.h"
#include "openwow/game/commentator_state.h"
#include "openwow/game/group_system.h"
#include "openwow/game/world_session.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/unit_defines.h"
#include "openwow/game/update_fields.h"
#include "openwow/core/storm_string.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <string>

namespace openwow::game {

namespace {

constexpr std::uint32_t kPlayerFlags2Spectator = 0x00080000u;

}

UnitQueryBridge& UnitQueryBridge::Get() {
  static UnitQueryBridge instance;
  return instance;
}

static std::string ToLower(std::string_view sv) {
  std::string s(sv);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

static int ParseIndex(std::string_view sv) {
  if (sv.empty()) return -1;
  std::uint64_t val = 0;
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
  if (ec != std::errc{} || ptr != sv.data() + sv.size()) return -1;
  return static_cast<int>(static_cast<std::uint32_t>(val));
}

static bool UnitNameEquals(std::string_view lhs, std::string_view rhs) {
  const std::string lhs_string(lhs);
  const std::string rhs_string(rhs);
  return openwow::core::SStrCmpUTF8NoCase(
             lhs_string.c_str(), rhs_string.c_str(), 0x7FFFFFFFu) == 0;
}

static ObjectGuid ResolveTrackedPartyGuid(const int index) {
  if (index < 1 || index > 4) return ObjectGuid();

  return ObjectGuid(
      GroupSystem::Get().GetTrackedPartyMemberGuid(static_cast<std::uint32_t>(index - 1)));
}

static ObjectGuid ResolveControlledUnitGuid(const ObjectManager& objects,
                                            const std::uint64_t member_guid,
                                            const std::uint64_t cached_pet_guid) {
  if (member_guid != 0) {
    if (const auto* unit = objects.GetUnit(ObjectGuid(member_guid))) {
      const auto controlled_guid = unit->State().GetPrimaryControlledUnitGUID();
      if (!controlled_guid.IsEmpty()) {
        return controlled_guid;
      }
    }
  }

  return ObjectGuid(cached_pet_guid);
}

static ObjectGuid ResolveTrackedPartyControlledUnitGuid(
    const ObjectManager& objects, const int index) {
  const auto party_guid = ResolveTrackedPartyGuid(index);
  if (party_guid.IsEmpty()) {
    return ObjectGuid();
  }

  const auto* member = GroupSystem::Get().GetMemberByGuid(party_guid.GetRawValue());
  const auto cached_pet_guid = member != nullptr ? member->pet_guid : 0;
  return ResolveControlledUnitGuid(
      objects, party_guid.GetRawValue(), cached_pet_guid);
}

static ObjectGuid ResolveTrackedRaidGuid(const int index) {
  if (index < 1 || index > 40) {
    return ObjectGuid();
  }

  auto& group_system = GroupSystem::Get();
  if (!group_system.IsInRaid()) {
    return ObjectGuid();
  }

  const auto* member =
      group_system.GetMember(static_cast<std::size_t>(index - 1));
  if (member == nullptr) {
    return ObjectGuid();
  }

  return ObjectGuid(member->guid);
}

static ObjectGuid ResolveTrackedRaidControlledUnitGuid(
    const ObjectManager& objects, const int index) {
  if (index < 1 || index > 40) {
    return ObjectGuid();
  }

  auto& group_system = GroupSystem::Get();
  if (!group_system.IsInRaid()) {
    return ObjectGuid();
  }

  const auto* member =
      group_system.GetMember(static_cast<std::size_t>(index - 1));
  if (member == nullptr) {
    return ObjectGuid();
  }

  return ResolveControlledUnitGuid(objects, member->guid, member->pet_guid);
}

static ObjectGuid ResolveTrackedGroupMemberGuidByName(std::string_view name) {
  if (name.empty()) {
    return ObjectGuid();
  }

  auto& group_system = GroupSystem::Get();
  for (const auto& member : group_system.GetMembers()) {
    if (!member.guid.IsEmpty() && UnitNameEquals(member.name, name)) {
      return member.guid;
    }
  }

  for (std::size_t index = 0; index < group_system.GetNumGroupMembers(); ++index) {
    const auto* member = group_system.GetMember(index);
    if (member != nullptr && member->guid != 0 && UnitNameEquals(member->name, name)) {
      return ObjectGuid(member->guid);
    }
  }

  return ObjectGuid();
}

static ObjectGuid ResolveCachedUnitName(WorldSession& session, std::string_view name) {
  if (name.empty()) {
    return ObjectGuid();
  }

  if (const auto cached_guid = session.query_cache().FindPlayerGuidByName(name);
      cached_guid.has_value()) {
    return *cached_guid;
  }

  const auto dash = name.find('-');
  if (dash != std::string_view::npos && dash > 0) {
    const auto bare_name = name.substr(0, dash);
    if (const auto cached_guid = session.query_cache().FindPlayerGuidByName(bare_name);
        cached_guid.has_value()) {
      return *cached_guid;
    }
    if (const auto group_guid = ResolveTrackedGroupMemberGuidByName(bare_name);
        !group_guid.IsEmpty()) {
      return group_guid;
    }
  }

  return ResolveTrackedGroupMemberGuidByName(name);
}

static void MergeTrackedGroupMemberSnapshot(
    UnitQuerySnapshot& snap, const std::string& name, const std::uint8_t class_id,
    const bool online, const bool dead) {
  if (!name.empty() && snap.name.empty()) {
    snap.name = name;
  }
  if (class_id != 0 && snap.classId == 0) {
    snap.classId = class_id;
  }
  snap.isConnected = online;
  snap.isDead = snap.isDead || dead;
}

static std::optional<UnitQuerySnapshot> GetTrackedGroupMemberSnapshot(
    WorldSession* session, const ObjectGuid guid) {
  if (session == nullptr || guid.IsEmpty()) {
    return std::nullopt;
  }

  UnitQuerySnapshot snap;
  snap.guid = guid;
  snap.isPlayer = true;
  bool found = false;

  if (const auto member = GroupSystem::Get().GetMember(guid);
      member.has_value() && member->online) {
    MergeTrackedGroupMemberSnapshot(
        snap, member->name, member->classId, member->online, member->dead);
    found = true;
  }

  if (const auto* legacy_member =
          GroupSystem::Get().GetMemberByGuid(guid.GetRawValue());
      legacy_member != nullptr && legacy_member->is_online) {
    MergeTrackedGroupMemberSnapshot(
        snap, legacy_member->name, legacy_member->class_id,
        legacy_member->is_online, false);
    found = true;
  }

  if (const auto* roster_member = session->group().GetMember(guid);
      roster_member != nullptr && (roster_member->online_status & 0x01) != 0) {
    MergeTrackedGroupMemberSnapshot(
        snap, roster_member->name, 0,
        (roster_member->online_status & 0x01) != 0, false);
    found = true;
  }

  if (!found) {
    return std::nullopt;
  }

  return snap;
}

ParsedUnitId ParseUnitId(std::string_view raw) {
  if (raw.empty()) return {UnitIdKind::kUnknown, 0};
  std::string uid = ToLower(raw);

  if (uid == "player")        return {UnitIdKind::kPlayer, 0};
  if (uid == "target")        return {UnitIdKind::kTarget, 0};
  if (uid == "focus")         return {UnitIdKind::kFocus, 0};
  if (uid == "pet")           return {UnitIdKind::kPet, 0};
  if (uid == "playerpet")     return {UnitIdKind::kPlayerPet, 0};
  if (uid == "mouseover")     return {UnitIdKind::kMouseover, 0};
  if (uid == "vehicle")       return {UnitIdKind::kVehicle, 0};
  if (uid == "none")         return {UnitIdKind::kNone, 0};
  if (uid == "npc")          return {UnitIdKind::kNpc, 0};
  if (uid == "questnpc")     return {UnitIdKind::kQuestNpc, 0};
  if (uid == "targettarget")  return {UnitIdKind::kTargetTarget, 0};
  if (uid == "focustarget")   return {UnitIdKind::kFocusTarget, 0};
  if (uid == "pettarget")     return {UnitIdKind::kPetTarget, 0};

  auto tryPrefixIndex = [&](std::string_view prefix, UnitIdKind kind,
                            UnitIdKind targetKind = UnitIdKind::kUnknown,
                            UnitIdKind petKind = UnitIdKind::kUnknown)
      -> std::optional<ParsedUnitId> {
    if (uid.size() <= prefix.size()) return std::nullopt;
    if (uid.compare(0, prefix.size(), prefix) != 0) return std::nullopt;
    auto rest = std::string_view(uid).substr(prefix.size());

    std::size_t digit_end = 0;
    while (digit_end < rest.size() && std::isdigit(static_cast<unsigned char>(rest[digit_end])))
      ++digit_end;
    if (digit_end == 0) return std::nullopt;

    int idx = ParseIndex(rest.substr(0, digit_end));
    if (idx < 0) return std::nullopt;

    auto suffix = rest.substr(digit_end);
    if (suffix.empty()) return ParsedUnitId{kind, idx};
    if (suffix == "target" && targetKind != UnitIdKind::kUnknown)
      return ParsedUnitId{targetKind, idx};
    if (suffix == "pet" && petKind != UnitIdKind::kUnknown)
      return ParsedUnitId{petKind, idx};
    return std::nullopt;
  };

  if (auto r = tryPrefixIndex("party", UnitIdKind::kParty,
                               UnitIdKind::kPartyTarget)) return *r;
  if (auto r = tryPrefixIndex("partypet", UnitIdKind::kPartyPet)) return *r;

  if (auto r = tryPrefixIndex("raid", UnitIdKind::kRaid,
                               UnitIdKind::kRaidTarget)) return *r;
  if (auto r = tryPrefixIndex("raidpet", UnitIdKind::kRaidPet)) return *r;

  if (auto r = tryPrefixIndex("arena", UnitIdKind::kArena)) return *r;
  if (auto r = tryPrefixIndex("arenapet", UnitIdKind::kArenaPet)) return *r;

  if (auto r = tryPrefixIndex("boss", UnitIdKind::kBoss)) return *r;

  if (auto r = tryPrefixIndex("commentator", UnitIdKind::kCommentator)) return *r;

  return {UnitIdKind::kUnknown, 0};
}

ObjectGuid UnitQueryBridge::ResolveToGuid(
    WorldSession* session, std::string_view unit_id) const {
  if (!session) return ObjectGuid();

  std::string base_token(unit_id);
  std::string lowered = ToLower(base_token);
  std::vector<std::string_view> suffix_chain;
  while (true) {
    if (lowered.size() > 6 && lowered.compare(lowered.size() - 6, 6, "target") == 0) {
      suffix_chain.push_back("target");
      lowered = lowered.substr(0, lowered.size() - 6);
      base_token.resize(base_token.size() - 6);
    } else if (lowered.size() > 3 && lowered.compare(lowered.size() - 3, 3, "pet") == 0) {
      suffix_chain.push_back("pet");
      lowered = lowered.substr(0, lowered.size() - 3);
      base_token.resize(base_token.size() - 3);
    } else {
      break;
    }
  }
  while (!base_token.empty() && (base_token.back() == '-' || base_token.back() == ' ')) {
    base_token.pop_back();
    lowered.pop_back();
  }

  std::reverse(suffix_chain.begin(), suffix_chain.end());

  auto parsed = ParseUnitId(lowered);
  auto& objects = session->objects();
  const auto* player = static_cast<const CGUnit_C*>(objects.GetLocalPlayer());

  auto resolve_base = [&]() -> ObjectGuid {
    switch (parsed.kind) {
    case UnitIdKind::kPlayer:
      return objects.GetLocalPlayerGuid();

    case UnitIdKind::kTarget: {
      return objects.GetTargetGuid();
    }

    case UnitIdKind::kFocus:
      return objects.GetFocusTargetGuid();

    case UnitIdKind::kPet:
    case UnitIdKind::kPlayerPet: {
      if (!player) return ObjectGuid();
      auto summon = player->GetGuidField(UNIT_FIELD_SUMMON);
      if (!summon.IsEmpty()) return summon;
      return player->GetGuidField(UNIT_FIELD_CHARM);
    }

    case UnitIdKind::kMouseover:

      return objects.GetMouseoverGuid();

    case UnitIdKind::kVehicle:
      if (!player || !player->Vehicle().HasAttachedVehiclePassenger()) {
        return ObjectGuid();
      }
      if (const auto* vehicle = player->Vehicle().GetVehicleUnit(); vehicle != nullptr) {
        return vehicle->GetGuid();
      }
      return ObjectGuid();

    case UnitIdKind::kTargetTarget: {
      auto target_guid = objects.GetTargetGuid();
      return ResolveRelativeTarget(session, target_guid);
    }

    case UnitIdKind::kFocusTarget: {
      auto focus_guid = objects.GetFocusTargetGuid();
      return ResolveRelativeTarget(session, focus_guid);
    }

    case UnitIdKind::kPetTarget: {
      if (!player) return ObjectGuid();
      auto pet_guid = player->GetGuidField(UNIT_FIELD_SUMMON);
      return ResolveRelativeTarget(session, pet_guid);
    }

    case UnitIdKind::kParty: {
      return ResolveTrackedPartyGuid(parsed.index);
    }

    case UnitIdKind::kPartyTarget: {

      return ResolveRelativeTarget(session, ResolveTrackedPartyGuid(parsed.index));
    }

    case UnitIdKind::kPartyPet: {
      return ResolveTrackedPartyControlledUnitGuid(objects, parsed.index);
    }

    case UnitIdKind::kRaid: {
      return ResolveTrackedRaidGuid(parsed.index);
    }

    case UnitIdKind::kRaidTarget: {
      return ResolveRelativeTarget(session, ResolveTrackedRaidGuid(parsed.index));
    }

    case UnitIdKind::kRaidPet: {
      return ResolveTrackedRaidControlledUnitGuid(objects, parsed.index);
    }

    case UnitIdKind::kArena: {
      int idx = parsed.index;
      if (idx < 1 || idx > 5) return ObjectGuid();
      if (static_cast<std::size_t>(idx) > session->battleground().GetArenaOpponentSlotCount()) {
        return ObjectGuid();
      }
      const auto tracked = session->battleground().GetArenaOpponent(
          static_cast<std::size_t>(idx - 1));
      return tracked.guid;
    }

    case UnitIdKind::kArenaPet: {
      int idx = parsed.index;
      if (idx < 1 || idx > 5) return ObjectGuid();
      if (static_cast<std::size_t>(idx) > session->battleground().GetArenaOpponentSlotCount()) {
        return ObjectGuid();
      }
      const auto tracked = session->battleground().GetArenaOpponent(
          static_cast<std::size_t>(idx - 1));
      if (!tracked.guid.IsEmpty()) {
        if (const auto* obj = objects.Get(tracked.guid)) {

          const auto charm_guid = obj->GetGuidField(UNIT_FIELD_CHARM);
          if (!charm_guid.IsEmpty()) {
            return charm_guid;
          }
          const auto live_pet_guid = obj->GetGuidField(UNIT_FIELD_SUMMON);
          if (!live_pet_guid.IsEmpty()) {
            return live_pet_guid;
          }
        }
      }
      return tracked.pet_guid;
    }

    case UnitIdKind::kBoss: {
      int idx = parsed.index;
      if (idx < 1) return ObjectGuid();
      return session->instance().encounter_unit_guid(
          static_cast<std::size_t>(idx - 1));
    }

    case UnitIdKind::kCommentator: {
      if (parsed.index <= 0 || player == nullptr ||
          (player->State().GetUnitFlags2() & kPlayerFlags2Spectator) == 0u) {
        return ObjectGuid();
      }

      return CommentatorState::Get().GetSelectedPlayerGuidByTokenIndex(
          static_cast<std::size_t>(parsed.index - 1));
    }

    case UnitIdKind::kNone:

      return ObjectGuid(std::numeric_limits<std::uint64_t>::max());

    case UnitIdKind::kNpc:
      return objects.GetNpcGuid();

    case UnitIdKind::kQuestNpc: {

      const auto& interaction =
          session->quests().quest_frame_interaction_state();
      return interaction.secondary_guid.GetRawValue() != 0
                 ? interaction.secondary_guid
                 : interaction.interaction_guid;
    }

    case UnitIdKind::kUnknown:
      return ObjectGuid();
  }
    return ObjectGuid();
  };

  ObjectGuid guid = resolve_base();
  if (guid.IsEmpty()) {
    guid = ResolveCachedUnitName(*session, base_token);
  }
  for (const auto& suffix : suffix_chain) {
    guid = ResolveChainedSuffix(session, guid, suffix);
  }
  return guid;
}

ObjectGuid UnitQueryBridge::ResolveChainedSuffix(
    WorldSession* session, ObjectGuid base_guid,
    std::string_view suffix) const {
  if (base_guid.IsEmpty() || !session) return ObjectGuid();
  auto& objects = session->objects();
  const auto local_player_guid = objects.GetLocalPlayerGuid();
  const auto* local_player = objects.GetLocalPlayerTyped();
  const auto* obj = session->objects().Get(base_guid);
  if (!obj) return ObjectGuid();
  if (suffix == "target") {
    if (base_guid == local_player_guid) {
      return objects.GetTargetGuid();
    }
    return obj->GetGuidField(UNIT_FIELD_TARGET);
  }
  if (suffix == "pet") {
    if (base_guid != local_player_guid &&
        (local_player == nullptr ||
         (local_player->State().GetUnitFlags2() & kPlayerFlags2Spectator) == 0u)) {
      return ObjectGuid();
    }

    auto charm = obj->GetGuidField(UNIT_FIELD_CHARM);
    if (!charm.IsEmpty()) return charm;
    return obj->GetGuidField(UNIT_FIELD_SUMMON);
  }
  return ObjectGuid();
}

ObjectGuid UnitQueryBridge::ResolveRelativeTarget(
    WorldSession* session, ObjectGuid base_guid) const {
  if (base_guid.IsEmpty() || !session) return ObjectGuid();
  const auto* obj = session->objects().Get(base_guid);
  if (!obj) return ObjectGuid();
  return obj->GetGuidField(UNIT_FIELD_TARGET);
}

UnitQuerySnapshot UnitQueryBridge::SnapshotFromObject(
    const CGObject_C* obj) {
  UnitQuerySnapshot snap;
  if (!obj) return snap;

  snap.guid = obj->GetGuid();
  snap.name = obj->GetName();
  snap.level = obj->GetLevel();
  snap.health = obj->GetHealth();
  snap.maxHealth = obj->GetMaxHealth();
  snap.isPlayer = obj->IsPlayer();

  std::uint32_t bytes0 = obj->GetUInt32(UNIT_FIELD_BYTES_0);
  snap.raceId    = static_cast<std::uint8_t>(bytes0 & 0xFF);
  snap.classId   = static_cast<std::uint8_t>((bytes0 >> 8) & 0xFF);
  snap.genderId  = static_cast<std::uint8_t>((bytes0 >> 16) & 0xFF);
  snap.powerType = (bytes0 >> 24) & 0xFF;

  auto pt_offset = static_cast<std::uint16_t>(UNIT_FIELD_POWER1 + snap.powerType);
  auto max_pt_offset = static_cast<std::uint16_t>(UNIT_FIELD_MAXPOWER1 + snap.powerType);
  if (snap.powerType <= 6) {
    snap.power = obj->GetUInt32(pt_offset);
    snap.maxPower = obj->GetUInt32(max_pt_offset);
  }

  snap.factionTemplate = obj->GetUInt32(UNIT_FIELD_FACTIONTEMPLATE);

  snap.inCombat =
      (obj->GetUInt32(UNIT_FIELD_FLAGS) &
       static_cast<std::uint32_t>(UnitStateFlag::kInCombat)) != 0;

  snap.isDead = (snap.health == 0);

  snap.isGhost = obj->IsPlayer() && (obj->GetUInt32(PLAYER_FLAGS) & 0x10) != 0;

  snap.x = obj->GetX();
  snap.y = obj->GetY();
  snap.z = obj->GetZ();

  return snap;
}

std::optional<UnitQuerySnapshot> UnitQueryBridge::Query(
    WorldSession* session, std::string_view unit_id) const {
  auto guid = ResolveToGuid(session, unit_id);
  if (guid.IsEmpty()) return std::nullopt;

  const auto* obj = session->objects().Get(guid);
  if (!obj) {
    return GetPlayerInfoByGUID(session, guid.GetRawValue());
  }

  auto snap = SnapshotFromObject(obj);

  if (snap.name.empty() && session) {
    if (snap.isPlayer) {
      auto cached_name = session->objects().GetPlayerName(guid);
      if (!cached_name.empty()) snap.name = cached_name;
    } else {
      auto entry = obj->GetEntry();
      if (entry != 0) {
        const auto* tmpl = session->query_cache().GetCreatureTemplate(entry);
        if (tmpl) snap.name = tmpl->name;
      }
    }
  }

  return snap;
}

std::optional<UnitQuerySnapshot> UnitQueryBridge::GetPlayerInfoByGUID(
    WorldSession* session, std::uint64_t raw_guid) const {
  if (!session || raw_guid == 0) return std::nullopt;

  ObjectGuid guid(raw_guid);

  const auto* obj = session->objects().Get(guid);
  if (obj) {
    auto snap = SnapshotFromObject(obj);

    if (snap.name.empty() && snap.isPlayer) {
      auto cached_name = session->objects().GetPlayerName(guid);
      if (!cached_name.empty()) snap.name = cached_name;
    }
    return snap;
  }

  if (auto tracked_group_member = GetTrackedGroupMemberSnapshot(session, guid);
      tracked_group_member.has_value()) {
    return tracked_group_member;
  }

  const auto* name_entry = session->objects().GetNameEntry(guid);
  if (!name_entry) return std::nullopt;

  UnitQuerySnapshot snap;
  snap.guid = guid;
  snap.name = name_entry->name;
  snap.raceId = name_entry->race;
  snap.classId = name_entry->class_id;
  snap.genderId = name_entry->gender;
  snap.isPlayer = true;
  snap.isConnected = true;
  return snap;
}

}
