#pragma once

#include "openwow/game/object_guid.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace openwow::game {

class CGUnit_C;
class ObjectManager;
class WorldSession;
struct SpellGroundClickData;

bool ConfirmSpellGroundTarget(const WorldSession& session,
                              const SpellGroundClickData& click);

bool SpellAction_PlaceGlyphInSocket(WorldSession& session, int socket_index);

void SpellAction_CastTalentGroupSpell(const WorldSession& session,
                                      std::uint32_t spell_id, int a4, int a5,
                                      int a6);

bool SpellAction_TryAssignTargetByGuid(WorldSession& session,
                                       std::uint64_t guid);

bool SpellAction_TryCastHeldItemOnPet(const WorldSession& session,
                                      const CGUnit_C* item_obj,
                                      const CGUnit_C* target_unit);

using HeldItemPetSpellResolver = bool (*)(const WorldSession& session,
                                           std::uint32_t spell_id);
void SpellAction_SetHeldItemPetSpellResolver(HeldItemPetSpellResolver resolver);
void SpellAction_ResetHeldItemPetSpellResolver();

using PeriodicClientSpellTickCountProvider = std::uint32_t (*)();
using PeriodicClientSpellTargetResolver = bool (*)(const ObjectManager& objects,
                                                   ObjectGuid target_guid);
using PeriodicClientSpellExecutor = bool (*)(const WorldSession& session,
                                             std::uint32_t trigger_spell_id,
                                             ObjectGuid target_guid);

struct PeriodicClientSpellHooks {
  PeriodicClientSpellTickCountProvider tick_count = nullptr;
  PeriodicClientSpellTargetResolver target_resolver = nullptr;
  PeriodicClientSpellExecutor executor = nullptr;
};

void SpellAction_SchedulePeriodicClientSpell(ObjectGuid target_guid, std::uint32_t owner_spell_id,
                                              std::uint32_t trigger_spell_id,
                                              std::uint32_t interval_ms);

void SpellAction_CancelPeriodicClientSpell(ObjectGuid target_guid,
                                            std::uint32_t owner_spell_id_filter);

void SpellAction_SetPeriodicClientSpellHooks(PeriodicClientSpellHooks hooks);
void SpellAction_ResetPeriodicClientSpellHooks();
void SpellAction_ResetPeriodicClientSpellTimers();
[[nodiscard]] std::size_t SpellAction_GetPeriodicClientSpellTimerCount();

int SpellAction_ProcessPeriodicClientSpellTimers(const WorldSession& session);

struct PendingItemSpellHistoryEntry {
  std::int32_t item_entry_id = 0;
  std::int32_t spell_id = 0;
  std::int32_t auxiliary_value = 0;
  std::uint8_t cast_flag = 0;
};

PendingItemSpellHistoryEntry *SpellAction_FindOrCreatePendingItemSpellHistory(int item_entry_id,
                                                                               int spell_id,
                                                                               int auxiliary_value,
                                                                               int cast_flag);

void SpellAction_Update(const WorldSession& session, float dt);

bool SpellAction_ResolveTargeting(const WorldSession& session,
                                  std::uint32_t spell_id,
                                  std::uint64_t explicit_target_guid,
                                  int cast_flags);

bool SpellAction_ValidateAndInitiateCast(const WorldSession& session,
                                         std::uint32_t spell_id,
                                         std::uint64_t target_guid,
                                         int item_slot, int cast_flags);

bool SpellAction_ValidateAndInitiatePetCast(const WorldSession& session,
                                            ObjectGuid pet_guid,
                                            std::uint32_t spell_id,
                                            ObjectGuid target_guid);

void SpellAction_DisplaySpellFailure(const WorldSession& session,
                                     std::uint32_t spell_id,
                                     ObjectGuid caster_guid,
                                     std::uint32_t error_code,
                                     const std::string& substitution = {});

namespace detail {

void ClearPendingItemSpellHistory();
[[nodiscard]] std::size_t GetPendingItemSpellHistoryCount();
[[nodiscard]] std::optional<PendingItemSpellHistoryEntry>
GetPendingItemSpellHistoryEntry(int item_entry_id);

}

}
