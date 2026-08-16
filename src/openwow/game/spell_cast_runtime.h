#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/spell_public_values.h"
#include "openwow/game/spell_targeting.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace openwow::game {

class WorldSession;

enum class SpellSlotType : std::uint8_t {
  kCurrent = 0,
  kAutoRepeat = 1,
  kChannel = 2,
  kCount = 3,
};

enum class SpellClientState : std::uint8_t {
  kIdle = 0,
  kActive,
};

struct ActiveSpellInfo {
  std::uint32_t spell_id = 0;
  std::uint8_t cast_count = 0;
  ObjectGuid caster_guid;
  ObjectGuid target_guid;
  SpellClientState state = SpellClientState::kIdle;
};

struct SpellCastTargets {
  std::uint32_t target_mask = 0;
  ObjectGuid unit_target;
  ObjectGuid object_target;
  ObjectGuid item_target;
  ObjectGuid source_transport;
  float source_x = 0.0f;
  float source_y = 0.0f;
  float source_z = 0.0f;
  ObjectGuid destination_transport;
  float destination_x = 0.0f;
  float destination_y = 0.0f;
  float destination_z = 0.0f;
  std::string string_target;
  float trajectory_pitch = 0.0f;
  float trajectory_speed = 0.0f;
};

enum class SpellCastOrigin : std::uint8_t {
  kPlayer,
  kPet,
};

struct SpellCastCommand {
  SpellCastOrigin origin = SpellCastOrigin::kPlayer;
  ObjectGuid caster_guid;
  std::uint32_t spell_id = 0;
  std::uint8_t cast_count = 0;
  std::uint8_t cast_flags = 0;
  SpellCastTargets targets;
};

using PacketSendFunc = std::function<bool(const SpellCastCommand& command)>;

class SpellCastRuntime {
 public:
  SpellCastRuntime();

  void SetSendCastSpell(PacketSendFunc send);

  SpellCastResult CastSpell(const WorldSession& session,
                            std::uint32_t spell_id,
                            std::uint64_t target_guid = 0,
                            std::uint8_t cast_flags = 0);
  SpellCastResult CastTriggeredSpell(const WorldSession& session,
                                     std::uint32_t spell_id,
                                     std::uint64_t target_guid = 0,
                                     std::uint8_t cast_flags = 0);
  SpellCastResult CastPetSpell(const WorldSession& session,
                               ObjectGuid pet_guid,
                               std::uint32_t spell_id,
                               std::uint64_t target_guid = 0,
                               std::uint32_t* blocking_mechanic_out = nullptr);
  [[nodiscard]] SpellCastResult ValidatePlayerCastRequest(
      const WorldSession& session, std::uint32_t spell_id) const;

  void OnSpellStart(std::uint32_t spell_id, std::uint8_t cast_count,
                    const ObjectGuid& caster, const ObjectGuid& target);
  void OnSpellGo(std::uint32_t spell_id, std::uint8_t cast_count);
  void OnSpellFailed(std::uint32_t spell_id, std::uint8_t cast_count);

  void OnChannelStart(const ObjectGuid& caster_guid, std::uint32_t spell_id);
  void OnChannelStop();

  void StartAutoRepeat(std::uint32_t spell_id);
  void StopAutoRepeat(WorldSession& session);
  void SetAutoAttackSpellId(std::uint32_t spell_id);

  void SetUnitTarget(SpellSlotType slot, const ObjectGuid& unit);
  void SetObjectTarget(SpellSlotType slot, const ObjectGuid& object,
                       std::uint32_t packet_target_mask);
  void SetSourcePosition(SpellSlotType slot, const ObjectGuid& transport,
                         float x, float y, float z);
  void SetDestinationPosition(SpellSlotType slot, const ObjectGuid& transport,
                              float x, float y, float z);
  void SetItemTarget(SpellSlotType slot, const ObjectGuid& item);
  void SetTradeItemTarget(SpellSlotType slot, const ObjectGuid& item);

  [[nodiscard]] bool PrepareTargetedCast(
      std::uint32_t spell_id,
      SpellCastOrigin origin = SpellCastOrigin::kPlayer,
      ObjectGuid caster_guid = {});
  [[nodiscard]] SpellCastResult CastPreparedSpell(
      const WorldSession& session, std::uint8_t cast_flags = 0);
  [[nodiscard]] ObjectGuid GetPreparedTarget(std::uint32_t spell_id) const;
  [[nodiscard]] SpellCastOrigin GetPreparedOrigin(
      std::uint32_t spell_id) const;
  [[nodiscard]] ObjectGuid GetPreparedCaster(
      std::uint32_t spell_id) const;
  void DiscardPreparedTargetedCast(std::uint32_t spell_id);

  [[nodiscard]] ActiveSpellInfo GetSlot(SpellSlotType slot) const;
  [[nodiscard]] bool IsSlotActive(SpellSlotType slot) const;
  [[nodiscard]] bool IsCasting() const;
  [[nodiscard]] bool IsChanneling() const;
  [[nodiscard]] bool IsAutoRepeating() const;
  [[nodiscard]] std::uint32_t GetCurrentSpellId() const;
  [[nodiscard]] std::uint32_t GetAutoRepeatSpellId() const;
  [[nodiscard]] std::uint32_t GetAutoAttackSpellId() const;
  [[nodiscard]] std::uint32_t GetChanneledSpellId() const;
  void SetRememberedHeldItemPetTargetSpellId(std::uint32_t spell_id);
  [[nodiscard]] std::uint32_t GetRememberedHeldItemPetTargetSpellId() const;

  void CancelSpell(WorldSession& session, SpellSlotType slot);

  void CancelSpellCastsTargeting(WorldSession& session,
                                 const ObjectGuid& target);

  void CancelAllLocalPlayerCasts(WorldSession& session);
  void SendCancelChannelling(WorldSession& session, std::uint32_t spell_id);

  [[nodiscard]] SpellTargeting& GetTargeting();
  [[nodiscard]] const SpellTargeting& GetTargeting() const;

  void Reset();

 private:
  struct PreparedCast {
    std::uint32_t spell_id = 0;
    SpellCastOrigin origin = SpellCastOrigin::kPlayer;
    ObjectGuid caster_guid;
    SpellCastTargets targets;
  };

  [[nodiscard]] SpellCastTargets BuildTargets(
      const WorldSession& session, std::uint64_t target_guid) const;
  SpellCastResult ExecuteCast(const WorldSession& session,
                              SpellCastOrigin origin,
                              ObjectGuid caster_guid,
                              std::uint32_t spell_id,
                              std::uint64_t target_guid,
                              std::uint8_t cast_flags);
  void StopAutoRepeat(WorldSession& session, bool send_cancel_packet);
  void FireAutoRepeatStateChange(bool was_active) const;

  std::array<ActiveSpellInfo, static_cast<std::size_t>(SpellSlotType::kCount)>
      slots_{};
  PreparedCast prepared_cast_;

  std::uint8_t next_cast_count_ = 1;
  std::uint32_t auto_attack_spell_id_ = 0;
  std::uint32_t remembered_held_item_pet_target_spell_id_ = 0;
  PacketSendFunc send_cast_spell_;
  SpellTargeting targeting_;
};

void HandleMovementActivatedSpellInterrupts(WorldSession& session);

}
