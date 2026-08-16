#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/spell_runtime_values.h"

#include <cstdint>
#include <optional>

namespace openwow::data::dbc {
class DbcLoader;
struct SpellEntry;
struct SpellVisualEntry;
}

namespace openwow::game {

class CGItem_C;
class CGObject_C;
class CGPlayer_C;
class CGUnit_C;
class ItemDefinitions;
class ObjectManager;
class PlayerInventoryReplica;
class SpellCastRuntime;
class WorldSession;

void ItemCooldownList_Method(std::uintptr_t list);

void DestLocSpellCastList_Method(std::uintptr_t list);

void SpellC_OnWorldEnter();

void SpellC_ResetMeleeAttackCastFailureReason();

void SpellC_CleanupExpiredMacros(std::uint32_t current_time);

void PendingSpellCast_Cleanup(std::uintptr_t list);

void PeriodicClientTrigger_Cleanup(std::uintptr_t list);

void SpellHistory_FreeOwnedNodes(std::uintptr_t list_root_addr);

void SpellHistory_CleanupBucket(std::uintptr_t bucket_addr);

void SpellHistory_Record(const WorldSession& session,
                          std::uintptr_t ,
                          std::uint32_t spell_id,
                          std::uint32_t cast_id,
                          std::uint64_t caster_guid,
                          std::uint32_t duration,
                          std::uint32_t cooldown,
                          std::uint32_t category_cooldown,
                          bool is_channel,
                          std::uint32_t item_id,
                          std::uint32_t flags);

void DisplayCastError(WorldSession& session, std::uintptr_t spell_rec,
                       std::uintptr_t spell_entry,
                       std::uint32_t error_code, std::uint32_t extra1,
                       std::uint32_t extra2);

bool IsCurrentItemEntry(const WorldSession& session,
                        std::uint32_t item_entry_id);

bool IsCurrentSpellId(const WorldSession& session, std::uint32_t spell_id);

[[nodiscard]] bool IsSpellRecordCurrentForUnit(
    const openwow::data::dbc::SpellEntry& spell,
    const openwow::data::dbc::DbcLoader& dbc_loader,
    const CGUnit_C* unit);

void CancelOrCompleteCast(WorldSession& session,
                           std::uintptr_t spell_entry, bool update_ui,
                           bool send_cancel, std::uint32_t reason);

void CancelAutocastSpells(WorldSession& session);

void CancelPendingCastsByGuid(WorldSession& session, std::uint64_t guid);

void CancelPendingCastsForActivePlayer(WorldSession& session);

int HandleSpellStartPacket_806700(const WorldSession& session,
                                  std::uintptr_t data_store,
                                  std::uintptr_t spell_entry);

int HandleSpellFailurePacket(WorldSession& session,
                              std::uintptr_t a1, std::uintptr_t a2,
                              std::uintptr_t a3, std::uintptr_t data_store);

int HandleCooldownEventPacket(std::uintptr_t a1, std::uintptr_t a2,
                               std::uintptr_t a3, std::uintptr_t data_store);

}
