#include "openwow/game/objects/cgunit.h"

#include "openwow/game/object_manager.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/object_types.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/barber_shop.h"
#include "openwow/game/interaction_range.h"
#include "openwow/game/threat_system.h"
#include "openwow/game/unit_sound_dispatch.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/net/serialization/cdatastore_ops.h"

namespace openwow::game {

int CGUnit_C::OnGroupMemberUpdate(std::uint64_t guid, int opcode, int a3,
                                  const std::uint8_t *packet) {

  (void)guid;
  (void)opcode;
  (void)a3;
  (void)packet;
  return 1;
}

void CGUnit_C::HandleAppearanceUpdatePacket(const std::uint8_t * ) {

}

int CGUnit_C::OpcodeHandler_AppearanceUpdate(ObjectManager &objects,
                                             std::int32_t ,
                                             const std::uint8_t *packet) {
  if (!packet) {
    return 1;
  }

  auto &store =
      *reinterpret_cast<net::CDataStore *>(const_cast<std::uint8_t *>(packet));
  std::uint64_t guid = 0;
  net::CDataStore_GetUInt64(store, &guid);

  auto *object = CGObject_HasFlags(objects, guid, 8);
  if (object && object->IsUnit()) {
    auto *unit = static_cast<CGUnit_C *>(object);

    unit->HandleAppearanceUpdatePacket(packet);
  }

  return 1;
}

bool CGUnit_C::GetGroupMemberStatus(const std::uint64_t *member_guid, std::uint8_t *status_plus_one,
                                    std::uint8_t *raw_percent, float *scaled_percent,
                                    std::uint32_t *threat_value) const {
  if (!member_guid) {
    if (status_plus_one)
      *status_plus_one = 0;
    if (raw_percent)
      *raw_percent = 0;
    if (scaled_percent)
      *scaled_percent = 0.0f;
    if (threat_value)
      *threat_value = 0;
    return false;
  }

  ThreatQueryData query;
  const ObjectGuid member(*member_guid);
  const bool has_live_threat = ThreatSystem::Get().TryGetThreatQueryData(GetGuid(), member, &query);

  if (!has_live_threat) {
    if (status_plus_one) {
      *status_plus_one =
          query.has_entry ? static_cast<std::uint8_t>(query.entry.threat_status + 1) : 0;
    }
    if (raw_percent)
      *raw_percent = 0;
    if (scaled_percent)
      *scaled_percent = 0.0f;
    if (threat_value)
      *threat_value = 0;
    return false;
  }

  if (status_plus_one) {
    *status_plus_one = static_cast<std::uint8_t>(query.entry.threat_status + 1);
  }
  if (raw_percent) {
    *raw_percent = query.entry.raw_percent;
  }
  if (threat_value) {
    *threat_value = query.entry.threat_value;
  }

  if (scaled_percent) {
    if (member == query.highest_guid) {
      *scaled_percent = 100.0f;
    } else {
      const auto* const objects = object_manager();
      const auto *const other_unit =
          objects != nullptr ? objects->GetUnit(member) : nullptr;
      const float scale =
          other_unit != nullptr && GetSquaredDistanceToPosition(other_unit->GetPosition()) <=
                                       interaction_range::ComputeUnitInteractionRangeSquared(
                                           State().GetCombatReach(), other_unit->State().GetCombatReach())
              ? 0.90909088f
              : 0.76923078f;
      *scaled_percent = static_cast<float>(query.entry.raw_percent) * scale;
    }
  }

  return true;
}

bool HandleUnitNavigationQuery(ObjectManager &objects,
                               const std::uint64_t guid) {
  const auto *const unit = objects.GetUnit(ObjectGuid(guid));
  return unit != nullptr && unit->Movement().IsNavigableAsPlayer();
}

int CGUnit_C::HandleUnitFunc3(ObjectManager &objects,
                              const std::uint64_t guid) {
  auto *const unit = objects.GetMutableUnit(ObjectGuid(guid));
  if (unit == nullptr) {
    return 1;
  }
  const std::uint8_t new_tier = unit->State().GetAnimTier();
  if (unit->Animation().UpdateCachedAnimationTier(new_tier)) {
    ui::game::ScriptEventDispatch::Get().FireUnitClassification(guid);
  }
  return 1;
}

int CGUnit_C::HandleUnitFunc4(ObjectManager &objects, WorldSession &session,
                              const std::uint64_t guid) {
  if (guid == objects.GetActivePlayerGuid().GetRawValue()) {
    static_cast<void>(BarberShop::Get().Cancel(session));
  }
  if (auto *const unit = objects.GetMutableUnit(ObjectGuid(guid));
      unit != nullptr) {
    unit->Presentation().RefreshActiveDisplayRuntimeState();
    unit->Presentation().OnDisplayIdChanged();
  }
  return 1;
}

int CGUnit_C::OnTargetFieldChanged(ObjectManager &objects,
                                   const std::uint64_t guid) {

  if (auto *const unit = objects.GetMutableUnit(ObjectGuid(guid));
      unit != nullptr) {
    unit->SetTargetChangeTimeMs(core::GameClock::GetTickCount32());
  }
  return 1;
}
int CGUnit_C::HandleUnitFunc6(const std::uint8_t *) { return 1; }
int CGUnit_C::HandleUnitFunc7(const std::uint8_t *) { return 1; }

int CGUnit_C::HandleUnitFunc8(ObjectManager &objects,
                              const std::uint8_t *packet) {
  if (packet == nullptr) {
    return 1;
  }
  PacketReader reader(packet, 12);
  std::uint64_t guid = 0;
  std::uint32_t action_type = 0;
  if (!reader.ReadU64(guid) || !reader.ReadU32(action_type)) {
    return 1;
  }
  if (const auto *const unit = objects.GetUnit(ObjectGuid(guid));
      unit != nullptr) {
    UnitSound_PlayPetActionSound(*unit, action_type);
  }
  return 1;
}

int CGUnit_C::HandleUnitFunc9(const std::uint8_t *) { return 1; }
int CGUnit_C::HandleUnitFunc10(const std::uint8_t *) { return 1; }
int CGUnit_C::HandleUnitFunc11(const std::uint8_t *) { return 1; }

int CGUnit_C::HandleLootListOpcode(ObjectManager &objects, std::uint64_t, int,
                                   int, const std::uint8_t *packet) {
  if (packet == nullptr) {
    return 1;
  }
  PacketReader reader(packet, 4096);
  ObjectGuid creature_guid;
  if (!reader.ReadGuid(creature_guid)) {
    return 1;
  }
  auto *const object = CGObject_HasFlags(
      objects, creature_guid.GetRawValue(),
      static_cast<std::uint32_t>(kTypeMaskUnit));
  if (object != nullptr) {
    static_cast<CGUnit_C *>(object)->Loot().StoreLootListGuids(reader);
  }
  return 1;
}

int OnUnitMoveCounterUpdate(ObjectManager &objects, const std::uint64_t guid,
                            int, int, const std::uint8_t *) {
  if (auto *const unit = objects.GetMutableUnit(ObjectGuid(guid));
      unit != nullptr) {
    unit->Movement().IncrementMoveSequence(1);
  }
  return 1;
}

}
