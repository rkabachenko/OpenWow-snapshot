
#include "openwow/ui/game/script_event_dispatch.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/game/vehicle_helpers.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/event_dispatcher.h"
#include "openwow/ui/game/game_events.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game {

using namespace openwow::ui::game::events;

namespace {

constexpr double kVehicleAngleZeroRangeEpsilon = 0.000099999997;

double NormalizeVehicleAngle(double raw_pitch, double min_pitch, double max_pitch) {
  const double range = max_pitch - min_pitch;
  if (std::fabs(range) <= kVehicleAngleZeroRangeEpsilon) {
    return 0.0;
  }

  return (raw_pitch - min_pitch) / range;
}

void AppendTokenIfGuidMatches(
    const std::unordered_map<std::string, std::uint64_t> &token_to_guid,
    std::uint64_t guid,
    const char *token,
    std::vector<std::string> &out) {
  const auto it = token_to_guid.find(token);
  if (it != token_to_guid.end() && it->second == guid) {
    if (std::find(out.begin(), out.end(), it->first) == out.end()) {
      out.push_back(it->first);
    }
  }
}

void AppendIndexedTokensIfGuidMatches(
    const std::unordered_map<std::string, std::uint64_t> &token_to_guid,
    std::uint64_t guid,
    const char *prefix,
    int last_index,
    std::vector<std::string> &out) {
  for (int index = 1; index <= last_index; ++index) {
    const std::string token = std::string(prefix) + std::to_string(index);
    AppendTokenIfGuidMatches(token_to_guid, guid, token.c_str(), out);
  }
}

void AppendUnique(std::vector<std::string> &tokens, std::string token) {
  if (std::find(tokens.begin(), tokens.end(), token) == tokens.end()) {
    tokens.push_back(std::move(token));
  }
}

void AppendTokenIfGuidMatches(const std::uint64_t wanted_guid,
                              const std::uint64_t candidate_guid,
                              const char *token,
                              std::vector<std::string> &out) {
  if (wanted_guid != 0 && candidate_guid == wanted_guid) {
    AppendUnique(out, token);
  }
}

void AppendIndexedTokenIfGuidMatches(const std::uint64_t wanted_guid,
                                     const std::uint64_t candidate_guid,
                                     const char *prefix,
                                     const std::size_t zero_based_index,
                                     std::vector<std::string> &out) {
  if (wanted_guid != 0 && candidate_guid == wanted_guid) {
    AppendUnique(out, std::string(prefix) + std::to_string(zero_based_index + 1));
  }
}

void AppendManualTokenMatches(
    const std::unordered_map<std::string, std::uint64_t> &token_to_guid,
    const std::uint64_t guid,
    std::vector<std::string> &result) {
  AppendTokenIfGuidMatches(token_to_guid, guid, "player", result);
  AppendTokenIfGuidMatches(token_to_guid, guid, "vehicle", result);
  AppendTokenIfGuidMatches(token_to_guid, guid, "pet", result);
  AppendIndexedTokensIfGuidMatches(token_to_guid, guid, "party", 4, result);
  AppendIndexedTokensIfGuidMatches(token_to_guid, guid, "partypet", 4, result);
  AppendIndexedTokensIfGuidMatches(token_to_guid, guid, "raid", 40, result);
  AppendIndexedTokensIfGuidMatches(token_to_guid, guid, "raidpet", 40, result);
  AppendIndexedTokensIfGuidMatches(token_to_guid, guid, "arena", 5, result);
  AppendIndexedTokensIfGuidMatches(token_to_guid, guid, "arenapet", 5, result);
  AppendIndexedTokensIfGuidMatches(token_to_guid, guid, "boss", 16, result);
  AppendIndexedTokensIfGuidMatches(token_to_guid, guid, "commentator", 10, result);
  AppendTokenIfGuidMatches(token_to_guid, guid, "target", result);
  AppendTokenIfGuidMatches(token_to_guid, guid, "focus", result);
  AppendTokenIfGuidMatches(token_to_guid, guid, "npc", result);
  AppendTokenIfGuidMatches(token_to_guid, guid, "mouseover", result);
  AppendTokenIfGuidMatches(token_to_guid, guid, "questnpc", result);
  AppendTokenIfGuidMatches(token_to_guid, guid, "none", result);
}

void AppendManualSupplementalTokenMatches(
    const std::unordered_map<std::string, std::uint64_t> &token_to_guid,
    const std::uint64_t guid,
    std::vector<std::string> &result) {
  AppendIndexedTokensIfGuidMatches(token_to_guid, guid, "boss", 16, result);
  AppendIndexedTokensIfGuidMatches(token_to_guid, guid, "commentator", 10, result);
  AppendTokenIfGuidMatches(token_to_guid, guid, "questnpc", result);
  AppendTokenIfGuidMatches(token_to_guid, guid, "none", result);
}

bool IsSessionOwnedToken(const std::string &token) {
  static constexpr const char *kFixed[] = {
      "player", "vehicle", "pet", "target", "focus", "npc", "mouseover"};
  for (const char *fixed : kFixed) {
    if (token == fixed) {
      return true;
    }
  }

  const auto numeric_suffix_in_range = [](const std::string &value,
                                          const std::string_view prefix,
                                          const int first,
                                          const int last) {
    if (value.size() <= prefix.size() || value.rfind(prefix, 0) != 0) {
      return false;
    }
    try {
      const int index = std::stoi(value.substr(prefix.size()));
      return index >= first && index <= last;
    } catch (...) {
      return false;
    }
  };

  return numeric_suffix_in_range(token, "party", 1, 4) ||
         numeric_suffix_in_range(token, "partypet", 1, 4) ||
         numeric_suffix_in_range(token, "raid", 1, 40) ||
         numeric_suffix_in_range(token, "raidpet", 1, 40) ||
         numeric_suffix_in_range(token, "arena", 1, 5) ||
         numeric_suffix_in_range(token, "arenapet", 1, 5);
}

std::optional<int> IndexedTokenPriority(const std::string &token,
                                        const std::string_view prefix,
                                        const int base,
                                        const int first,
                                        const int last) {
  if (token.size() <= prefix.size() || token.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }

  try {
    const int index = std::stoi(token.substr(prefix.size()));
    if (index < first || index > last) {
      return std::nullopt;
    }
    return base + index;
  } catch (...) {
    return std::nullopt;
  }
}

int UnitTokenPriority(const std::string &token) {
  if (token == "player") return 0;
  if (token == "vehicle") return 1;
  if (token == "pet") return 2;
  if (const auto priority = IndexedTokenPriority(token, "party", 100, 1, 4)) {
    return *priority;
  }
  if (const auto priority = IndexedTokenPriority(token, "partypet", 200, 1, 4)) {
    return *priority;
  }
  if (const auto priority = IndexedTokenPriority(token, "raid", 300, 1, 40)) {
    return *priority;
  }
  if (const auto priority = IndexedTokenPriority(token, "raidpet", 400, 1, 40)) {
    return *priority;
  }
  if (const auto priority = IndexedTokenPriority(token, "arena", 500, 1, 5)) {
    return *priority;
  }
  if (const auto priority = IndexedTokenPriority(token, "arenapet", 600, 1, 5)) {
    return *priority;
  }
  if (const auto priority = IndexedTokenPriority(token, "boss", 700, 1, 16)) {
    return *priority;
  }
  if (const auto priority = IndexedTokenPriority(token, "commentator", 800, 1, 10)) {
    return *priority;
  }
  if (token == "target") return 900;
  if (token == "focus") return 901;
  if (token == "npc") return 902;
  if (token == "mouseover") return 903;
  if (token == "questnpc") return 904;
  if (token == "none") return 905;
  return 10000;
}

void AppendSessionTokenMatches(const openwow::game::WorldSession &session,
                               const std::uint64_t guid,
                               std::vector<std::string> &result) {
  const auto &objects = session.objects();

  AppendTokenIfGuidMatches(guid, objects.GetActivePlayerGuid().GetRawValue(),
                           "player", result);

  if (const auto *player = objects.GetLocalPlayerTyped(); player != nullptr) {
    if (const auto *vehicle = openwow::game::ResolveRootVehicleUnit(*player);
        vehicle != nullptr) {
      AppendTokenIfGuidMatches(guid, vehicle->GetGuid().GetRawValue(),
                               "vehicle", result);
    }
  }

  AppendTokenIfGuidMatches(guid, session.pet().GetPrimaryPetGuid().GetRawValue(),
                           "pet", result);

  const auto &group = openwow::game::GroupSystem::Get();
  const auto party_count =
      std::min<std::uint32_t>(group.GetTrackedPartyMemberCount(), 4u);
  for (std::uint32_t slot = 0; slot < party_count; ++slot) {
    AppendIndexedTokenIfGuidMatches(guid, group.GetTrackedPartyMemberGuid(slot),
                                    "party", slot, result);
    AppendIndexedTokenIfGuidMatches(
        guid, group.GetTrackedPartyControlledUnitGuid(session.objects(), slot),
        "partypet", slot,
        result);
  }

  const auto raid_count =
      std::min<std::size_t>(group.GetNumGroupMembers(), 40u);
  for (std::size_t slot = 0; slot < raid_count; ++slot) {
    const auto *member = group.GetMember(slot);
    if (member == nullptr) {
      continue;
    }
    AppendIndexedTokenIfGuidMatches(guid, member->guid, "raid", slot, result);
    AppendIndexedTokenIfGuidMatches(guid, member->pet_guid, "raidpet", slot,
                                    result);
  }

  const auto arena_count =
      std::min<std::size_t>(session.battleground().GetArenaOpponentSlotCount(), 5u);
  for (std::size_t slot = 0; slot < arena_count; ++slot) {
    const auto opponent = session.battleground().GetArenaOpponent(slot);
    AppendIndexedTokenIfGuidMatches(guid, opponent.guid.GetRawValue(), "arena",
                                    slot, result);
    AppendIndexedTokenIfGuidMatches(guid, opponent.pet_guid.GetRawValue(),
                                    "arenapet", slot, result);
  }
}

void AppendSessionSelectionTokenMatches(
    const openwow::game::WorldSession &session,
    const std::uint64_t guid,
    std::vector<std::string> &result) {
  const auto &objects = session.objects();
  AppendTokenIfGuidMatches(guid, objects.GetTargetGuid().GetRawValue(),
                           "target", result);
  AppendTokenIfGuidMatches(guid, objects.GetFocusTargetGuid().GetRawValue(),
                           "focus", result);
  AppendTokenIfGuidMatches(guid, objects.GetNpcGuid().GetRawValue(), "npc",
                           result);
  AppendTokenIfGuidMatches(guid, objects.GetMouseoverGuid().GetRawValue(),
                           "mouseover", result);
}

void AppendUniqueGuid(std::vector<std::uint64_t> &guids, const std::uint64_t guid) {
  if (guid != 0 && std::find(guids.begin(), guids.end(), guid) == guids.end()) {
    guids.push_back(guid);
  }
}

std::vector<std::uint64_t> CollectTrackedUnitGuids(
    const openwow::game::WorldSession *session) {
  std::vector<std::uint64_t> guids;
  if (session == nullptr) {
    return guids;
  }

  const auto &objects = session->objects();
  AppendUniqueGuid(guids, objects.GetActivePlayerGuid().GetRawValue());

  if (const auto *player = objects.GetLocalPlayerTyped(); player != nullptr) {
    AppendUniqueGuid(guids, player->State().GetPrimaryControlledUnitGUID().GetRawValue());
  }
  AppendUniqueGuid(guids, session->pet().GetPrimaryPetGuid().GetRawValue());

  const auto &group = openwow::game::GroupSystem::Get();
  const auto party_count =
      std::min<std::uint32_t>(group.GetTrackedPartyMemberCount(), 4u);
  for (std::uint32_t slot = 0; slot < party_count; ++slot) {
    AppendUniqueGuid(guids, group.GetTrackedPartyMemberGuid(slot));
    AppendUniqueGuid(guids,
                     group.GetTrackedPartyControlledUnitGuid(objects, slot));
  }

  const auto raid_count =
      std::min<std::size_t>(group.GetNumGroupMembers(), 40u);
  for (std::size_t slot = 0; slot < raid_count; ++slot) {
    const auto *member = group.GetMember(slot);
    if (member == nullptr) {
      continue;
    }
    AppendUniqueGuid(guids, member->guid);
    AppendUniqueGuid(guids, member->pet_guid);
  }

  const auto arena_count =
      std::min<std::size_t>(session->battleground().GetArenaOpponentSlotCount(), 5u);
  for (std::size_t slot = 0; slot < arena_count; ++slot) {
    const auto opponent = session->battleground().GetArenaOpponent(slot);
    AppendUniqueGuid(guids, opponent.guid.GetRawValue());
    AppendUniqueGuid(guids, opponent.pet_guid.GetRawValue());
  }

  AppendUniqueGuid(guids, objects.GetTargetGuid().GetRawValue());
  AppendUniqueGuid(guids, objects.GetFocusTargetGuid().GetRawValue());
  AppendUniqueGuid(guids, objects.GetNpcGuid().GetRawValue());
  return guids;
}

}

UnitTokenRegistry &UnitTokenRegistry::Get() {
  static UnitTokenRegistry instance;
  return instance;
}

void UnitTokenRegistry::SetToken(const std::string &token, std::uint64_t guid) {
  std::lock_guard lock(mutex_);
  if (guid == 0) {
    token_to_guid_.erase(token);
  } else {
    token_to_guid_[token] = guid;
  }
}

void UnitTokenRegistry::ClearToken(const std::string &token) {
  std::lock_guard lock(mutex_);
  token_to_guid_.erase(token);
}

std::uint64_t UnitTokenRegistry::GuidForToken(const std::string &token) const {
  std::lock_guard lock(mutex_);
  auto it = token_to_guid_.find(token);
  return it != token_to_guid_.end() ? it->second : 0;
}

std::string UnitTokenRegistry::TokenForGuid(std::uint64_t guid) const {
  const auto tokens = AllTokensForGuid(guid, nullptr);
  return tokens.empty() ? std::string{} : tokens.front();
}

std::string UnitTokenRegistry::TokenForGuid(
    std::uint64_t guid, const openwow::game::WorldSession *session) const {
  const auto tokens = AllTokensForGuid(guid, session);
  return tokens.empty() ? std::string{} : tokens.front();
}

std::string UnitTokenRegistry::TokenForAuraCasterGuid(
    const std::uint64_t guid,
    const openwow::game::WorldSession *session) const {
  const auto direct = TokenForGuid(guid, session);
  if (!direct.empty() || guid == 0 || session == nullptr) {
    return direct;
  }

  const auto *unit = session->objects().GetUnit(openwow::game::ObjectGuid(guid));
  if (unit == nullptr) {
    return {};
  }

  const auto owner_guid = unit->State().GetCharmedByOrCreatedByGUID();
  const auto controlled_guid = unit->State().GetPrimaryControlledUnitGUID();
  const std::array<std::uint64_t, 2> related_guids = {
      owner_guid.GetRawValue(), controlled_guid.GetRawValue()};

  std::string best_token;
  int best_priority = std::numeric_limits<int>::max();
  for (const auto related_guid : related_guids) {
    if (related_guid == 0) {
      continue;
    }

    const auto tokens = AllTokensForGuid(related_guid, session);
    for (const auto &token : tokens) {
      const int priority = UnitTokenPriority(token);
      if (priority < best_priority ||
          (priority == best_priority && token < best_token)) {
        best_priority = priority;
        best_token = token;
      }
    }
  }

  return best_token;
}

std::vector<std::string> UnitTokenRegistry::AllTokensForGuid(std::uint64_t guid) const {
  return AllTokensForGuid(guid, nullptr);
}

std::vector<std::string> UnitTokenRegistry::AllTokensForGuid(
    std::uint64_t guid, const openwow::game::WorldSession *session) const {
  std::lock_guard lock(mutex_);
  std::vector<std::string> result;
  result.reserve(token_to_guid_.size() + 16);

  if (session != nullptr) {
    AppendSessionTokenMatches(*session, guid, result);
    AppendManualSupplementalTokenMatches(token_to_guid_, guid, result);
    AppendSessionSelectionTokenMatches(*session, guid, result);
  } else {
    AppendManualTokenMatches(token_to_guid_, guid, result);
  }

  std::vector<std::string> extras;
  for (const auto &[token, mapped_guid] : token_to_guid_) {
    if (mapped_guid != guid) {
      continue;
    }
    if (session != nullptr && IsSessionOwnedToken(token)) {
      continue;
    }
    if (std::find(result.begin(), result.end(), token) == result.end()) {
      extras.push_back(token);
    }
  }
  std::sort(extras.begin(), extras.end());
  result.insert(result.end(), extras.begin(), extras.end());
  return result;
}

void UnitTokenRegistry::SetPlayer(std::uint64_t guid) {
  SetToken("player", guid);
}

void UnitTokenRegistry::SetTarget(std::uint64_t guid) {
  if (guid == 0) {
    ClearToken("target");
  } else {
    SetToken("target", guid);
  }
}

void UnitTokenRegistry::SetFocus(std::uint64_t guid) {
  if (guid == 0) {
    ClearToken("focus");
  } else {
    SetToken("focus", guid);
  }
}

void UnitTokenRegistry::SetPartyMember(std::uint8_t index, std::uint64_t guid) {
  std::string token = "party" + std::to_string(index + 1);
  if (guid == 0) {
    ClearToken(token);
  } else {
    SetToken(token, guid);
  }
}

void UnitTokenRegistry::SetRaidMember(std::uint8_t index, std::uint64_t guid) {
  if (index >= 40)
    return;
  std::string token = "raid" + std::to_string(index + 1);
  if (guid == 0) {
    ClearToken(token);
  } else {
    SetToken(token, guid);
  }
}

void UnitTokenRegistry::SetBossFrame(std::uint8_t index, std::uint64_t guid) {
  if (index >= 16)
    return;
  std::string token = "boss" + std::to_string(index + 1);
  if (guid == 0) {
    ClearToken(token);
  } else {
    SetToken(token, guid);
  }
}

void UnitTokenRegistry::SetPet(std::uint64_t guid) {
  if (guid == 0) {
    ClearToken("pet");
  } else {
    SetToken("pet", guid);
  }
}

void UnitTokenRegistry::ClearBossFrames() {
  for (std::uint8_t index = 0; index < 16; ++index) {
    ClearToken("boss" + std::to_string(index + 1));
  }
}

void UnitTokenRegistry::SetVehicle(std::uint64_t guid) {
  if (guid == 0) {
    ClearToken("vehicle");
  } else {
    SetToken("vehicle", guid);
  }
}

void UnitTokenRegistry::SetRaidPetMember(std::uint8_t index, std::uint64_t guid) {
  if (index >= 40) return;
  std::string token = "raidpet" + std::to_string(index + 1);
  if (guid == 0) {
    ClearToken(token);
  } else {
    SetToken(token, guid);
  }
}

void UnitTokenRegistry::SetArenaOpponent(std::uint8_t index, std::uint64_t guid) {
  if (index >= 5) return;
  std::string token = "arena" + std::to_string(index + 1);
  if (guid == 0) {
    ClearToken(token);
  } else {
    SetToken(token, guid);
  }
}

void UnitTokenRegistry::SetArenaPet(std::uint8_t index, std::uint64_t guid) {
  if (index >= 5) return;
  std::string token = "arenapet" + std::to_string(index + 1);
  if (guid == 0) {
    ClearToken(token);
  } else {
    SetToken(token, guid);
  }
}

void UnitTokenRegistry::SetCommentatorUnit(std::uint8_t index, std::uint64_t guid) {
  if (index >= 10) return;
  std::string token = "commentator" + std::to_string(index + 1);
  if (guid == 0) {
    ClearToken(token);
  } else {
    SetToken(token, guid);
  }
}

void UnitTokenRegistry::SetNpc(std::uint64_t guid) {
  if (guid == 0) {
    ClearToken("npc");
  } else {
    SetToken("npc", guid);
  }
}

void UnitTokenRegistry::SetQuestNpc(std::uint64_t guid) {
  if (guid == 0) {
    ClearToken("questnpc");
  } else {
    SetToken("questnpc", guid);
  }
}

void UnitTokenRegistry::SetMouseover(std::uint64_t guid) {
  if (guid == 0) {
    ClearToken("mouseover");
  } else {
    SetToken("mouseover", guid);
  }
}

void UnitTokenRegistry::ClearArenaOpponents() {
  for (std::uint8_t index = 0; index < 5; ++index) {
    ClearToken("arena" + std::to_string(index + 1));
    ClearToken("arenapet" + std::to_string(index + 1));
  }
}

void UnitTokenRegistry::ClearCommentatorUnits() {
  for (std::uint8_t index = 0; index < 10; ++index) {
    ClearToken("commentator" + std::to_string(index + 1));
  }
}

void UnitTokenRegistry::Reset() {
  std::lock_guard lock(mutex_);
  token_to_guid_.clear();
}

bool UnitTokenRegistry::IsValidToken(const std::string &token) {
  static const std::vector<std::string> kFixed = {
      "player",  "target",       "focus",       "pet",       "mouseover",
      "vehicle", "npc", "questnpc", "none",
      "targettarget", "focustarget", "pettarget",
  };
  for (const auto &t : kFixed) {
    if (token == t)
      return true;
  }

  if (token.size() == 6 && token.substr(0, 5) == "party") {
    char c = token[5];
    return c >= '1' && c <= '4';
  }

  if (token.size() == 9 && token.substr(0, 8) == "partypet") {
    char c = token[8];
    return c >= '1' && c <= '4';
  }

  if (token.size() >= 8 && token.substr(0, 7) == "raidpet") {
    try {
      int n = std::stoi(token.substr(7));
      return n >= 1 && n <= 40;
    } catch (...) {
      return false;
    }
  }

  if (token.size() >= 5 && token.substr(0, 4) == "raid") {
    try {
      int n = std::stoi(token.substr(4));
      return n >= 1 && n <= 40;
    } catch (...) {
      return false;
    }
  }

  if (token.size() == 6 && token.substr(0, 5) == "arena") {
    char c = token[5];
    return c >= '1' && c <= '5';
  }

  if (token.size() == 9 && token.substr(0, 8) == "arenapet") {
    char c = token[8];
    return c >= '1' && c <= '5';
  }

  if (token.size() >= 5 && token.substr(0, 4) == "boss") {
    try {
      int n = std::stoi(token.substr(4));
      return n >= 1 && n <= 16;
    } catch (...) {
      return false;
    }
  }

  if (token.size() >= 12 && token.substr(0, 11) == "commentator") {
    try {
      int n = std::stoi(token.substr(11));
      return n >= 1 && n <= 10;
    } catch (...) {
      return false;
    }
  }
  return false;
}

std::size_t UnitTokenRegistry::token_count() const {
  std::lock_guard lock(mutex_);
  return token_to_guid_.size();
}

ScriptEventDispatch &ScriptEventDispatch::Get() {
  static ScriptEventDispatch instance;
  return instance;
}

void ScriptEventDispatch::Initialize(EventDispatcher *dispatcher,
                                     openwow::game::WorldSession *session) {
  dispatcher_ = dispatcher;
  session_ = session;
}

void ScriptEventDispatch::BindWorldSession(openwow::game::WorldSession *session) {
  session_ = session;
}

void ScriptEventDispatch::Shutdown() {
  dispatcher_ = nullptr;
  session_ = nullptr;
  queued_events_.clear();
}

void ScriptEventDispatch::FireEvent(const char *event_name) {
  if (!dispatcher_) {
    return;
  }
  dispatcher_->FireEvent(event_name);
}

void ScriptEventDispatch::FireEventArgs(const char *event_name,
                                        std::initializer_list<EventArg> args) {
  if (!dispatcher_) {
    return;
  }
  dispatcher_->FireEventArgs(event_name, args);
}

void ScriptEventDispatch::FireEventV(const char *event_name,
                                     const std::vector<EventArg>& args) {
  if (!dispatcher_) {
    return;
  }
  dispatcher_->FireEventV(event_name, args);
}

void ScriptEventDispatch::FirePerUnitEvent(const char *event_name, std::uint64_t guid) {
  if (!dispatcher_ || guid == 0)
    return;

  auto tokens = UnitTokenRegistry::Get().AllTokensForGuid(guid, session_);
  if (tokens.empty())
    return;

  for (const auto &token : tokens) {
    dispatcher_->FireEvent(event_name, token);
  }
}

void ScriptEventDispatch::FirePerUnitEventWithArgs(const char *event_name, std::uint64_t guid,
                                                   const std::vector<EventArg> &extra_args) {
  if (!dispatcher_ || guid == 0)
    return;

  auto tokens = UnitTokenRegistry::Get().AllTokensForGuid(guid, session_);
  if (tokens.empty())
    return;

  for (const auto &token : tokens) {
    std::vector<EventArg> args;
    args.push_back(token);
    args.insert(args.end(), extra_args.begin(), extra_args.end());
    dispatcher_->FireEventV(event_name, args);
  }
}

void ScriptEventDispatch::QueueEvent(const std::uint64_t guid, const char *event_name) {
  if (event_name == nullptr || *event_name == '\0') {
    return;
  }

  if (std::find_if(queued_events_.begin(), queued_events_.end(),
                   [guid, event_name](const QueuedScriptEvent &entry) {
                     return entry.guid == guid && entry.event_name == event_name;
                   }) != queued_events_.end()) {
    return;
  }

  queued_events_.push_back({guid, event_name});
}

void ScriptEventDispatch::QueuePerUnitEvent(const char *event_name,
                                            const std::uint64_t guid) {
  if (guid == 0) {
    return;
  }
  QueueEvent(guid, event_name);
}

void ScriptEventDispatch::QueueTrackedUnitEvent(const char *event_name) {
  for (const auto guid : CollectTrackedUnitGuids(session_)) {
    QueueEvent(guid, event_name);
  }
}

void ScriptEventDispatch::QueueGlobalEvent(const char *event_name) {
  QueueEvent(0, event_name);
}

void ScriptEventDispatch::FlushQueuedEvents() {
  std::vector<QueuedScriptEvent> pending;
  pending.swap(queued_events_);

  if (dispatcher_ == nullptr) {
    return;
  }

  for (const auto &entry : pending) {
    if (entry.guid == 0) {
      FireGlobalEvent(entry.event_name.c_str());
    } else {
      FirePerUnitEvent(entry.event_name.c_str(), entry.guid);
    }
  }
}

void ScriptEventDispatch::ClearQueuedEvents() {
  queued_events_.clear();
}

std::size_t ScriptEventDispatch::queued_event_count() const {
  return queued_events_.size();
}

void ScriptEventDispatch::FireGlobalEvent(const char *event_name) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(event_name);
}

void ScriptEventDispatch::FireGlobalEventArgs(const char *event_name,
                                              const std::vector<EventArg> &args) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEventV(event_name, args);
}

void ScriptEventDispatch::FireGlobalEventWithArgs(const char *event_name,
                                                  const std::vector<std::string> &args) {
  if (!dispatcher_)
    return;
  std::vector<EventArg> event_args;
  for (const auto &a : args) {
    event_args.push_back(a);
  }
  dispatcher_->FireEventV(event_name, event_args);
}

void ScriptEventDispatch::FireUnitHealth(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_HEALTH, guid);
}

void ScriptEventDispatch::FireUnitMaxHealth(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_MAXHEALTH, guid);
}

void ScriptEventDispatch::FireUnitPowerSpecific(std::uint64_t guid, std::uint8_t power_type) {
  static const char *kPowerEvents[] = {
      UNIT_MANA,
      UNIT_RAGE,
      UNIT_FOCUS,
      UNIT_ENERGY,
      UNIT_HAPPINESS,
      nullptr,
      UNIT_RUNIC_POWER,
  };
  if (power_type < 7 && kPowerEvents[power_type]) {
    FirePerUnitEvent(kPowerEvents[power_type], guid);
  }
}

void ScriptEventDispatch::FireUnitMaxPowerSpecific(
    const std::uint64_t guid, const std::uint8_t power_type) {
  static constexpr const char* kMaxPowerEvents[] = {
      UNIT_MAXMANA,
      UNIT_MAXRAGE,
      UNIT_MAXFOCUS,
      UNIT_MAXENERGY,
      UNIT_MAXHAPPINESS,
      nullptr,
      UNIT_MAXRUNIC_POWER,
  };
  if (power_type < 7 && kMaxPowerEvents[power_type] != nullptr) {
    FirePerUnitEvent(kMaxPowerEvents[power_type], guid);
  }
}

void ScriptEventDispatch::FireUnitLevel(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_LEVEL, guid);
}

void ScriptEventDispatch::FireUnitAura(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_AURA, guid);
}

void ScriptEventDispatch::FireUnitTarget(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_TARGET, guid);
}

void ScriptEventDispatch::FireUnitName(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_NAME_UPDATE, guid);
}

void ScriptEventDispatch::FireUnitFlags(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_FLAGS, guid);
}

void ScriptEventDispatch::FireUnitPortrait(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_PORTRAIT_UPDATE, guid);
}

void ScriptEventDispatch::FireUnitModel(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_MODEL_CHANGED, guid);
}

void ScriptEventDispatch::FireUnitFaction(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_FACTION, guid);
}

void ScriptEventDispatch::FireUnitClassification(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_CLASSIFICATION_CHANGED, guid);
}

void ScriptEventDispatch::FireUnitStats(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_STATS, guid);
}

void ScriptEventDispatch::FireUnitAttack(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_ATTACK, guid);
}

void ScriptEventDispatch::FireUnitDefense(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_DEFENSE, guid);
}

void ScriptEventDispatch::FireUnitSpellHaste(std::uint64_t guid) {

  FirePerUnitEvent(UNIT_STATS, guid);
}

void ScriptEventDispatch::FireUnitDisplayPower(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_DISPLAYPOWER, guid);
}

void ScriptEventDispatch::FireUnitInventoryChanged(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_INVENTORY_CHANGED, guid);
}

void ScriptEventDispatch::FireUnitComboPoints(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_COMBO_POINTS, guid);
}

void ScriptEventDispatch::FireUnitAttackPower(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_ATTACK_POWER, guid);
}

void ScriptEventDispatch::FireUnitAttackSpeed(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_ATTACK_SPEED, guid);
}

void ScriptEventDispatch::FireUnitRangedDamage(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_RANGEDDAMAGE, guid);
}

void ScriptEventDispatch::FireUnitRangedAttackPower(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_RANGED_ATTACK_POWER, guid);
}

void ScriptEventDispatch::FireUnitResistances(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_RESISTANCES, guid);
}

void ScriptEventDispatch::FireUnitPet(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_PET, guid);
}

void ScriptEventDispatch::FirePlayerXP() {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(PLAYER_XP_UPDATE, std::string("player"));
}

void ScriptEventDispatch::FirePlayerMoney() {
  FireGlobalEvent(PLAYER_MONEY);
}

void ScriptEventDispatch::FirePlayerFlags() {
  FireGlobalEvent(PLAYER_FLAGS_CHANGED);
}

void ScriptEventDispatch::FirePlayerEnteringWorld() {
  FireGlobalEvent(PLAYER_ENTERING_WORLD);
}

void ScriptEventDispatch::FirePlayerLeavingWorld() {
  FireGlobalEvent(PLAYER_LEAVING_WORLD);
}

void ScriptEventDispatch::FirePlayerLogin() {
  FireGlobalEvent(PLAYER_LOGIN);
}

void ScriptEventDispatch::FirePlayerAlive() {
  FireGlobalEvent(PLAYER_ALIVE);
}

void ScriptEventDispatch::FirePlayerDead() {
  FireGlobalEvent(PLAYER_DEAD);
}

void ScriptEventDispatch::FirePlayerUnghost() {
  FireGlobalEvent(PLAYER_UNGHOST);
}

void ScriptEventDispatch::FireAreaSpiritHealerInRange() {
  FireGlobalEvent(AREA_SPIRIT_HEALER_IN_RANGE);
}

void ScriptEventDispatch::FirePlayerRegenEnabled() {
  FireGlobalEvent(PLAYER_REGEN_ENABLED);
}

void ScriptEventDispatch::FirePlayerRegenDisabled() {
  FireGlobalEvent(PLAYER_REGEN_DISABLED);
}

void ScriptEventDispatch::FirePlayerEquipmentChanged(const std::uint8_t inventory_slot,
                                                       const bool has_item) {
  if (!dispatcher_)
    return;

  const auto lua_slot = static_cast<int>(inventory_slot) + 1;
  if (has_item) {
    dispatcher_->FireEventArgs(PLAYER_EQUIPMENT_CHANGED, {lua_slot, 1});
  } else {
    dispatcher_->FireEvent(PLAYER_EQUIPMENT_CHANGED, lua_slot);
  }
}

void ScriptEventDispatch::FirePlayerBankSlotsChanged(const std::uint8_t bank_slot) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(PLAYERBANKSLOTS_CHANGED, static_cast<int>(bank_slot));
}

void ScriptEventDispatch::FirePlayerBankBagSlotsChanged() {
  FireGlobalEvent(PLAYERBANKBAGSLOTS_CHANGED);
}

void ScriptEventDispatch::FirePlayerLevelUp(std::uint32_t new_level) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(PLAYER_LEVEL_UP, static_cast<int>(new_level));
}

void ScriptEventDispatch::FirePetSpellPowerUpdate() {
  FireGlobalEvent(PET_SPELL_POWER_UPDATE);
}

void ScriptEventDispatch::FirePlayerCombatStatEvents() {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(UNIT_ATTACK, std::string("player"));
  dispatcher_->FireEvent(UNIT_DEFENSE, std::string("player"));
  dispatcher_->FireEvent(UNIT_RANGED_ATTACK_POWER, std::string("player"));
}

void ScriptEventDispatch::FirePlayerUpdateResting() {
  FireGlobalEvent(PLAYER_UPDATE_RESTING);
}

void ScriptEventDispatch::FirePlayerControlGained() {
  FireGlobalEvent(PLAYER_CONTROL_GAINED);
}

void ScriptEventDispatch::FirePlayerControlLost() {
  FireGlobalEvent(PLAYER_CONTROL_LOST);
}

void ScriptEventDispatch::FirePlayerTotemUpdate(std::uint8_t slot) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(PLAYER_TOTEM_UPDATE, static_cast<int>(slot));
}

void ScriptEventDispatch::FirePlayerTargetChanged() {
  FireGlobalEvent(PLAYER_TARGET_CHANGED);
}

void ScriptEventDispatch::FirePlayerFocusChanged() {
  FireGlobalEvent(PLAYER_FOCUS_CHANGED);
}

void ScriptEventDispatch::FireRaidRosterUpdate() {
  FireGlobalEvent(RAID_ROSTER_UPDATE);
}

void ScriptEventDispatch::FirePartyMemberEnable(std::uint8_t index) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(PARTY_MEMBER_ENABLE, static_cast<int>(index + 1));
}

void ScriptEventDispatch::FirePartyMemberDisable(std::uint8_t index) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(PARTY_MEMBER_DISABLE, static_cast<int>(index + 1));
}

void ScriptEventDispatch::FirePartyLeaderChanged() {
  FireGlobalEvent(PARTY_LEADER_CHANGED);
}

void ScriptEventDispatch::FirePartyMembersChanged() {
  FireGlobalEvent(PARTY_MEMBERS_CHANGED);
}

void ScriptEventDispatch::FirePlayerEnterCombat() {
  FireGlobalEvent(PLAYER_ENTER_COMBAT);
}

void ScriptEventDispatch::FirePlayerLeaveCombat() {
  FireGlobalEvent(PLAYER_LEAVE_COMBAT);
}

void ScriptEventDispatch::FireUnitCombat(std::uint64_t guid,
                                         const std::string &damage_school,
                                         const std::string &descriptor,
                                         int amount,
                                         int extra_amount) {
  if (!dispatcher_ || guid == 0) {
    return;
  }

  const auto tokens = UnitTokenRegistry::Get().AllTokensForGuid(guid);
  for (const auto &token : tokens) {
    dispatcher_->FireEventArgs(UNIT_COMBAT,
                               {token, damage_school, descriptor, amount, extra_amount});
  }
}

void ScriptEventDispatch::FireCombatLogEvents(
    const openwow::game::CombatLogEntry &entry) {
  if (dispatcher_ == nullptr) {
    return;
  }

  lua_State *const state = dispatcher_->GetLuaState();
  if (state == nullptr) {
    return;
  }

  const int original_top = lua_gettop(state);
  const int argument_count =
      openwow::game::CombatLog::PushEntryToLua(state, entry);
  std::vector<EventArg> arguments;
  arguments.reserve(static_cast<std::size_t>(argument_count));
  for (int index = original_top + 1; index <= original_top + argument_count;
       ++index) {
    switch (lua_type(state, index)) {
      case LUA_TSTRING: {
        std::size_t length = 0;
        const char *const value = lua_tolstring(state, index, &length);
        arguments.emplace_back(std::string(value != nullptr ? value : "", length));
        break;
      }
      case LUA_TNUMBER:
        arguments.emplace_back(static_cast<double>(lua_tonumber(state, index)));
        break;
      case LUA_TBOOLEAN:
        arguments.emplace_back(lua_toboolean(state, index) != 0);
        break;
      case LUA_TNIL:
      default:
        arguments.emplace_back(std::monostate{});
        break;
    }
  }
  lua_settop(state, original_top);

  const bool passes_filter =
      session_ == nullptr || session_->combat_log().MatchesEventFilters(entry);

  static int combat_log_receipt_count = 0;
  constexpr int kCombatLogReceiptLimit = 10;
  if (combat_log_receipt_count < kCombatLogReceiptLimit) {
    ++combat_log_receipt_count;
    const auto filtered_listeners =
        dispatcher_->GetFramesRegisteredForEvent(COMBAT_LOG_EVENT).size();
    const auto unfiltered_listeners =
        dispatcher_->GetFramesRegisteredForEvent(COMBAT_LOG_EVENT_UNFILTERED)
            .size();
    const auto filter_count =
        session_ != nullptr ? session_->combat_log().event_filter_count()
                            : std::size_t{0};
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kInfo,
        std::string("Combat log dispatch receipt: event=") +
            openwow::game::CombatLog::GetEventName(entry.type) +
            " filtered=" + (passes_filter ? "pass" : "drop") +
            " filters=" + std::to_string(filter_count) +
            " listeners=" + std::to_string(filtered_listeners) +
            " unfiltered_listeners=" + std::to_string(unfiltered_listeners) +
            " args=" + std::to_string(arguments.size()));
  }

  if (passes_filter) {
    dispatcher_->FireEventV(COMBAT_LOG_EVENT, arguments);
  }
  dispatcher_->FireEventV(COMBAT_LOG_EVENT_UNFILTERED, arguments);
}

void ScriptEventDispatch::FireUnitSpellcastStart(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_SPELLCAST_START, guid);
}

void ScriptEventDispatch::FireUnitSpellcastStop(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_SPELLCAST_STOP, guid);
}

void ScriptEventDispatch::FireUnitSpellcastSucceeded(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_SPELLCAST_SUCCEEDED, guid);
}

void ScriptEventDispatch::FireUnitSpellcastFailed(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_SPELLCAST_FAILED, guid);
}

void ScriptEventDispatch::FireUnitSpellcastInterrupted(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_SPELLCAST_INTERRUPTED, guid);
}

void ScriptEventDispatch::FireUnitSpellcastChannelStart(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_SPELLCAST_CHANNEL_START, guid);
}

void ScriptEventDispatch::FireUnitSpellcastChannelUpdate(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_SPELLCAST_CHANNEL_UPDATE, guid);
}

void ScriptEventDispatch::FireUnitSpellcastChannelStop(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_SPELLCAST_CHANNEL_STOP, guid);
}

void ScriptEventDispatch::FireUnitSpellcastSent(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_SPELLCAST_SENT, guid);
}

void ScriptEventDispatch::FireUnitSpellcastFailedQuiet(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_SPELLCAST_FAILED_QUIET, guid);
}

void ScriptEventDispatch::FireUnitSpellcastDelayed(std::uint64_t guid) {
  FirePerUnitEvent(UNIT_SPELLCAST_DELAYED, guid);
}

void ScriptEventDispatch::FireZoneChanged() {
  FireGlobalEvent(ZONE_CHANGED);
}

void ScriptEventDispatch::FireZoneChangedNewArea() {
  FireGlobalEvent(ZONE_CHANGED_NEW_AREA);
}

void ScriptEventDispatch::FireSubzoneChanged() {
  FireGlobalEvent(ZONE_CHANGED_INDOORS);
}

void ScriptEventDispatch::FireBagOpen(int bag) {
  if (!dispatcher_) {
    return;
  }
  dispatcher_->FireEvent(BAG_OPEN, bag);
}

void ScriptEventDispatch::FireBagUpdate(const int bag) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(BAG_UPDATE, bag);
}

void ScriptEventDispatch::FireBagClosed(std::uint8_t bag) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(BAG_CLOSED, static_cast<int>(bag));
}

void ScriptEventDispatch::FirePlayerBagUpdates() {
  FireBagUpdate(0);
  FireBagUpdate(-2);
  FireBagUpdate(-4);
}

void ScriptEventDispatch::FireBagUpdateCooldown() {
  FireGlobalEvent(BAG_UPDATE_COOLDOWN);
}

void ScriptEventDispatch::FireCursorUpdate() {
  FireGlobalEvent(CURSOR_UPDATE);
}

void ScriptEventDispatch::FireActionbarShowGrid() {
  FireGlobalEvent(ACTIONBAR_SHOWGRID);
}

void ScriptEventDispatch::FireActionbarHideGrid() {
  FireGlobalEvent(ACTIONBAR_HIDEGRID);
}

void ScriptEventDispatch::FireActionbarUpdate() {
  FireGlobalEvent(ACTIONBAR_UPDATE_STATE);
}

void ScriptEventDispatch::FireActionbarSlotChanged(std::uint8_t slot) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(ACTIONBAR_SLOT_CHANGED, static_cast<int>(slot));
}

void ScriptEventDispatch::FireActionbarPageChanged() {
  FireGlobalEvent(ACTIONBAR_PAGE_CHANGED);
}

void ScriptEventDispatch::FireActionbarUpdateCooldown() {
  FireGlobalEvent(ACTIONBAR_UPDATE_COOLDOWN);
}

void ScriptEventDispatch::FireActionbarSpellAndShapeshiftCooldownUpdates(
    const bool has_shapeshift_forms) {
  FireActionbarUpdateCooldown();
  if (has_shapeshift_forms) {
    FireGlobalEvent(UPDATE_SHAPESHIFT_COOLDOWN);
  }
  FireSpellUpdateCooldown();
}

void ScriptEventDispatch::FireActionbarUpdateUsable() {
  FireGlobalEvent(ACTIONBAR_UPDATE_USABLE);
}

void ScriptEventDispatch::FireInventoryCooldownsChanged() {
  FireActionbarUpdateCooldown();
  FireBagUpdateCooldown();
}

void ScriptEventDispatch::FireUpdateBonusActionbar() {
  FireGlobalEvent(UPDATE_BONUS_ACTIONBAR);
}

void ScriptEventDispatch::FireSpellsChanged() {
  FireGlobalEvent(SPELLS_CHANGED);
}

void ScriptEventDispatch::FireCombatRatingUpdate() {
  FireGlobalEvent(COMBAT_RATING_UPDATE);
}

void ScriptEventDispatch::FireSpellUpdateCooldown() {
  FireGlobalEvent(SPELL_UPDATE_COOLDOWN);
}

void ScriptEventDispatch::FireLanguageListChanged() {
  FireGlobalEvent(LANGUAGE_LIST_CHANGED);
}

void ScriptEventDispatch::FirePetBarHideGrid() {
  FireGlobalEvent(PET_BAR_HIDEGRID);
}

void ScriptEventDispatch::FirePetBarShowGrid() {
  FireGlobalEvent(PET_BAR_SHOWGRID);
}

void ScriptEventDispatch::FirePetBarUpdate() {
  FireGlobalEvent(PET_BAR_UPDATE);
}

void ScriptEventDispatch::FirePetBarUpdateCooldown() {
  FireGlobalEvent(PET_BAR_UPDATE_COOLDOWN);
}

void ScriptEventDispatch::FirePetBarUpdateUsable() {
  FireGlobalEvent(PET_BAR_UPDATE_USABLE);
}

void ScriptEventDispatch::FirePetRenameable() {
  FireGlobalEvent(PET_RENAMEABLE);
}

void ScriptEventDispatch::FireWearEquipmentSet(const std::string &set_name) {
  FireGlobalEventWithArgs(WEAR_EQUIPMENT_SET, {set_name});
}

void ScriptEventDispatch::FireReadyCheck(const std::string &initiator,
                                         const std::uint32_t time_left_seconds) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEventArgs(READY_CHECK, {initiator, static_cast<int>(time_left_seconds)});
}

void ScriptEventDispatch::FireReadyCheckConfirm(std::uint64_t guid, bool ready) {
  FirePerUnitEventWithArgs(READY_CHECK_CONFIRM, guid, {ready});
}

void ScriptEventDispatch::FireReadyCheckFinished(const bool interrupted) {

  FireGlobalEventArgs(READY_CHECK_FINISHED, {interrupted});
}

void ScriptEventDispatch::FireRaidTargetUpdate() {
  FireGlobalEvent(RAID_TARGET_UPDATE);
}

void ScriptEventDispatch::FireConfirmSummon() {
  FireGlobalEvent(CONFIRM_SUMMON);
}

void ScriptEventDispatch::FireCancelSummon() {
  FireGlobalEvent(CANCEL_SUMMON);
}

void ScriptEventDispatch::FireConfirmTalentWipe(std::uint32_t cost) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEventArgs(CONFIRM_TALENT_WIPE,
                             {static_cast<int>(cost)});
}

void ScriptEventDispatch::FireCombatTextUpdate(const std::string &type) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(COMBAT_TEXT_UPDATE, type);
}

void ScriptEventDispatch::FireCombatTextUpdate(const std::string &type,
                                               const int amount) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEventArgs(COMBAT_TEXT_UPDATE, {type, amount});
}

void ScriptEventDispatch::FireCombatTextUpdate(const std::string &type,
                                               const int amount,
                                               const int extra_amount) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEventArgs(COMBAT_TEXT_UPDATE, {type, amount, extra_amount});
}

void ScriptEventDispatch::FireCombatTextUpdate(const std::string &type,
                                               const std::string &name) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEventArgs(COMBAT_TEXT_UPDATE, {type, name});
}

void ScriptEventDispatch::FireCombatTextUpdate(const std::string &type,
                                               const int amount,
                                               const std::string &suffix) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEventArgs(COMBAT_TEXT_UPDATE, {type, amount, suffix});
}

void ScriptEventDispatch::FireCombatTextUpdate(const std::string &type,
                                               const std::string &name,
                                               const int amount) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEventArgs(COMBAT_TEXT_UPDATE, {type, name, amount});
}

void ScriptEventDispatch::FireCombatTextUpdate(const std::string &type,
                                               const std::string &name,
                                               const int amount,
                                               const int extra_amount) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEventArgs(COMBAT_TEXT_UPDATE,
                             {type, name, amount, extra_amount});
}

void ScriptEventDispatch::FireStartLootRoll(int roll_id, int countdown_ms) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEventArgs(START_LOOT_ROLL, {roll_id, countdown_ms});
}

void ScriptEventDispatch::FireCancelLootRoll(int roll_id) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEventArgs(CANCEL_LOOT_ROLL, {roll_id});
}

void ScriptEventDispatch::FireConfirmLootRoll(int roll_id, int roll_type) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEventArgs(CONFIRM_LOOT_ROLL, {roll_id, roll_type});
}

void ScriptEventDispatch::FireConfirmDisenchantRoll(int roll_id,
                                                    int roll_type) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEventArgs(CONFIRM_DISENCHANT_ROLL, {roll_id, roll_type});
}

void ScriptEventDispatch::FireChatPlayerNotFound(const std::string &name) {
  FireGlobalEventWithArgs(CHAT_MSG_SYSTEM, {name});
}

void ScriptEventDispatch::FireChatServerMessage(const std::string &message) {
  FireGlobalEventWithArgs(CHAT_MSG_SYSTEM, {message});
}

void ScriptEventDispatch::FireUiErrorMessage(const std::string &message) {
  FireGlobalEventWithArgs(UI_ERROR_MESSAGE, {message});
}

void ScriptEventDispatch::FireUiInfoMessage(const std::string &message) {
  FireGlobalEventWithArgs(UI_INFO_MESSAGE, {message});
}

void ScriptEventDispatch::FireChannelUiUpdate() {
  FireGlobalEvent(CHANNEL_UI_UPDATE);
}

void ScriptEventDispatch::FireChannelRosterUpdate(const int display_slot,
                                                  const int member_count) {
  FireEventArgs(CHANNEL_ROSTER_UPDATE, {display_slot, member_count});
}

void ScriptEventDispatch::FireGossipShow() {
  FireGlobalEvent(GOSSIP_SHOW);
}

void ScriptEventDispatch::FireGossipClosed() {
  FireGlobalEvent(GOSSIP_CLOSED);
}

void ScriptEventDispatch::FireTrainerShow() {
  FireGlobalEvent(TRAINER_SHOW);
}

void ScriptEventDispatch::FireTrainerClosed() {
  FireGlobalEvent(TRAINER_CLOSED);
}

void ScriptEventDispatch::FireMerchantShow() {
  FireGlobalEvent(MERCHANT_SHOW);
}

void ScriptEventDispatch::FireMerchantUpdate() {
  FireGlobalEvent(MERCHANT_UPDATE);
}

void ScriptEventDispatch::FireMerchantClosed() {
  FireGlobalEvent(MERCHANT_CLOSED);
}

void ScriptEventDispatch::FireQuestDetail() {
  FireGlobalEvent(QUEST_DETAIL);
}

void ScriptEventDispatch::FireQuestComplete() {
  FireGlobalEvent(QUEST_COMPLETE);
}

void ScriptEventDispatch::FireQuestProgress() {
  FireGlobalEvent(QUEST_PROGRESS);
}

void ScriptEventDispatch::FireQuestFinished() {
  FireGlobalEvent(QUEST_FINISHED);
}

void ScriptEventDispatch::FireQuestGreeting() {
  FireGlobalEvent(QUEST_GREETING);
}

void ScriptEventDispatch::FireQuestAccepted(int quest_log_index, int quest_id) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(QUEST_ACCEPTED, quest_log_index, quest_id);
}

void ScriptEventDispatch::FireQuestLogUpdate() {
  FireGlobalEvent(QUEST_LOG_UPDATE);
}

void ScriptEventDispatch::FireQuestQueryComplete() {
  FireGlobalEvent(QUEST_QUERY_COMPLETE);
}

void ScriptEventDispatch::FireQuestAcceptConfirm(const std::string &sharer_name,
                                                 const std::string &quest_title) {
  if (!dispatcher_ || sharer_name.empty())
    return;
  dispatcher_->FireEventArgs(QUEST_ACCEPT_CONFIRM, {sharer_name, quest_title});
}

void ScriptEventDispatch::FireLootOpened(const bool auto_loot) {
  if (!dispatcher_) {
    return;
  }
  dispatcher_->FireEvent(LOOT_OPENED, auto_loot ? 1 : 0);
}

void ScriptEventDispatch::FireLootClosed() {
  FireGlobalEvent(LOOT_CLOSED);
}

void ScriptEventDispatch::FireLootSlotCleared(int slot) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(LOOT_SLOT_CLEARED, slot);
}

void ScriptEventDispatch::FireLootSlotChanged(int slot) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(LOOT_SLOT_CHANGED, slot);
}

void ScriptEventDispatch::FireLootBindConfirm(int slot) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(LOOT_BIND_CONFIRM, slot);
}

void ScriptEventDispatch::FireOpenMasterLootList() {
  FireGlobalEvent(OPEN_MASTER_LOOT_LIST);
}

void ScriptEventDispatch::FireUpdateMasterLootList() {
  FireGlobalEvent(UPDATE_MASTER_LOOT_LIST);
}

void ScriptEventDispatch::FireFriendListUpdate() {
  FireGlobalEvent(FRIENDLIST_UPDATE);
}

void ScriptEventDispatch::FireIgnoreListUpdate() {
  FireGlobalEvent(IGNORELIST_UPDATE);
}

void ScriptEventDispatch::FireWhoListUpdate() {
  FireGlobalEvent(WHO_LIST_UPDATE);
}

void ScriptEventDispatch::FireGuildRosterUpdate() {
  FireGlobalEvent(GUILD_ROSTER_UPDATE);
}

void ScriptEventDispatch::FireGuildRosterUpdate(const int update_type) {
  FireGlobalEventWithArgs(GUILD_ROSTER_UPDATE, {std::to_string(update_type)});
}

void ScriptEventDispatch::FireGuildEventLogUpdate() {
  FireGlobalEvent(GUILD_EVENT_LOG_UPDATE);
}

void ScriptEventDispatch::FireGuildInviteRequest(const std::string &inviter,
                                                 const std::string &guild_name) {
  FireGlobalEventWithArgs(GUILD_INVITE_REQUEST, {inviter, guild_name});
}

void ScriptEventDispatch::FireGuildMotd(const std::string &motd) {
  FireGlobalEventWithArgs(GUILD_MOTD, {motd});
}

void ScriptEventDispatch::FirePlayerGuildUpdate() {
  FireGlobalEvent(PLAYER_GUILD_UPDATE);
}

void ScriptEventDispatch::FireVehicleAngleUpdate(double raw_pitch, double min_pitch,
                                                 double max_pitch) {
  FireEventArgs(VEHICLE_ANGLE_UPDATE,
                {NormalizeVehicleAngle(raw_pitch, min_pitch, max_pitch), raw_pitch});
}

void ScriptEventDispatch::FireEvent(const std::string &event_name) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(event_name);
}

void ScriptEventDispatch::FireEventArgs(const std::string &event_name,
                                        std::initializer_list<EventArg> args) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEventArgs(event_name, args);
}

void ScriptEventDispatch::FireMailShow() {
  FireGlobalEvent(MAIL_SHOW);
}

void ScriptEventDispatch::FireMailClosed() {
  FireGlobalEvent(MAIL_CLOSED);
}

void ScriptEventDispatch::FireMailInboxUpdate() {
  FireGlobalEvent(MAIL_INBOX_UPDATE);
}

void ScriptEventDispatch::FireMailSendSuccess() {
  FireGlobalEvent(MAIL_SEND_SUCCESS);
}

void ScriptEventDispatch::FireUpdatePendingMail() {
  FireGlobalEvent(UPDATE_PENDING_MAIL);
}

void ScriptEventDispatch::FireAuctionHouseShow() {
  FireGlobalEvent(AUCTION_HOUSE_SHOW);
}

void ScriptEventDispatch::FireAuctionHouseClosed() {
  FireGlobalEvent(AUCTION_HOUSE_CLOSED);
}

void ScriptEventDispatch::FireAuctionItemListUpdate() {
  FireGlobalEvent(AUCTION_ITEM_LIST_UPDATE);
}

void ScriptEventDispatch::FireAuctionOwnedListUpdate() {
  FireGlobalEvent(AUCTION_OWNED_LIST_UPDATE);
}

void ScriptEventDispatch::FireAuctionBidderListUpdate() {
  FireGlobalEvent(AUCTION_BIDDER_LIST_UPDATE);
}

void ScriptEventDispatch::FireAuctionMultiSellStart(const int total_count) {
  if (dispatcher_ == nullptr) {
    return;
  }
  dispatcher_->FireEvent(AUCTION_MULTISELL_START, total_count);
}

void ScriptEventDispatch::FireAuctionMultiSellUpdate(const int completed_count,
                                                     const int total_count) {
  if (dispatcher_ == nullptr) {
    return;
  }
  dispatcher_->FireEvent(AUCTION_MULTISELL_UPDATE, completed_count, total_count);
}

void ScriptEventDispatch::FireAuctionMultiSellFailure() {
  FireGlobalEvent(AUCTION_MULTISELL_FAILURE);
}

void ScriptEventDispatch::FirePartyInviteRequest(const std::string &inviter) {
  FireGlobalEventWithArgs(PARTY_INVITE_REQUEST, {inviter});
}

void ScriptEventDispatch::FirePartyInviteCancel() {
  FireGlobalEvent(PARTY_INVITE_CANCEL);
}

void ScriptEventDispatch::FirePartyLootMethodChanged() {
  FireGlobalEvent(PARTY_LOOT_METHOD_CHANGED);
}

void ScriptEventDispatch::FireRaidInstanceWelcome(const std::string &name, int reset_time,
                                                  int flag1, int flag2) {
  if (!dispatcher_)
    return;
  std::vector<EventArg> args;
  args.push_back(name);
  args.push_back(reset_time);
  args.push_back(flag1);
  args.push_back(flag2);
  dispatcher_->FireEventV(RAID_INSTANCE_WELCOME, args);
}

void ScriptEventDispatch::FireBankFrameOpened() {
  FireGlobalEvent(BANKFRAME_OPENED);
}

void ScriptEventDispatch::FireBankFrameClosed() {
  FireGlobalEvent(BANKFRAME_CLOSED);
}

void ScriptEventDispatch::FireDuelRequested(const std::string &challenger) {
  FireGlobalEventWithArgs(DUEL_REQUESTED, {challenger});
}

void ScriptEventDispatch::FireDuelOutOfBounds() {
  FireGlobalEvent(DUEL_OUTOFBOUNDS);
}

void ScriptEventDispatch::FireDuelInBounds() {
  FireGlobalEvent(DUEL_INBOUNDS);
}

void ScriptEventDispatch::FireDuelFinished() {
  FireGlobalEvent(DUEL_FINISHED);
}

void ScriptEventDispatch::FireTradeShow() {
  FireGlobalEvent(TRADE_SHOW);
}

void ScriptEventDispatch::FireTradeClosed() {
  FireGlobalEvent(TRADE_CLOSED);
}

void ScriptEventDispatch::FireTradeAcceptUpdate() {
  FireGlobalEvent(TRADE_ACCEPT_UPDATE);
}

void ScriptEventDispatch::FireTradeAcceptUpdate(int player_accepted, int trader_accepted) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(TRADE_ACCEPT_UPDATE, player_accepted, trader_accepted);
}

void ScriptEventDispatch::FireTradePlayerItemChanged(int trade_slot) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(TRADE_PLAYER_ITEM_CHANGED, trade_slot);
}

void ScriptEventDispatch::FireTradePotentialBindEnchant(const bool enabled) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(TRADE_POTENTIAL_BIND_ENCHANT, enabled ? 1 : 0);
}

void ScriptEventDispatch::FireTradeTargetItemChanged(int trade_slot) {
  if (!dispatcher_)
    return;
  dispatcher_->FireEvent(TRADE_TARGET_ITEM_CHANGED, trade_slot);
}

void ScriptEventDispatch::FirePlayerTradeMoney() {
  FireGlobalEvent(PLAYER_TRADE_MONEY);
}

void ScriptEventDispatch::FireTradeMoneyChanged() {
  FireGlobalEvent(TRADE_MONEY_CHANGED);
}

void ScriptEventDispatch::FireTradeUpdate() {
  FireGlobalEvent(TRADE_UPDATE);
}

void ScriptEventDispatch::FireTradeRequest() {
  FireGlobalEvent(TRADE_REQUEST);
}

void ScriptEventDispatch::FireTradeRequestCancel() {
  FireGlobalEvent(TRADE_REQUEST_CANCEL);
}

void ScriptEventDispatch::FireInspectHonorUpdate() {
  FireGlobalEvent(INSPECT_HONOR_UPDATE);
}

void ScriptEventDispatch::FireTaxiMapOpened() {
  FireGlobalEvent(TAXIMAP_OPENED);
}

void ScriptEventDispatch::FireTaxiMapClosed() {
  FireGlobalEvent(TAXIMAP_CLOSED);
}

void ScriptEventDispatch::FireBarberShopClose() {
  FireGlobalEvent(BARBER_SHOP_CLOSE);
}

void ScriptEventDispatch::FireBarberShopOpen() {
  FireGlobalEvent(BARBER_SHOP_OPEN);
}

void ScriptEventDispatch::FireBarberShopSuccess() {
  FireGlobalEvent(BARBER_SHOP_SUCCESS);
}

void ScriptEventDispatch::FireBarberShopAppearanceApplied() {

  FireGlobalEvent(BARBER_SHOP_APPEARANCE_APPLIED);
}

void ScriptEventDispatch::FireCommentatorMapUpdate() {

  FireGlobalEvent(events::COMMENTATOR_MAP_UPDATE);
}

void ScriptEventDispatch::FireCommentatorEnterWorld() {

  FireGlobalEvent(events::COMMENTATOR_ENTER_WORLD);
}

void ScriptEventDispatch::FireCommentatorPlayerUpdate() {

  FireGlobalEvent(events::COMMENTATOR_PLAYER_UPDATE);
}

void ScriptEventDispatch::FireCommentatorSkirmishQueueRequest() {

  FireGlobalEvent(events::COMMENTATOR_SKIRMISH_QUEUE_REQUEST);
}

void ScriptEventDispatch::FireVoiceSessionsUpdate() {

  FireGlobalEvent(events::VOICE_SESSIONS_UPDATE);
}

void ScriptEventDispatch::FireVoiceLeftSession() {

  FireGlobalEvent(events::VOICE_LEFT_SESSION);
}

void ScriptEventDispatch::FireVoiceStart(const std::uint64_t guid,
                                         const std::string &speaker_name) {

  FirePerUnitEvent(events::VOICE_START, guid);
  if (!speaker_name.empty()) {
    const auto unit_token = UnitTokenRegistry::Get().TokenForGuid(guid);
    if (unit_token.empty()) {
      FireGlobalEventWithArgs(events::VOICE_START, {speaker_name});
    } else {
      FireGlobalEventWithArgs(events::VOICE_START, {speaker_name, unit_token});
    }
  }
}

void ScriptEventDispatch::FireVoiceStop(const std::uint64_t guid,
                                        const std::string &speaker_name) {
  FirePerUnitEvent(events::VOICE_STOP, guid);
  if (!speaker_name.empty()) {
    const auto unit_token = UnitTokenRegistry::Get().TokenForGuid(guid);
    if (unit_token.empty()) {
      FireGlobalEventWithArgs(events::VOICE_STOP, {speaker_name});
    } else {
      FireGlobalEventWithArgs(events::VOICE_STOP, {speaker_name, unit_token});
    }
  }
}

void ScriptEventDispatch::FireStartMinigame() {

  FireGlobalEvent(events::START_MINIGAME);
}

void ScriptEventDispatch::FireMinigameUpdate() {

  FireGlobalEvent(events::MINIGAME_UPDATE);
}

void ScriptEventDispatch::FireTimePlayedMsg(std::uint32_t total_time, std::uint32_t level_time) {

  FireGlobalEventWithArgs(events::TIME_PLAYED_MSG,
                          {std::to_string(total_time), std::to_string(level_time)});
}

}
