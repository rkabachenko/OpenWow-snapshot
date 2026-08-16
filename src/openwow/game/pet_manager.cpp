
#include "openwow/game/pet_manager.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/db_cache_instances.h"
#include "openwow/data/wdb_cache.h"
#include "openwow/data/wdb_persistence.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spellbook_catalog.h"
#include "openwow/game/spellbook_frame.h"
#include "openwow/game/vehicle_helpers.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/world_session.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <iterator>

namespace openwow::game {

namespace {

constexpr std::size_t kPetNameMaxBytesIncludingNul = 0x50;
constexpr std::size_t kDeclinedPetNameMaxBytesIncludingNul = 0x60;

void AppendU32LE(std::vector<std::uint8_t> &bytes, const std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

void AppendCString(std::vector<std::uint8_t> &bytes, const std::string &value) {
  bytes.insert(bytes.end(), value.begin(), value.end());
  bytes.push_back(0);
}

std::vector<std::uint8_t> SerializePetNameWdbRecord(const PetNameInfo &info) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(16 + info.name.size());

  AppendU32LE(bytes, info.pet_number);
  AppendCString(bytes, info.name);
  bytes.push_back(info.has_declined ? 1u : 0u);
  if (info.has_declined) {
    for (const auto &declined_name : info.declined_names) {
      AppendCString(bytes, declined_name);
    }
  }
  AppendU32LE(bytes, info.name_timestamp);
  return bytes;
}

std::optional<PetNameInfo> DeserializePetNameWdbRecord(const std::uint32_t expected_pet_number,
                                                       std::span<const std::uint8_t> bytes) {
  PacketReader reader(bytes);
  PetNameInfo info{};
  std::uint8_t has_declined = 0;

  if (!reader.ReadU32(info.pet_number) || info.pet_number != expected_pet_number) {
    return std::nullopt;
  }

  if (!reader.ReadCString(info.name, kPetNameMaxBytesIncludingNul)) {

    info.name.clear();
    info.found = false;
    return info;
  }
  if (!reader.ReadU8(has_declined)) {

    info.found = !info.name.empty();
    return info;
  }

  info.found = !info.name.empty();
  info.has_declined = has_declined != 0;
  if (info.has_declined) {
    for (auto &declined_name : info.declined_names) {
      if (!reader.ReadCString(declined_name,
                              kDeclinedPetNameMaxBytesIncludingNul)) {
        declined_name.clear();
      }
    }
  }

  if (!reader.ReadU32(info.name_timestamp)) {

    info.name_timestamp = 0;
  }

  return info;
}

PetReactState DecodePetReactState(const std::uint32_t mode_packed) {
  return static_cast<PetReactState>(mode_packed & 0xFFu);
}

PetCommandState DecodePetCommandState(const std::uint32_t mode_packed) {
  return static_cast<PetCommandState>((mode_packed >> 8) & 0xFFu);
}

std::uint16_t DecodePetFlags(const std::uint32_t mode_packed) {
  return static_cast<std::uint16_t>((mode_packed >> 16) & 0xFFFFu);
}

bool ShouldExposePacketSpellInPetSpellbook(const std::uint32_t spell_id) {
  const auto spell = SpellQueryBridge::Get().Query(spell_id);
  if (!spell) {
    return false;
  }

  return (spell->attributes & kSpellAttr0HiddenClientside) == 0 &&
         (spell->attributesEx4 & kSpellAttrEx4HiddenSpellbook) == 0;
}

const PetActionButton *FindLastPacketSpellEntryBySpellId(const std::vector<PetActionButton> &spells,
                                                         const std::uint32_t spell_id) {
  if (spell_id == 0) {
    return nullptr;
  }

  const auto spell_it =
      std::find_if(spells.rbegin(), spells.rend(), [spell_id](const PetActionButton &spell) {
        return spell.ActionId() == spell_id;
      });
  if (spell_it == spells.rend()) {
    return nullptr;
  }

  return &*spell_it;
}

PetActionButton *FindLastPacketSpellEntryBySpellId(std::vector<PetActionButton> &spells,
                                                   const std::uint32_t spell_id) {
  if (spell_id == 0) {
    return nullptr;
  }

  const auto spell_it =
      std::find_if(spells.rbegin(), spells.rend(), [spell_id](const PetActionButton &spell) {
        return spell.ActionId() == spell_id;
      });
  if (spell_it == spells.rend()) {
    return nullptr;
  }

  return &*spell_it;
}

void RebuildPetSpellbookSpells(PetBarState &bar) {
  bar.spellbook_spells.clear();
  bar.spellbook_spells.reserve(bar.spells.size());

  for (const auto &spell : bar.spells) {
    const auto spell_id = spell.ActionId();
    if (spell_id == 0 || !ShouldExposePacketSpellInPetSpellbook(spell_id)) {
      continue;
    }

    bar.spellbook_spells.push_back(spell_id);
  }
}

constexpr std::uint32_t kPetVehicleSeatStateGeneratedBarAllowed = 1u;
constexpr std::uint32_t kPetBarHiddenCreatureTemplateTypeFlag = 0x2000u;

ObjectGuid GetPrimaryPetGuidForBarState(const PetManager &manager) {
  return manager.GetPrimaryPetGuid();
}

std::uint8_t GetActivePlayerSeatIndex(const CGPlayer_C &player) {
  const auto *passenger = player.Vehicle().GetVehiclePassengerComponent();
  if (passenger == nullptr) {
    return 0xFFu;
  }

  return passenger->GetAltVehicleGuid() != 0 ? passenger->GetAltSeatIndex()
                                             : passenger->GetPrimarySeatIndex();
}

const openwow::data::dbc::VehicleSeatEntry *
LookupPetVehicleSeatForActivePlayer(const WorldSession &session, const CGPlayer_C &local_player,
                                    const CGUnit_C &pet_unit) {
  return LookupVehicleSeatEntryForVehicleSeat(session, pet_unit,
                                              GetActivePlayerSeatIndex(local_player));
}

bool PetBarVisibilitySuppressedByCreatureTemplate(const WorldSession &session,
                                                  const CGUnit_C &pet_unit) {
  const auto entry = pet_unit.GetEntry();
  if (entry == 0) {
    return false;
  }

  const auto *creature_template = session.query_cache().GetCreatureTemplate(entry);
  return creature_template != nullptr &&
         (creature_template->type_flags & kPetBarHiddenCreatureTemplateTypeFlag) != 0u;
}

bool IsPetBarVisibleForSession(const WorldSession &session, const PetManager &manager) {
  const ObjectGuid primary_pet_guid = GetPrimaryPetGuidForBarState(manager);
  if (primary_pet_guid.IsEmpty()) {
    return false;
  }

  const auto *pet_unit = session.objects().GetUnit(primary_pet_guid);
  if (pet_unit == nullptr) {
    return true;
  }

  if (PetBarVisibilitySuppressedByCreatureTemplate(session, *pet_unit)) {
    return false;
  }

  const auto *local_player = session.objects().GetLocalPlayerTyped();
  if (local_player != nullptr && local_player->GetPlayerTarget() == primary_pet_guid) {
    const auto *seat_entry = LookupPetVehicleSeatForActivePlayer(session, *local_player, *pet_unit);
    if (seat_entry != nullptr && seat_entry->flags_b == 0u) {
      return false;
    }
  }

  return static_cast<std::int32_t>(pet_unit->State().GetHealth()) > 0;
}

bool ComputeGeneratedPetBarActive(const WorldSession &session, const PetManager &manager) {
  const auto *local_player = session.objects().GetLocalPlayerTyped();
  if (local_player == nullptr) {
    return false;
  }

  const ObjectGuid primary_pet_guid = GetPrimaryPetGuidForBarState(manager);
  if (primary_pet_guid.IsEmpty()) {
    return false;
  }

  if (local_player->GetActiveControlGuid() != primary_pet_guid ||
      !IsPetBarVisibleForSession(session, manager)) {
    return false;
  }

  if (local_player->GetPlayerTarget() != primary_pet_guid) {
    return true;
  }

  const auto *pet_unit = session.objects().GetUnit(primary_pet_guid);
  if (pet_unit == nullptr) {
    return true;
  }

  const auto *seat_entry = LookupPetVehicleSeatForActivePlayer(session, *local_player, *pet_unit);
  if (seat_entry == nullptr) {
    return true;
  }

  return seat_entry->flags_b == kPetVehicleSeatStateGeneratedBarAllowed;
}

}

using openwow::diagnostics::Log;
using openwow::diagnostics::LogLevel;

bool PetManager::HandlePetSpells(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);

  std::uint64_t guid_raw = 0;
  if (!r.ReadU64(guid_raw))
    return false;

  const auto previous_guid = pet_bar_.guid.GetRawValue();
  const bool pet_changed = previous_guid != guid_raw;

  if (guid_raw == 0) {
    pet_bar_ = PetBarState{};
    pet_guids_.clear();
    attack_command_active_ = false;
    SpellBookFrame::RebuildPetSpellGroups({});
    return true;
  }

  PetBarState bar;
  bar.guid = ObjectGuid(guid_raw);
  bar.active = true;

  (void)r.ReadU16(bar.creature_family);
  (void)r.ReadU32(bar.duration_ms);
  bar.timed_pet_deadline_tick =
      bar.duration_ms != 0 ? core::GameClock::GetTickCount32() + bar.duration_ms : 0u;
  (void)r.ReadU32(bar.mode_packed);
  bar.react = DecodePetReactState(bar.mode_packed);
  bar.command = DecodePetCommandState(bar.mode_packed);
  bar.flags = DecodePetFlags(bar.mode_packed);

  for (int i = 0; i < 10; ++i) {
    (void)r.ReadU32(bar.action_bar[i].raw);
  }

  std::uint8_t spell_count = 0;
  (void)r.ReadU8(spell_count);
  bar.spells.reserve(spell_count);
  for (std::uint8_t i = 0; i < spell_count; ++i) {
    PetActionButton entry{};
    if (!r.ReadU32(entry.raw)) {
      break;
    }
    bar.spells.push_back(entry);
  }
  RebuildPetSpellbookSpells(bar);

  std::uint8_t cooldown_count = 0;
  (void)r.ReadU8(cooldown_count);
  bar.cooldowns.reserve(cooldown_count);
  const double start_time_s = core::GameClock::GetTickCountSeconds();
  for (std::uint8_t i = 0; i < cooldown_count; ++i) {
    PetCooldown cooldown{};
    std::uint32_t raw_category_cooldown_ms = 0;
    if (!r.ReadU32(cooldown.spell_id) || !r.ReadU16(cooldown.category) ||
        !r.ReadU32(cooldown.cooldown_ms) ||
        !r.ReadU32(raw_category_cooldown_ms)) {
      break;
    }
    cooldown.category_cooldown_ms = raw_category_cooldown_ms & 0x7FFFFFFFu;
    cooldown.enabled = (raw_category_cooldown_ms & 0x80000000u) == 0;
    cooldown.start_time_s = start_time_s;
    bar.cooldowns.push_back(cooldown);
  }

  pet_bar_ = std::move(bar);
  {
    auto existing = std::find(pet_guids_.begin(), pet_guids_.end(), guid_raw);
    if (existing == pet_guids_.end()) {
      pet_guids_.insert(pet_guids_.begin(), guid_raw);
    } else if (existing != pet_guids_.begin()) {
      const auto guid = *existing;
      pet_guids_.erase(existing);
      pet_guids_.insert(pet_guids_.begin(), guid);
    }
  }
  SpellBookFrame::RebuildPetSpellGroups(pet_bar_.spellbook_spells);
  if (pet_changed) {
    attack_command_active_ = false;
  }
  Log(LogLevel::kInfo, "Pet: loaded bar with " + std::to_string(spell_count) + " spells, " +
                           std::to_string(cooldown_count) + " cooldowns");
  return true;
}

bool PetManager::HandlePetMode(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);

  std::uint64_t guid_raw;
  if (!r.ReadU64(guid_raw))
    return false;

  std::uint32_t mode_packed = 0;
  if (!r.ReadU32(mode_packed))
    return false;

  if (pet_bar_.active && pet_bar_.guid.GetRawValue() == guid_raw) {
    pet_bar_.mode_packed = mode_packed;
    pet_bar_.react = DecodePetReactState(mode_packed);
    pet_bar_.command = DecodePetCommandState(mode_packed);
    pet_bar_.flags = DecodePetFlags(mode_packed);
  }
  return true;
}

bool PetManager::HandlePetActionFeedback(const std::uint8_t *data, std::size_t len,
                                         PetActionFeedbackResult *result) {
  PacketReader r(data, len);
  std::uint8_t feedback_code = 0;
  if (!r.ReadU8(feedback_code)) {
    return false;
  }

  last_feedback_ = static_cast<PetFeedback>(feedback_code);
  last_feedback_spell_id_ = 0;
  if (last_feedback_ == PetFeedback::kClearCooldownDisplay174Or637 ||
      last_feedback_ == PetFeedback::kClearCooldownDisplay175) {
    if (!r.ReadU32(last_feedback_spell_id_)) {
      return false;
    }
  }

  if (result != nullptr) {
    result->feedback = last_feedback_;
    result->spell_id = last_feedback_spell_id_;
  }
  return true;
}

bool PetManager::ClearSpellCooldown(const std::uint32_t spell_id) {
  if (spell_id == 0) {
    return false;
  }

  const auto original_size = pet_bar_.cooldowns.size();
  std::erase_if(pet_bar_.cooldowns,
                [spell_id](const PetCooldown &cooldown) { return cooldown.spell_id == spell_id; });
  return pet_bar_.cooldowns.size() != original_size;
}

void PetManager::InsertCooldown(const PetCooldown &cooldown) {
  const double now_s = core::GameClock::GetTickCountSeconds();
  const auto elapsed = [now_s](const double start_s,
                               const std::uint32_t duration_ms) {
    return duration_ms == 0u ||
           start_s + static_cast<double>(duration_ms) / 1000.0 <= now_s;
  };

  std::erase_if(pet_bar_.cooldowns, [&](const PetCooldown &node) {
    return node.enabled && elapsed(node.start_time_s, node.cooldown_ms) &&
           elapsed(node.start_time_s, node.category_cooldown_ms) &&
           elapsed(node.start_time_s, node.gcd_duration_ms);
  });
  pet_bar_.cooldowns.push_back(cooldown);
}

std::optional<std::uint32_t> PetManager::GetTimedPetRemainingMs() const {
  const auto deadline_tick = pet_bar_.timed_pet_deadline_tick;
  if (deadline_tick == 0) {
    return std::nullopt;
  }

  const auto first_now_tick = core::GameClock::GetTickCount32();
  if (deadline_tick == first_now_tick) {
    return 0u;
  }

  return deadline_tick - core::GameClock::GetTickCount32();
}

bool PetManager::HandlePetCastFailed(const std::uint8_t *data, std::size_t len,
                                     PetCastFailedResult *result) {
  PacketReader r(data, len);

  PetCastFailedResult parsed{};
  if (!r.ReadU8(parsed.cast_count))
    return false;
  if (!r.ReadU32(parsed.spell_id))
    return false;
  if (!r.ReadU8(parsed.result))
    return false;

  std::uint32_t extra = 0;
  if (r.ReadU32(extra)) {
    parsed.extra1 = extra;
    if (r.ReadU32(extra)) {
      parsed.extra2 = extra;
    }
  }

  last_pet_cast_spell_ = parsed.spell_id;
  last_pet_cast_result_ = parsed.result;
  if (result != nullptr) {
    *result = parsed;
  }
  return true;
}

bool PetManager::HandlePetNameQueryResponse(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);

  PetNameInfo info;
  if (!r.ReadU32(info.pet_number))
    return false;

  if (!r.ReadCString(info.name, kPetNameMaxBytesIncludingNul)) {
    info.name.clear();
  }

  std::uint8_t has_declined = 0;
  if (r.ReadU32(info.name_timestamp)) {
    (void)r.ReadU8(has_declined);
  }
  info.has_declined = (has_declined != 0);

  if (info.has_declined) {
    for (int i = 0; i < 5; ++i) {
      if (!r.ReadCString(info.declined_names[i],
                         kDeclinedPetNameMaxBytesIncludingNul)) {
        info.declined_names[i].clear();
      }
    }
  }

  if (info.name.empty()) {
    info.found = false;
    pet_names_[info.pet_number] = info;
    if (db_cache_runtime_.cache().InvalidateEntry(
            openwow::data::WDBCacheType::PetName, info.pet_number)) {
      db_cache_runtime_.persistence().SetDirty(
          openwow::data::WDBCacheType::PetName);
    }
    auto callbacks = pet_name_queries_.Resolve(info.pet_number);
    for (auto &cb : callbacks) {
      cb(false);
    }
    return true;
  }

  info.found = true;

  const auto pet_number = info.pet_number;
  pet_names_[pet_number] = std::move(info);
  const auto &cached_info = pet_names_[pet_number];
  db_cache_runtime_.cache().UpdateEntry(
      openwow::data::WDBCacheType::PetName, pet_number,
      SerializePetNameWdbRecord(cached_info),
      openwow::data::wdb_format::kVersion_PetName);
  db_cache_runtime_.persistence().SetDirty(
      openwow::data::WDBCacheType::PetName);

  auto callbacks = pet_name_queries_.Resolve(pet_number);
  for (auto &cb : callbacks) {
    cb(true);
  }
  return true;
}

bool PetManager::HandleStabledPets(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);

  std::uint64_t npc_raw;
  if (!r.ReadU64(npc_raw))
    return false;
  stable_list_.npc_guid = ObjectGuid(npc_raw);

  std::uint8_t pet_count;
  if (!r.ReadU8(pet_count))
    return false;
  if (!r.ReadU8(stable_list_.max_slots))
    return false;

  stable_list_.pets.clear();
  stable_list_.pets.resize(pet_count);
  for (std::uint8_t i = 0; i < pet_count; ++i) {
    auto &p = stable_list_.pets[i];
    if (!r.ReadU32(p.pet_number))
      return false;
    if (!r.ReadU32(p.creature_id))
      return false;
    if (!r.ReadU32(p.level))
      return false;
    if (!r.ReadCString(p.name))
      return false;
    if (!r.ReadU8(p.flags))
      return false;
  }

  return true;
}

bool PetManager::HandlePetGuids(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);

  std::uint32_t count = 0;
  (void)r.ReadU32(count);

  pet_guids_.clear();
  pet_guids_.resize(count);

  std::uint64_t pet_guid = 0;
  for (std::uint32_t i = 0; i < count; ++i) {
    (void)r.ReadU64(pet_guid);
    pet_guids_[i] = pet_guid;
  }
  return true;
}

net::wotlk::WorldPacket PetManager::BuildPetNameQuery(std::uint32_t pet_number,
                                                      const ObjectGuid &guid) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_PET_NAME_QUERY);
  pkt.AppendU32(pet_number);
  pkt.AppendU64(guid.GetRawValue());
  return pkt;
}

net::wotlk::WorldPacket PetManager::BuildPetAction(const ObjectGuid &pet_guid,
                                                   std::uint32_t action_data,
                                                   const ObjectGuid &target) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_PET_ACTION);
  pkt.AppendU64(pet_guid.GetRawValue());
  pkt.AppendU32(action_data);
  pkt.AppendU64(target.GetRawValue());
  return pkt;
}

net::wotlk::WorldPacket PetManager::BuildPetCancelAura(const ObjectGuid &pet_guid,
                                                       std::uint32_t spell_id) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_PET_CANCEL_AURA);
  pkt.AppendU64(pet_guid.GetRawValue());
  pkt.AppendU32(spell_id);
  return pkt;
}

net::wotlk::WorldPacket
PetManager::BuildPetSetAction(const ObjectGuid &pet_guid,
                              std::optional<net::wotlk::PetSetActionSlotState> secondary_slot,
                              net::wotlk::PetSetActionSlotState target_slot) {

  return net::wotlk::PacketSender::BuildPetSetAction(pet_guid.GetRawValue(), secondary_slot,
                                                     target_slot);
}

const PetNameInfo *PetManager::GetPetName(std::uint32_t pet_number) const {
  auto it = pet_names_.find(pet_number);
  if (it != pet_names_.end()) {
    return &it->second;
  }

  const auto persisted = db_cache_runtime_.cache().Get(
      openwow::data::WDBCacheType::PetName, pet_number);
  if (!persisted.has_value()) {
    return nullptr;
  }

  const auto parsed = DeserializePetNameWdbRecord(pet_number, persisted->data);
  if (!parsed.has_value()) {
    return nullptr;
  }

  const auto [inserted_it, inserted] = pet_names_.emplace(pet_number, *parsed);
  if (!inserted) {
    inserted_it->second = *parsed;
  }
  return &inserted_it->second;
}

std::size_t PetManager::GetSpellbookSpellCount() const {

  if (pet_bar_.active || !pet_bar_.spellbook_spells.empty()) {
    std::size_t unique_count = 0;
    for (auto it = pet_bar_.spellbook_spells.begin(); it != pet_bar_.spellbook_spells.end(); ++it) {
      if (*it != 0 && std::find(pet_bar_.spellbook_spells.begin(), it, *it) == it) {
        ++unique_count;
      }
    }
    return unique_count;
  }
  return pet_bar_.spells.size();
}

std::uint32_t PetManager::GetSpellbookSpellId(const std::size_t slot) const {
  if (slot == 0) {
    return 0;
  }

  if (pet_bar_.active || !pet_bar_.spellbook_spells.empty()) {
    std::size_t visible_slot = 0;
    for (auto it = pet_bar_.spellbook_spells.begin(); it != pet_bar_.spellbook_spells.end(); ++it) {
      if (*it == 0 || std::find(pet_bar_.spellbook_spells.begin(), it, *it) != it) {
        continue;
      }
      if (++visible_slot == slot) {
        return *it;
      }
    }
    return 0;
  }

  if (slot > pet_bar_.spells.size()) {
    return 0;
  }
  return pet_bar_.spells[slot - 1].ActionId();
}

bool PetManager::HasSpellbookSpellId(const std::uint32_t spell_id) const {
  if (spell_id == 0) {
    return false;
  }

  if (!pet_bar_.active && pet_bar_.spellbook_spells.empty()) {
    return FindSpellEntryBySpellId(spell_id) != nullptr;
  }

  return std::find(pet_bar_.spellbook_spells.begin(), pet_bar_.spellbook_spells.end(), spell_id) !=
         pet_bar_.spellbook_spells.end();
}

bool PetManager::HasActionBarSpellId(const std::uint32_t spell_id) const {
  if (spell_id == 0) {
    return false;
  }
  return std::ranges::any_of(
      pet_bar_.action_bar, [spell_id](const PetActionButton& action) {
        return IsPetSpellActionKind(action.ActionKind()) &&
               action.ActionId() == spell_id;
      });
}

const PetActionButton *PetManager::FindSpellEntryBySpellId(const std::uint32_t spell_id) const {
  return FindLastPacketSpellEntryBySpellId(pet_bar_.spells, spell_id);
}

const PetActionButton *PetManager::GetSpellbookSpellEntry(const std::size_t slot) const {
  const auto spell_id = GetSpellbookSpellId(slot);
  return FindSpellEntryBySpellId(spell_id);
}

void PetManager::SyncSpellEntryFromActionBar(const PetActionButton &action_button) {
  if (action_button.ActionKind() != 1) {
    return;
  }

  for (auto it = pet_bar_.spells.rbegin(); it != pet_bar_.spells.rend(); ++it) {
    if ((it->raw & PetActionButton::kAutocastIdentityMask) ==
        (action_button.raw & PetActionButton::kAutocastIdentityMask)) {
      it->raw = action_button.raw;
      return;
    }
  }
}

std::optional<bool>
PetManager::SetSpellAutocastStateBySpellId(const std::uint32_t spell_id,
                                           const std::optional<bool> requested_enabled) {
  if (!pet_bar_.active || pet_bar_.guid.IsEmpty() || spell_id == 0) {
    return std::nullopt;
  }

  auto *spell = FindMutableSpellEntryBySpellId(spell_id);
  if (spell == nullptr || !spell->IsAutocastAllowed()) {
    return std::nullopt;
  }

  const bool enabled = requested_enabled.value_or(!spell->IsAutocastEnabled());
  const PetActionButton matching_entry = *spell;
  spell->SetAutocastEnabled(enabled);

  for (auto &slot : pet_bar_.action_bar) {
    if (slot.MatchesAutocastIdentity(matching_entry)) {
      slot.SetAutocastEnabled(enabled);
    }
  }

  return enabled;
}

std::optional<std::uint32_t>
PetManager::SetActionBarAutocastState(const std::size_t slot,
                                      const std::optional<bool> requested_enabled) {
  if (!pet_bar_.active || pet_bar_.guid.IsEmpty() || slot >= 10u) {
    return std::nullopt;
  }

  auto &action_button = pet_bar_.action_bar[slot];
  if (!action_button.IsAutocastAllowed()) {
    return std::nullopt;
  }

  const bool enabled = requested_enabled.value_or(!action_button.IsAutocastEnabled());
  action_button.SetAutocastEnabled(enabled);
  SyncSpellEntryFromActionBar(action_button);
  return action_button.raw;
}

void PetManager::SetLocalReactState(PetReactState react) {
  if (!pet_bar_.active) {
    return;
  }
  pet_bar_.react = react;
  pet_bar_.mode_packed = (pet_bar_.mode_packed & 0xFFFFFF00u) | static_cast<std::uint32_t>(react);
}

void PetManager::SetLocalCommandState(PetCommandState command) {
  if (!pet_bar_.active) {
    return;
  }
  pet_bar_.command = command;
  pet_bar_.mode_packed =
      (pet_bar_.mode_packed & 0xFFFF00FFu) | (static_cast<std::uint32_t>(command) << 8);
  if (command != PetCommandState::kAttack) {
    attack_command_active_ = false;
  }
}

ObjectGuid PetManager::GetPrimaryPetGuid() const {
  if (pet_guids_.empty()) {
    return pet_bar_.active ? pet_bar_.guid : ObjectGuid();
  }

  return ObjectGuid(pet_guids_.front());
}

bool PetManager::IsAttackActionSlot(const std::size_t slot) const {
  if (GetPrimaryPetGuid().IsEmpty() || slot >= std::size(pet_bar_.action_bar)) {
    return false;
  }

  const auto &action_button = pet_bar_.action_bar[slot];
  return action_button.raw != 0 && action_button.ActionKind() == 7u &&
         action_button.ActionId() == 2u;
}

void PetManager::SetAttackCommandActive(bool active) {
  attack_command_active_ = active;
}

bool PetManager::StopAttackIfActive(InteractionSender &interaction) {
  if (!attack_command_active_) {
    return false;
  }

  interaction.SendPetStopAttack(GetPrimaryPetGuid().GetRawValue());
  attack_command_active_ = false;
  return true;
}

bool PetManager::RefreshGeneratedBarState(const WorldSession &session) {
  const bool generated_bar_active = ComputeGeneratedPetBarActive(session, *this);
  const bool changed = pet_bar_.generated_bar_active != generated_bar_active;
  pet_bar_.generated_bar_active = generated_bar_active;
  return changed;
}

bool PetManager::HasActionBar(const WorldSession &session) const {
  return IsPetBarVisibleForSession(session, *this) && !pet_bar_.generated_bar_active;
}

void PetManager::ResetStableListState() {
  stable_list_ = StableListInfo{};
  ResetStablePetSelection();
}

const PetNameInfo *PetManager::GetOrRequestPetName(std::uint32_t pet_number,
                                                   std::uint64_t guid) {
  return GetOrRequestPetName(pet_number, PetNameRequestOptions{.context = guid});
}

const PetNameInfo *PetManager::GetOrRequestPetName(std::uint32_t pet_number,
                                                   PetNameRequestOptions options) {
  if (pet_number == 0) {
    return nullptr;
  }

  const PetNameInfo *cached = GetPetName(pet_number);
  if (cached) {
    return cached;
  }

  const std::uint32_t tick =
      pet_name_tick_provider_ ? pet_name_tick_provider_() : 0;
  pet_name_queries_.Request(pet_number, tick, std::move(options));
  return nullptr;
}

bool PetManager::IsPetNameQueryPending(std::uint32_t pet_number) const {
  return pet_name_queries_.IsPending(pet_number);
}

void PetManager::MarkPetNameQueryPending(std::uint32_t pet_number) {
  const std::uint32_t tick =
      pet_name_tick_provider_ ? pet_name_tick_provider_() : 0;
  pet_name_queries_.MarkPending(pet_number, tick);
}

void PetManager::SetPetNameQueryDispatcher(PetNameQueryDispatchFn dispatcher) {
  pet_name_queries_.SetDispatcher(std::move(dispatcher));
}

void PetManager::PumpPetNameQueries(std::uint32_t current_tick_ms) {
  pet_name_queries_.Pump(current_tick_ms);
}

void PetManager::ClearPendingNameQueriesOnLogout() {
  pet_name_queries_.Clear();
}

void PetManager::ClearNameCacheForClientCacheVersion() {
  pet_names_.clear();
  pet_name_queries_.Clear();
}

void PetManager::SetPetNameQueryMaxInFlight(std::uint32_t max_in_flight) {
  pet_name_queries_.SetMaxInFlight(max_in_flight);
}

void PetManager::SetPetNameTickCountProvider(
    std::function<std::uint32_t()> provider) {
  pet_name_tick_provider_ = std::move(provider);
}

void PetManager::CancelPetNameCallback(std::uint32_t pet_number,
                                       PetNameCallbackKey key) {
  pet_name_queries_.CancelCallback(pet_number, key);
}

void PetManager::CancelPetNameCallbacks(PetNameCallbackKey key) {
  pet_name_queries_.CancelCallbacks(key);
}

PetActionButton *PetManager::FindMutableSpellEntryBySpellId(const std::uint32_t spell_id) {
  return FindLastPacketSpellEntryBySpellId(pet_bar_.spells, spell_id);
}

void PetManager::Clear() {
  pet_bar_ = PetBarState{};
  SpellBookFrame::RebuildPetSpellGroups({});
  last_feedback_ = PetFeedback::kNone;
  last_feedback_spell_id_ = 0;
  last_pet_cast_spell_ = 0;
  last_pet_cast_result_ = 0;
  attack_command_active_ = false;
  pet_names_.clear();
  pet_name_queries_.Clear();
  ResetStableListState();
  pet_guids_.clear();
}

void PetManager::ResetPetBarForEnterWorld() {
  pet_bar_ = PetBarState{};
  pet_guids_.clear();
  attack_command_active_ = false;
  SpellBookFrame::RebuildPetSpellGroups({});
}

}
