#include "openwow/game/session/unit_token_resolver.h"

#include "openwow/core/storm_string.h"
#include "openwow/game/group_system.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/instance_handler.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/update_fields.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {
namespace {

bool EqualName(const std::string_view left, const std::string_view right) {
  const std::string lhs(left);
  const std::string rhs(right);
  return openwow::core::SStrCmpUTF8NoCase(
             lhs.c_str(), rhs.c_str(), 0x7fffffffu) == 0;
}

std::string Lower(const std::string_view text) {
  std::string result(text);
  std::transform(
      result.begin(), result.end(), result.begin(),
      [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
      });
  return result;
}

std::optional<int> IndexedToken(
    const std::string_view token, const std::string_view prefix,
    const int maximum) {
  if (!token.starts_with(prefix)) return std::nullopt;
  const auto suffix = token.substr(prefix.size());
  int index = 0;
  const auto [end, error] =
      std::from_chars(suffix.data(), suffix.data() + suffix.size(), index);
  if (error != std::errc{} || end != suffix.data() + suffix.size() ||
      index < 1 || index > maximum) {
    return std::nullopt;
  }
  return index;
}

ObjectGuid ControlledUnit(
    const ObjectManager& objects, const ObjectGuid owner,
    const std::uint64_t cached_pet = 0) {
  if (const auto* unit = objects.GetUnit(owner); unit != nullptr) {
    const auto controlled = unit->State().GetPrimaryControlledUnitGUID();
    if (!controlled.IsEmpty()) return controlled;
  }
  return ObjectGuid(cached_pet);
}

ObjectGuid MemberByName(
    const ObjectManager& objects, const GroupSystem& group,
    const std::string_view requested) {
  const bool realm_qualified = requested.find('-') != std::string_view::npos;
  const auto bare = requested.substr(0, requested.find('-'));
  for (std::size_t index = 0; index < group.GetNumGroupMembers(); ++index) {
    const auto* member = group.GetMember(index);
    if (member == nullptr || member->guid == 0) continue;
    if (EqualName(member->name, realm_qualified ? bare : requested)) {
      return ObjectGuid(member->guid);
    }
    const auto pet = ControlledUnit(
        objects, ObjectGuid(member->guid), member->pet_guid);
    if (const auto* unit = objects.GetUnit(pet);
        unit != nullptr &&
        EqualName(unit->GetName(), realm_qualified ? bare : requested)) {
      return pet;
    }
  }
  return {};
}

ObjectGuid ResolveBase(
    ObjectManager& objects, const GroupSystem& group,
    const BattlefieldInfo& battlefield, const InstanceHandler& instance,
    const std::string_view token) {
  const auto* player = objects.GetLocalPlayerTyped();

  if (token == "none") return {};
  if (token == "player") return objects.GetActivePlayerGuid();
  if (token == "target") return objects.GetTargetGuid();
  if (token == "focus") return objects.GetFocusTargetGuid();
  if (token == "mouseover") return objects.GetMouseoverGuid();
  if (token == "pet") {
    return player != nullptr
               ? ControlledUnit(objects, player->GetGuid())
               : ObjectGuid{};
  }
  if (token == "vehicle") {
    return player != nullptr && player->Vehicle().HasAttachedVehiclePassenger() &&
                   player->Vehicle().GetVehicleUnit() != nullptr
               ? player->Vehicle().GetVehicleUnit()->GetGuid()
               : ObjectGuid{};
  }

  if (const auto index = IndexedToken(token, "party", 4);
      index.has_value()) {
    return ObjectGuid(group.GetTrackedPartyMemberGuid(*index - 1));
  }
  if (const auto index = IndexedToken(token, "partypet", 4);
      index.has_value()) {
    const auto owner =
        ObjectGuid(group.GetTrackedPartyMemberGuid(*index - 1));
    const auto* member = group.GetMemberByGuid(owner.GetRawValue());
    return ControlledUnit(
        objects, owner, member != nullptr ? member->pet_guid : 0);
  }
  if (const auto index = IndexedToken(token, "raid", 40);
      index.has_value()) {
    const auto* member = group.GetMember(*index - 1);
    return member != nullptr ? ObjectGuid(member->guid) : ObjectGuid{};
  }
  if (const auto index = IndexedToken(token, "raidpet", 40);
      index.has_value()) {
    const auto* member = group.GetMember(*index - 1);
    return member != nullptr
               ? ControlledUnit(
                     objects, ObjectGuid(member->guid), member->pet_guid)
               : ObjectGuid{};
  }
  if (const auto index = IndexedToken(token, "arena", 5);
      index.has_value()) {
    return static_cast<std::size_t>(*index) <=
                   battlefield.GetArenaOpponentSlotCount()
               ? battlefield
                     .GetArenaOpponent(static_cast<std::size_t>(*index - 1))
                     .guid
               : ObjectGuid{};
  }
  if (const auto index = IndexedToken(token, "arenapet", 5);
      index.has_value()) {
    if (static_cast<std::size_t>(*index) >
        battlefield.GetArenaOpponentSlotCount()) {
      return {};
    }
    const auto opponent = battlefield.GetArenaOpponent(*index - 1);
    return ControlledUnit(objects, opponent.guid, opponent.pet_guid.GetRawValue());
  }
  if (const auto index = IndexedToken(token, "boss", 64);
      index.has_value()) {
    return instance.encounter_unit_guid(*index - 1);
  }

  if (const auto member = MemberByName(objects, group, token);
      !member.IsEmpty()) {
    return member;
  }
  if (const auto active = objects.GetActivePlayerGuid(); !active.IsEmpty() &&
      EqualName(objects.GetPlayerName(active), token)) {
    return active;
  }
  if (player != nullptr) {
    const auto pet = ControlledUnit(objects, player->GetGuid());
    if (const auto* unit = objects.GetUnit(pet);
        unit != nullptr && EqualName(unit->GetName(), token)) {
      return pet;
    }
  }
  for (std::size_t index = 0;
       index < battlefield.GetArenaOpponentSlotCount(); ++index) {
    const auto opponent = battlefield.GetArenaOpponent(index);
    if (EqualName(objects.GetPlayerName(opponent.guid), token)) {
      return opponent.guid;
    }
    if (const auto* pet = objects.GetUnit(opponent.pet_guid);
        pet != nullptr && EqualName(pet->GetName(), token)) {
      return opponent.pet_guid;
    }
  }
  return {};
}

}

ObjectGuid ResolveUnitToken(
    ObjectManager& objects, const GroupSystem& group,
    const BattlefieldInfo& battlefield, const InstanceHandler& instance,
    const std::string_view token) {
  std::string base = Lower(token);
  std::vector<bool> target_suffixes;
  while (base.size() > 3) {
    if (base.ends_with("target")) {
      target_suffixes.push_back(true);
      base.resize(base.size() - 6);
    } else if (base.ends_with("pet")) {
      target_suffixes.push_back(false);
      base.resize(base.size() - 3);
    } else {
      break;
    }
  }
  std::reverse(target_suffixes.begin(), target_suffixes.end());

  auto guid = ResolveBase(objects, group, battlefield, instance, base);
  for (const bool target : target_suffixes) {
    const auto* unit = objects.GetUnit(guid);
    if (unit == nullptr) return {};
    guid = target ? unit->GetGuidField(UNIT_FIELD_TARGET)
                  : ControlledUnit(objects, guid);
  }
  return guid;
}

}
