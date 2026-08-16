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
class WorldSession;

void StopActiveCast(WorldSession& session);

void CancelPendingSpellCast(WorldSession& session);

bool ValidateSpellTarget(WorldSession& session, std::uintptr_t caster,
                          std::uintptr_t spell_rec,
                          std::uint64_t target_guid, std::uint8_t* out_result,
                          bool show_error, std::uintptr_t extra);

int HandleSpellDelayedPacket(WorldSession& session,
                              std::uintptr_t a1, std::uintptr_t a2,
                              std::uintptr_t a3, std::uintptr_t data_store);

int ComputeMissileTrajectory(std::uintptr_t visual_kit, std::uintptr_t caster,
                              std::uintptr_t spell_entry, std::uintptr_t spell_rec,
                              std::uintptr_t target_list,
                              std::uintptr_t target_count_ptr,
                              float* out_pitch, float* out_speed);

std::uintptr_t AllocItemCooldownHashNode(std::uintptr_t callback,
                                          std::uint32_t extra_size,
                                          bool zero_fill);

std::uintptr_t AllocDestLocSpellCastHashNode(std::uintptr_t callback,
                                              std::uint32_t extra_size,
                                              bool zero_fill);

void OnItemTemplateCooldownCacheLoaded(WorldSession& session,
                                        std::uint32_t entry_id,
                                        std::uint32_t unused1,
                                        std::uint32_t unused2,
                                        bool success);

bool FillItemCooldownByEntry(WorldSession& session, std::uint32_t entry_id,
                              std::uint32_t* out_duration,
                              std::uint32_t* out_start_time,
                              bool* out_enabled);

bool FillItemCooldownByInventoryItem(const CGItem_C& item,
                                      std::uint32_t* out_duration,
                                      std::uint32_t* out_start_time,
                                      bool* out_enabled);

}
