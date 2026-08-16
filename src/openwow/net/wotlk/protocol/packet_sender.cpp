
#include "openwow/net/wotlk/protocol/packet_sender.h"

#include "openwow/game/commentator_state.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace openwow::net::wotlk {

namespace {

void AppendNullString(WorldPacket &pkt, std::string_view str) {
  pkt.AppendString(str);
}

void AppendCommentatorInstanceKeyTail(WorldPacket &pkt,
                                      const game::CommentatorInstanceKey &instance_key) {
  pkt.AppendU32(instance_key.tail.key_u32);
  pkt.AppendU16(instance_key.tail.key_u16);
  pkt.AppendU8(instance_key.tail.key_u8);
}

WorldPacket BuildCommentatorSkirmishQueueCommand(const std::uint32_t sub_opcode,
                                                 const std::uint32_t argument_value,
                                                 const std::uint64_t first_guid,
                                                 const std::uint64_t second_guid,
                                                 const std::uint32_t tail_value) {
  WorldPacket pkt(Opcode::CMSG_COMMENTATOR_SKIRMISH_QUEUE_COMMAND);
  pkt.AppendU32(sub_opcode);
  pkt.AppendU32(argument_value);
  pkt.AppendU64(first_guid);
  pkt.AppendU64(second_guid);
  pkt.AppendU32(tail_value);
  return pkt;
}

std::string SanitizeGuildScriptText(std::string_view text,
                                    const std::size_t max_copied_bytes,
                                    const std::size_t truncate_on_logical_char) {
  if (max_copied_bytes == 0) {
    return {};
  }

  std::string sanitized;
  sanitized.reserve(std::min(text.size(), max_copied_bytes));

  for (const char ch : text) {
    if (ch == '\0') {
      break;
    }
    if (sanitized.size() >= max_copied_bytes) {
      break;
    }
    if (ch == '|') {
      continue;
    }
    sanitized.push_back(ch);
  }

  std::size_t logical_length = 0;
  for (std::size_t i = 0; i < sanitized.size(); ++i) {
    const auto byte = static_cast<std::uint8_t>(sanitized[i]);
    if ((byte & 0xC0u) == 0x80u) {
      continue;
    }

    ++logical_length;
    if (logical_length == truncate_on_logical_char) {
      sanitized.resize(i);
      break;
    }
  }

  return sanitized;
}

std::string SanitizeGuildMotdText(std::string_view text) {
  return SanitizeGuildScriptText(text, 0x204 - 1, 129);
}

std::string SanitizeGuildInfoText(std::string_view text) {
  return SanitizeGuildScriptText(text, 0x7D4 - 1, 501);
}

std::uint32_t FloatBits(const float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float BitsToFloat(const std::uint32_t bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

constexpr std::array<std::uint32_t, 16> kSpellTrajectoryHashNibbleTable = {
    0x486E26EEu, 0xDCAA16B3u, 0xE1918EEFu, 0x202DAFDBu,
    0x341C7DC7u, 0x1C365303u, 0x40EF2D37u, 0x65FD5E49u,
    0xD6057177u, 0x904ECE93u, 0x1C38024Fu, 0x98FD323Bu,
    0xE3061AE7u, 0xA39B0FA1u, 0x9797F25Fu, 0xE4444563u,
};

std::uint32_t HashSpellTrajectoryWord(const std::uint32_t word) {
  std::uint32_t hash = 0x7FED7FEDu;
  std::uint32_t accumulator = 0xEEEEEEEEu;

  for (int shift = 0; shift < 32; shift += 8) {
    const auto value = static_cast<std::uint8_t>(word >> shift);
    hash = (kSpellTrajectoryHashNibbleTable[value >> 4] -
            kSpellTrajectoryHashNibbleTable[value & 0x0Fu]) ^
           (hash + accumulator);
    accumulator += value + (accumulator << 5) + hash + 3u;
  }

  return hash == 0 ? 1u : hash;
}

std::uint32_t ComputeTrajectoryParityBit(const std::uint32_t cleared_value) {
  std::uint32_t folded = cleared_value ^ (cleared_value >> 16);
  folded ^= folded >> 8;
  folded ^= folded >> 4;
  return (0x28D7u >> (folded & 0x0Fu)) & 1u;
}

std::uint32_t WriteTrajectoryParityBit(const std::uint32_t value, const std::uint32_t bit_mask) {
  const std::uint32_t cleared_value = value & ~bit_mask;
  return cleared_value | (ComputeTrajectoryParityBit(cleared_value) * bit_mask);
}

void WriteSpellTargetsPayload(WorldPacket &pkt, const SpellTargets &targets) {
  pkt.AppendU32(targets.target_mask);

  constexpr std::uint32_t kObjectTargetMask =
      kTargetFlagUnit | kTargetFlagPvpCorpse | kTargetFlagGameObject |
      kTargetFlagCorpseAlly | kTargetFlagUnitMinipet;
  if (targets.target_mask & kObjectTargetMask) {
    AppendPackedGuid(pkt, (targets.target_mask & kTargetFlagGameObject) != 0
                              ? targets.go_target
                              : targets.unit_target);
  }

  if (targets.target_mask & (kTargetFlagItem | kTargetFlagTradeItem)) {
    AppendPackedGuid(pkt, targets.item_target);
  }

  if (targets.target_mask & kTargetFlagSourceLocation) {
    AppendPackedGuid(pkt, targets.src_transport);
    pkt.AppendFloat(targets.src_x);
    pkt.AppendFloat(targets.src_y);
    pkt.AppendFloat(targets.src_z);
  }

  if (targets.target_mask & kTargetFlagDestLocation) {
    AppendPackedGuid(pkt, targets.dst_transport);
    pkt.AppendFloat(targets.dst_x);
    pkt.AppendFloat(targets.dst_y);
    pkt.AppendFloat(targets.dst_z);
  }

  if (targets.target_mask & kTargetFlagString) {
    std::array<std::uint8_t, kSpellTargetStringCapacity> string_target{};
    const auto terminator = targets.str_target.find('\0');
    const auto length = std::min({targets.str_target.size(), terminator,
                                  kSpellTargetStringCapacity - 1});
    std::copy_n(targets.str_target.begin(), length, string_target.begin());
    pkt.AppendBytes(string_target.data(), string_target.size());
  }
}

SpellTargets ObfuscateSpellTrajectoryFields(const SpellTargets &targets) {
  SpellTargets obfuscated = targets;

  std::uint32_t pitch_bits = FloatBits(obfuscated.trajectory_pitch);
  std::uint32_t speed_bits = FloatBits(obfuscated.trajectory_speed);
  std::array<std::uint32_t, 3> source_bits = {
      FloatBits(obfuscated.src_x),
      FloatBits(obfuscated.src_y),
      FloatBits(obfuscated.src_z),
  };
  std::array<std::uint32_t, 3> destination_bits = {
      FloatBits(obfuscated.dst_x),
      FloatBits(obfuscated.dst_y),
      FloatBits(obfuscated.dst_z),
  };

  static std::uint32_t trajectory_obfuscation_counter = 0u;
  const std::uint32_t hash_input =
      ((pitch_bits & 0x07010CF0u) ^ trajectory_obfuscation_counter ^
       (speed_bits & 0x00250F00u) ^ (source_bits[0] & 0x0102F000u) ^
       (source_bits[1] & 0x300FE630u) ^ (source_bits[2] & 0x0100F000u) ^
       (destination_bits[0] & 0xA8070F00u) ^
       (destination_bits[1] & 0x070F0500u)) |
      0x01010101u;
  ++trajectory_obfuscation_counter;
  const std::uint32_t hash = HashSpellTrajectoryWord(hash_input);

  pitch_bits = (pitch_bits & 0xFFFFFFFCu) | ((hash >> 10) & 1u) | ((hash >> 27) & 2u);
  speed_bits = (speed_bits & 0xFFFFFFF6u) | ((hash >> 1) & 1u) | ((hash >> 19) & 8u);
  source_bits[0] =
      (source_bits[0] & 0xFFFFFFF9u) | (((hash & 0x8000u) | ((hash >> 9) & 0x10000u)) >> 14);
  source_bits[1] ^= (((hash >> 1) ^ source_bits[1]) & 0x2u);
  source_bits[1] ^= ((source_bits[1] ^ (hash >> 11)) & 0x8u);
  source_bits[2] ^= (((hash >> 6) ^ source_bits[2]) & 0x8u);
  source_bits[2] ^= ((source_bits[2] ^ (hash >> 28)) & 0x4u);
  destination_bits[0] =
      (destination_bits[0] & 0xFFFFFFF9u) | ((hash >> 6) & 0x2u) | ((hash >> 3) & 0x4u);
  destination_bits[1] ^= ((destination_bits[1] ^ (hash >> 29)) & 0x1u);
  destination_bits[1] ^= (((hash >> 3) ^ destination_bits[1]) & 0x8u);
  destination_bits[2] ^= ((destination_bits[2] ^ (hash >> 20)) & 0x1u);
  destination_bits[2] ^= (((hash >> 10) ^ destination_bits[2]) & 0x4u);

  pitch_bits = WriteTrajectoryParityBit(pitch_bits, 0x8u);
  speed_bits = WriteTrajectoryParityBit(speed_bits, 0x2u);
  source_bits[0] = WriteTrajectoryParityBit(source_bits[0], 0x8u);
  source_bits[1] = WriteTrajectoryParityBit(source_bits[1], 0x4u);
  source_bits[2] = WriteTrajectoryParityBit(source_bits[2], 0x1u);
  destination_bits[0] = WriteTrajectoryParityBit(destination_bits[0], 0x1u);
  destination_bits[1] = WriteTrajectoryParityBit(destination_bits[1], 0x4u);
  destination_bits[2] = WriteTrajectoryParityBit(destination_bits[2], 0x2u);

  obfuscated.trajectory_pitch = BitsToFloat(pitch_bits);
  obfuscated.trajectory_speed = BitsToFloat(speed_bits);
  obfuscated.src_x = BitsToFloat(source_bits[0]);
  obfuscated.src_y = BitsToFloat(source_bits[1]);
  obfuscated.src_z = BitsToFloat(source_bits[2]);
  obfuscated.dst_x = BitsToFloat(destination_bits[0]);
  obfuscated.dst_y = BitsToFloat(destination_bits[1]);
  obfuscated.dst_z = BitsToFloat(destination_bits[2]);
  return obfuscated;
}

void AppendSpellTrajectoryPayload(WorldPacket &pkt, const std::uint8_t cast_flags,
                                   const SpellTargets &targets) {
  if ((cast_flags & kClientSpellCastFlagHasTrajectory) == 0) {
    WriteSpellTargetsPayload(pkt, targets);
    return;
  }

  const SpellTargets obfuscated_targets =
      ObfuscateSpellTrajectoryFields(targets);
  WriteSpellTargetsPayload(pkt, obfuscated_targets);
  pkt.AppendFloat(obfuscated_targets.trajectory_pitch);
  pkt.AppendFloat(obfuscated_targets.trajectory_speed);
}

}

WorldPacket PacketSender::BuildAuthSession(std::uint32_t build,
                                           std::string_view account_name,
                                           std::uint32_t client_seed,
                                           const std::uint8_t digest[20],
                                           const std::vector<std::uint8_t> &addon_data) {
  AuthSessionPayload payload{};
  payload.build = build;
  payload.account_name = account_name;
  payload.client_seed = client_seed;
  return BuildAuthSession(payload, digest, addon_data);
}

WorldPacket PacketSender::BuildAuthSession(const AuthSessionPayload &payload,
                                           const std::uint8_t digest[20],
                                           const std::vector<std::uint8_t> &addon_data) {
  WorldPacket pkt(Opcode::CMSG_AUTH_SESSION);
  pkt.AppendU32(payload.build);
  pkt.AppendU32(payload.login_server_id);
  AppendNullString(pkt, payload.account_name);
  pkt.AppendU32(payload.login_server_type);
  pkt.AppendU32(payload.client_seed);
  pkt.AppendU32(payload.region_id);
  pkt.AppendU32(payload.battlegroup_id);
  pkt.AppendU32(payload.realm_id);
  pkt.AppendU64(payload.proof_of_work_nonce);
  pkt.AppendBytes(digest, 20);
  pkt.AppendBytes(addon_data.data(), addon_data.size());
  return pkt;
}

WorldPacket PacketSender::BuildCharEnum() {
  return WorldPacket(Opcode::CMSG_CHAR_ENUM);
}

WorldPacket PacketSender::BuildCharCreate(std::string_view name, std::uint8_t race,
                                          std::uint8_t cls, std::uint8_t gender, std::uint8_t skin,
                                          std::uint8_t face, std::uint8_t hair_style,
                                          std::uint8_t hair_color, std::uint8_t facial_hair,
                                          std::uint8_t outfit_id) {
  WorldPacket pkt(Opcode::CMSG_CHAR_CREATE);
  AppendNullString(pkt, name);
  pkt.AppendU8(race);
  pkt.AppendU8(cls);
  pkt.AppendU8(gender);
  pkt.AppendU8(skin);
  pkt.AppendU8(face);
  pkt.AppendU8(hair_style);
  pkt.AppendU8(hair_color);
  pkt.AppendU8(facial_hair);
  pkt.AppendU8(outfit_id);
  return pkt;
}

WorldPacket PacketSender::BuildCharDelete(std::uint64_t guid) {
  WorldPacket pkt(Opcode::CMSG_CHAR_DELETE);
  pkt.AppendU64(guid);
  return pkt;
}

WorldPacket PacketSender::BuildPlayerLogin(std::uint64_t guid) {
  WorldPacket pkt(Opcode::CMSG_PLAYER_LOGIN);
  pkt.AppendU64(guid);
  return pkt;
}

WorldPacket PacketSender::BuildVoiceChatEnable(const bool voice_enabled,
                                               const bool microphone_enabled) {
  WorldPacket pkt(Opcode::CMSG_VOICE_SESSION_ENABLE);
  pkt.AppendU8(voice_enabled ? 1u : 0u);
  pkt.AppendU8(microphone_enabled ? 1u : 0u);
  return pkt;
}

WorldPacket PacketSender::BuildPing(std::uint32_t ping, std::uint32_t latency) {
  WorldPacket pkt(Opcode::CMSG_PING);
  pkt.AppendU32(ping);
  pkt.AppendU32(latency);
  return pkt;
}

WorldPacket PacketSender::BuildLogoutRequest() {
  return WorldPacket(Opcode::CMSG_LOGOUT_REQUEST);
}

WorldPacket PacketSender::BuildLogoutCancel() {
  return WorldPacket(Opcode::CMSG_LOGOUT_CANCEL);
}

WorldPacket PacketSender::BuildKeepAlive() {
  return WorldPacket(Opcode::CMSG_KEEP_ALIVE);
}

WorldPacket PacketSender::BuildMovement(Opcode opcode, const game::ObjectGuid &mover,
                                        const game::MovementInfo &info) {
  return BuildMovePacket(opcode, mover, info);
}

WorldPacket PacketSender::BuildMoveHeartbeat(const game::ObjectGuid &mover,
                                             const game::MovementInfo &info) {
  return BuildMovePacket(Opcode::MSG_MOVE_HEARTBEAT, mover, info);
}

WorldPacket PacketSender::BuildMoveSetFly(const game::ObjectGuid &mover,
                                          const game::MovementInfo &info) {
  return BuildMovePacket(Opcode::CMSG_MOVE_SET_FLY, mover, info);
}

WorldPacket PacketSender::BuildMoveTimeSkipped(const game::ObjectGuid &mover,
                                               const std::uint32_t skipped_time_ms) {
  WorldPacket pkt(Opcode::CMSG_MOVE_TIME_SKIPPED);
  AppendPackedGuid(pkt, mover);
  pkt.AppendU32(skipped_time_ms);
  return pkt;
}

namespace {

WorldPacket BuildMovementAck(Opcode opcode, const game::ObjectGuid &mover, std::uint32_t counter,
                             const game::MovementInfo &info) {
  WorldPacket pkt(opcode);
  AppendPackedGuid(pkt, mover);
  pkt.AppendU32(counter);
  WriteMovementInfo(pkt, info);
  return pkt;
}

WorldPacket BuildMovementAckWithValue(Opcode opcode, const game::ObjectGuid &mover,
                                      const std::uint32_t counter,
                                      const game::MovementInfo &info, const float value) {
  WorldPacket pkt = BuildMovementAck(opcode, mover, counter, info);
  pkt.AppendFloat(value);
  return pkt;
}

Opcode SpeedTypeToAckOpcode(game::SpeedType type) {
  using game::SpeedType;
  switch (type) {
  case game::kSpeedWalk:
    return Opcode::CMSG_FORCE_WALK_SPEED_CHANGE_ACK;
  case game::kSpeedRun:
    return Opcode::CMSG_FORCE_RUN_SPEED_CHANGE_ACK;
  case game::kSpeedRunBack:
    return Opcode::CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK;
  case game::kSpeedSwim:
    return Opcode::CMSG_FORCE_SWIM_SPEED_CHANGE_ACK;
  case game::kSpeedSwimBack:
    return Opcode::CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK;
  case game::kSpeedFlight:
    return Opcode::CMSG_FORCE_FLIGHT_SPEED_CHANGE_ACK;
  case game::kSpeedFlightBack:
    return Opcode::CMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK;
  case game::kSpeedTurnRate:
    return Opcode::CMSG_FORCE_TURN_RATE_CHANGE_ACK;
  case game::kSpeedPitchRate:
    return Opcode::CMSG_FORCE_PITCH_RATE_CHANGE_ACK;
  default:
    return Opcode::CMSG_FORCE_RUN_SPEED_CHANGE_ACK;
  }
}

}

WorldPacket PacketSender::BuildForceSpeedChangeAck(game::SpeedType type,
                                                   const game::ObjectGuid &mover,
                                                   std::uint32_t counter,
                                                   const game::MovementInfo &info,
                                                   float new_speed) {
  return BuildForceSpeedChangeAck(SpeedTypeToAckOpcode(type), mover, counter, info, new_speed);
}

WorldPacket PacketSender::BuildForceSpeedChangeAck(const Opcode ack_opcode,
                                                   const game::ObjectGuid &mover,
                                                   const std::uint32_t counter,
                                                   const game::MovementInfo &info,
                                                   const float new_speed) {
  return BuildMovementAckWithValue(ack_opcode, mover, counter, info, new_speed);
}

WorldPacket PacketSender::BuildForceMoveRootAck(const game::ObjectGuid &mover,
                                                std::uint32_t counter,
                                                const game::MovementInfo &info) {
  return BuildMovementAck(Opcode::CMSG_FORCE_MOVE_ROOT_ACK, mover, counter, info);
}

WorldPacket PacketSender::BuildForceMoveUnrootAck(const game::ObjectGuid &mover,
                                                  std::uint32_t counter,
                                                  const game::MovementInfo &info) {
  return BuildMovementAck(Opcode::CMSG_FORCE_MOVE_UNROOT_ACK, mover, counter, info);
}

WorldPacket PacketSender::BuildMoveKnockBackAck(const game::ObjectGuid &mover,
                                                std::uint32_t counter,
                                                const game::MovementInfo &info) {
  return BuildMovementAck(Opcode::CMSG_MOVE_KNOCK_BACK_ACK, mover, counter, info);
}

WorldPacket PacketSender::BuildMoveSetCanFlyAck(const game::ObjectGuid &mover,
                                                std::uint32_t counter,
                                                const game::MovementInfo &info,
                                                const bool enabled) {
  return BuildMovementAckWithValue(Opcode::CMSG_MOVE_SET_CAN_FLY_ACK, mover, counter, info,
                                   enabled ? 1.0f : 0.0f);
}

WorldPacket PacketSender::BuildMoveFeatherFallAck(const game::ObjectGuid &mover,
                                                  const std::uint32_t counter,
                                                  const game::MovementInfo &info,
                                                  const bool enabled) {
  return BuildMovementAckWithValue(Opcode::CMSG_MOVE_FEATHER_FALL_ACK, mover, counter, info,
                                   enabled ? 1.0f : 0.0f);
}

WorldPacket PacketSender::BuildMoveWaterWalkAck(const game::ObjectGuid &mover,
                                                const std::uint32_t counter,
                                                const game::MovementInfo &info,
                                                const bool enabled) {
  return BuildMovementAckWithValue(Opcode::CMSG_MOVE_WATER_WALK_ACK, mover, counter, info,
                                   enabled ? 1.0f : 0.0f);
}

WorldPacket PacketSender::BuildMoveHoverAck(const game::ObjectGuid &mover,
                                            const std::uint32_t counter,
                                            const game::MovementInfo &info,
                                            const bool enabled) {
  return BuildMovementAckWithValue(Opcode::CMSG_MOVE_HOVER_ACK, mover, counter, info,
                                   enabled ? 1.0f : 0.0f);
}

WorldPacket PacketSender::BuildMoveSetCanTransitionBetweenSwimAndFlyAck(
    const game::ObjectGuid &mover, const std::uint32_t counter, const game::MovementInfo &info,
    const bool enabled) {
  return BuildMovementAckWithValue(
      Opcode::CMSG_MOVE_SET_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY_ACK, mover, counter, info,
      enabled ? 1.0f : 0.0f);
}

WorldPacket PacketSender::BuildMoveGravityDisableAck(const game::ObjectGuid &mover,
                                                     const std::uint32_t counter,
                                                     const game::MovementInfo &info) {
  return BuildMovementAck(Opcode::CMSG_MOVE_GRAVITY_DISABLE_ACK, mover, counter, info);
}

WorldPacket PacketSender::BuildMoveGravityEnableAck(const game::ObjectGuid &mover,
                                                    const std::uint32_t counter,
                                                    const game::MovementInfo &info) {
  return BuildMovementAck(Opcode::CMSG_MOVE_GRAVITY_ENABLE_ACK, mover, counter, info);
}

WorldPacket PacketSender::BuildMoveSetCollisionHeightAck(const game::ObjectGuid &mover,
                                                         const std::uint32_t counter,
                                                         const game::MovementInfo &info,
                                                         const float collision_height) {
  return BuildMovementAckWithValue(Opcode::CMSG_MOVE_SET_COLLISION_HGT_ACK, mover, counter, info,
                                   collision_height);
}

WorldPacket PacketSender::BuildTimeSyncResponse(const std::uint32_t counter,
                                                const std::uint32_t client_time_ms) {
  WorldPacket pkt(Opcode::CMSG_TIME_SYNC_RESP);
  pkt.AppendU32(counter);
  pkt.AppendU32(client_time_ms);
  return pkt;
}

WorldPacket PacketSender::BuildMoveSplineDone(const game::ObjectGuid &mover,
                                              const game::MovementInfo &info,
                                              const std::uint32_t spline_id) {
  WorldPacket pkt = BuildMovePacket(Opcode::CMSG_MOVE_SPLINE_DONE, mover, info);
  pkt.AppendU32(spline_id);
  return pkt;
}

WorldPacket PacketSender::BuildSetSelection(std::uint64_t target_guid) {
  WorldPacket pkt(Opcode::CMSG_SET_SELECTION);
  pkt.AppendU64(target_guid);
  return pkt;
}

WorldPacket PacketSender::BuildAttackSwing(std::uint64_t target_guid) {
  WorldPacket pkt(Opcode::CMSG_ATTACKSWING);
  pkt.AppendU64(target_guid);
  return pkt;
}

WorldPacket PacketSender::BuildAttackStop() {
  return WorldPacket(Opcode::CMSG_ATTACKSTOP);
}

WorldPacket PacketSender::BuildCastSpell(std::uint8_t cast_count, std::uint32_t spell_id,
                                         std::uint8_t cast_flags,
                                         const SpellTargets &targets) {
  WorldPacket pkt(Opcode::CMSG_CAST_SPELL);
  pkt.AppendU8(cast_count);
  pkt.AppendU32(spell_id);
  pkt.AppendU8(cast_flags);
  AppendSpellTrajectoryPayload(pkt, cast_flags, targets);
  return pkt;
}

WorldPacket PacketSender::BuildPetCastSpell(
    const std::uint64_t pet_guid, const std::uint8_t cast_count,
    const std::uint32_t spell_id, const std::uint8_t cast_flags,
    const SpellTargets& targets) {
  WorldPacket pkt(Opcode::CMSG_PET_CAST_SPELL);
  pkt.AppendU64(pet_guid);
  pkt.AppendU8(cast_count);
  pkt.AppendU32(spell_id);
  pkt.AppendU8(cast_flags);
  AppendSpellTrajectoryPayload(pkt, cast_flags, targets);
  return pkt;
}

WorldPacket PacketSender::BuildCancelCast(const std::uint8_t cast_count,
                                           const std::uint32_t spell_id) {
  WorldPacket pkt(Opcode::CMSG_CANCEL_CAST);
  pkt.AppendU8(cast_count);
  pkt.AppendU32(spell_id);
  return pkt;
}

WorldPacket PacketSender::BuildCancelAura(std::uint32_t spell_id) {
  WorldPacket pkt(Opcode::CMSG_CANCEL_AURA);
  pkt.AppendU32(spell_id);
  return pkt;
}

WorldPacket PacketSender::BuildCancelChannelling(std::uint32_t spell_id) {
  WorldPacket pkt(Opcode::CMSG_CANCEL_CHANNELLING);
  pkt.AppendU32(spell_id);
  return pkt;
}

WorldPacket PacketSender::BuildCancelAutoRepeat() {
  return WorldPacket(Opcode::CMSG_CANCEL_AUTO_REPEAT_SPELL);
}

WorldPacket PacketSender::BuildUpdateProjectilePosition(
    std::uint64_t caster_guid, std::uint32_t spell_id,
    std::uint8_t cast_count, float x, float y, float z) {
  WorldPacket pkt(Opcode::CMSG_UPDATE_PROJECTILE_POSITION);
  pkt.AppendU64(caster_guid);
  pkt.AppendU32(spell_id);
  pkt.AppendU8(cast_count);
  pkt.AppendFloat(x);
  pkt.AppendFloat(y);
  pkt.AppendFloat(z);
  return pkt;
}

WorldPacket PacketSender::BuildChatMessage(game::ChatMsg type, game::Language language,
                                           std::string_view message,
                                           std::string_view target_or_channel) {
  WorldPacket pkt(Opcode::CMSG_MESSAGECHAT);
  pkt.AppendU32(static_cast<std::uint32_t>(type));
  pkt.AppendU32(static_cast<std::uint32_t>(language));

  if (game::ChatTypeNeedsChannel(type)) {
    AppendNullString(pkt, target_or_channel);
  }

  if (game::ChatTypeNeedsTarget(type)) {
    AppendNullString(pkt, target_or_channel);
  }

  AppendNullString(pkt, message);
  return pkt;
}

WorldPacket PacketSender::BuildJoinChannel(std::uint32_t channel_id, std::string_view channel_name,
                                           std::string_view password, const bool has_voice,
                                           const std::uint8_t join_flag) {
  WorldPacket pkt(Opcode::CMSG_JOIN_CHANNEL);
  pkt.AppendU32(channel_id);
  pkt.AppendU8(join_flag);
  pkt.AppendU8(has_voice ? 1 : 0);
  AppendNullString(pkt, channel_name);
  AppendNullString(pkt, password);
  return pkt;
}

WorldPacket PacketSender::BuildLeaveChannel(std::uint32_t channel_id,
                                            std::string_view channel_name) {
  WorldPacket pkt(Opcode::CMSG_LEAVE_CHANNEL);
  pkt.AppendU32(channel_id);
  AppendNullString(pkt, channel_name);
  return pkt;
}

WorldPacket PacketSender::BuildGossipHello(std::uint64_t npc_guid) {
  WorldPacket pkt(Opcode::CMSG_GOSSIP_HELLO);
  pkt.AppendU64(npc_guid);
  return pkt;
}

WorldPacket PacketSender::BuildGossipSelectOption(std::uint64_t npc_guid,
                                                  std::uint32_t menu_id,
                                                  std::uint32_t gossip_list_id,
                                                  std::string_view code_text) {
  WorldPacket pkt(Opcode::CMSG_GOSSIP_SELECT_OPTION);
  pkt.AppendU64(npc_guid);
  pkt.AppendU32(menu_id);
  pkt.AppendU32(gossip_list_id);
  if (!code_text.empty()) {
    AppendNullString(pkt, code_text);
  }
  return pkt;
}

WorldPacket PacketSender::BuildQuestgiverHello(std::uint64_t npc_guid) {
  WorldPacket pkt(Opcode::CMSG_QUESTGIVER_HELLO);
  pkt.AppendU64(npc_guid);
  return pkt;
}

WorldPacket PacketSender::BuildQuestgiverAcceptQuest(std::uint64_t npc_guid, std::uint32_t quest_id,
                                                     std::uint32_t accept_packet_value) {
  WorldPacket pkt(Opcode::CMSG_QUESTGIVER_ACCEPT_QUEST);
  pkt.AppendU64(npc_guid);
  pkt.AppendU32(quest_id);
  pkt.AppendU32(accept_packet_value);
  return pkt;
}

WorldPacket PacketSender::BuildQuestgiverCompleteQuest(std::uint64_t npc_guid,
                                                       std::uint32_t quest_id) {
  WorldPacket pkt(Opcode::CMSG_QUESTGIVER_COMPLETE_QUEST);
  pkt.AppendU64(npc_guid);
  pkt.AppendU32(quest_id);
  return pkt;
}

WorldPacket PacketSender::BuildQuestgiverChooseReward(std::uint64_t npc_guid,
                                                      std::uint32_t quest_id,
                                                      std::uint32_t reward_index) {
  WorldPacket pkt(Opcode::CMSG_QUESTGIVER_CHOOSE_REWARD);
  pkt.AppendU64(npc_guid);
  pkt.AppendU32(quest_id);
  pkt.AppendU32(reward_index);
  return pkt;
}

WorldPacket PacketSender::BuildQuestgiverRequestReward(std::uint64_t npc_guid,
                                                       std::uint32_t quest_id) {
  WorldPacket pkt(Opcode::CMSG_QUESTGIVER_REQUEST_REWARD);
  pkt.AppendU64(npc_guid);
  pkt.AppendU32(quest_id);
  return pkt;
}

WorldPacket PacketSender::BuildQuestPushResult(std::uint64_t receiver_guid, std::uint32_t quest_id,
                                               std::uint8_t result) {
  WorldPacket pkt(Opcode::MSG_QUEST_PUSH_RESULT);
  pkt.AppendU64(receiver_guid);
  pkt.AppendU32(quest_id);
  pkt.AppendU8(result);
  return pkt;
}

WorldPacket PacketSender::BuildPushQuestToParty(std::uint32_t quest_id) {
  WorldPacket pkt(Opcode::CMSG_PUSHQUESTTOPARTY);
  pkt.AppendU32(quest_id);
  return pkt;
}

WorldPacket PacketSender::BuildQuestlogRemoveQuest(std::uint8_t slot) {
  WorldPacket pkt(Opcode::CMSG_QUESTLOG_REMOVE_QUEST);
  pkt.AppendU8(slot);
  return pkt;
}

WorldPacket PacketSender::BuildQuestQuery(std::uint32_t quest_id) {
  WorldPacket pkt(Opcode::CMSG_QUEST_QUERY);
  pkt.AppendU32(quest_id);
  return pkt;
}

WorldPacket PacketSender::BuildQuestgiverStatusQuery(std::uint64_t guid) {
  WorldPacket pkt(Opcode::CMSG_QUESTGIVER_STATUS_QUERY);
  pkt.AppendU64(guid);
  return pkt;
}

WorldPacket PacketSender::BuildQuestgiverStatusMultipleQuery() {
  return WorldPacket(Opcode::CMSG_QUESTGIVER_STATUS_MULTIPLE_QUERY);
}

WorldPacket PacketSender::BuildQuestPoiQuery(const std::vector<std::uint32_t> &quest_ids) {
  WorldPacket pkt(Opcode::CMSG_QUEST_POI_QUERY);
  pkt.AppendU32(static_cast<std::uint32_t>(quest_ids.size()));
  for (const auto quest_id : quest_ids) {
    pkt.AppendU32(quest_id);
  }
  return pkt;
}

WorldPacket PacketSender::BuildSwapInvItem(std::uint8_t dst_slot, std::uint8_t src_slot) {
  WorldPacket pkt(Opcode::CMSG_SWAP_INV_ITEM);
  pkt.AppendU8(dst_slot);
  pkt.AppendU8(src_slot);
  return pkt;
}

WorldPacket PacketSender::BuildSwapItem(std::uint8_t dst_bag, std::uint8_t dst_slot,
                                        std::uint8_t src_bag, std::uint8_t src_slot) {
  WorldPacket pkt(Opcode::CMSG_SWAP_ITEM);
  pkt.AppendU8(dst_bag);
  pkt.AppendU8(dst_slot);
  pkt.AppendU8(src_bag);
  pkt.AppendU8(src_slot);
  return pkt;
}

WorldPacket PacketSender::BuildAutoEquipItem(std::uint8_t src_bag, std::uint8_t src_slot) {
  WorldPacket pkt(Opcode::CMSG_AUTOEQUIP_ITEM);
  pkt.AppendU8(src_bag);
  pkt.AppendU8(src_slot);
  return pkt;
}

WorldPacket PacketSender::BuildDestroyItem(std::uint8_t bag, std::uint8_t slot,
                                           std::uint32_t count) {
  WorldPacket pkt(Opcode::CMSG_DESTROYITEM);
  pkt.AppendU8(bag);
  pkt.AppendU8(slot);
  pkt.AppendU32(count);
  return pkt;
}

WorldPacket PacketSender::BuildOpenItem(std::uint8_t bag, std::uint8_t slot) {
  WorldPacket pkt(Opcode::CMSG_OPEN_ITEM);
  pkt.AppendU8(bag);
  pkt.AppendU8(slot);
  return pkt;
}

WorldPacket PacketSender::BuildWrapItem(std::uint8_t source_bag, std::uint8_t source_slot,
                                        std::uint8_t target_bag, std::uint8_t target_slot) {
  WorldPacket pkt(Opcode::CMSG_WRAP_ITEM);
  pkt.AppendU8(source_bag);
  pkt.AppendU8(source_slot);
  pkt.AppendU8(target_bag);
  pkt.AppendU8(target_slot);
  return pkt;
}

WorldPacket PacketSender::BuildReadItem(std::uint8_t bag, std::uint8_t slot) {
  WorldPacket pkt(Opcode::CMSG_READ_ITEM);
  pkt.AppendU8(bag);
  pkt.AppendU8(slot);
  return pkt;
}

WorldPacket PacketSender::BuildItemTextQuery(const std::uint64_t item_guid) {
  WorldPacket pkt(Opcode::CMSG_ITEM_TEXT_QUERY);
  pkt.AppendU64(item_guid);
  return pkt;
}

WorldPacket PacketSender::BuildUseItem(std::uint8_t bag_index, std::uint8_t slot,
                                       std::uint8_t cast_count, std::uint32_t spell_id,
                                       std::uint64_t item_guid, std::uint32_t glyph_index,
                                       std::uint8_t cast_flags,
                                       const SpellTargets &targets) {
  WorldPacket pkt(Opcode::CMSG_USE_ITEM);
  pkt.AppendU8(bag_index);
  pkt.AppendU8(slot);
  pkt.AppendU8(cast_count);
  pkt.AppendU32(spell_id);
  pkt.AppendU64(item_guid);
  pkt.AppendU32(glyph_index);
  pkt.AppendU8(cast_flags);
  AppendSpellTrajectoryPayload(pkt, cast_flags, targets);
  return pkt;
}

WorldPacket PacketSender::BuildSplitItem(std::uint8_t src_bag, std::uint8_t src_slot,
                                         std::uint8_t dst_bag, std::uint8_t dst_slot,
                                         std::uint32_t count) {
  WorldPacket pkt(Opcode::CMSG_SPLIT_ITEM);
  pkt.AppendU8(src_bag);
  pkt.AppendU8(src_slot);
  pkt.AppendU8(dst_bag);
  pkt.AppendU8(dst_slot);
  pkt.AppendU32(count);
  return pkt;
}

WorldPacket PacketSender::BuildSellItem(std::uint64_t vendor_guid, std::uint64_t item_guid,
                                        std::uint32_t count) {
  WorldPacket pkt(Opcode::CMSG_SELL_ITEM);
  pkt.AppendU64(vendor_guid);
  pkt.AppendU64(item_guid);
  pkt.AppendU32(count);
  return pkt;
}

WorldPacket PacketSender::BuildItemRefundInfo(std::uint64_t item_guid) {
  WorldPacket packet(Opcode::CMSG_ITEM_REFUND_INFO);
  packet.AppendU64(item_guid);
  return packet;
}

WorldPacket PacketSender::BuildItemRefund(std::uint64_t item_guid) {
  WorldPacket packet(Opcode::CMSG_ITEM_REFUND);
  packet.AppendU64(item_guid);
  return packet;
}

WorldPacket PacketSender::BuildSelfResurrect() {
  return WorldPacket(Opcode::CMSG_SELF_RES);
}

WorldPacket PacketSender::BuildBuyItem(std::uint64_t vendor_guid, std::uint32_t item_entry,
                                       std::uint32_t slot, std::uint32_t count,
                                       std::uint8_t bag) {
  WorldPacket pkt(Opcode::CMSG_BUY_ITEM);
  pkt.AppendU64(vendor_guid);
  pkt.AppendU32(item_entry);
  pkt.AppendU32(slot);
  pkt.AppendU32(count);
  pkt.AppendU8(bag);
  return pkt;
}

WorldPacket PacketSender::BuildBuyItemInSlot(std::uint64_t vendor_guid, std::uint32_t item_entry,
                                             std::uint32_t vendor_slot, std::uint64_t target_guid,
                                             std::uint8_t target_slot, std::uint32_t count) {
  WorldPacket pkt(Opcode::CMSG_BUY_ITEM_IN_SLOT);
  pkt.AppendU64(vendor_guid);
  pkt.AppendU32(item_entry);
  pkt.AppendU32(vendor_slot);
  pkt.AppendU64(target_guid);
  pkt.AppendU8(target_slot);
  pkt.AppendU32(count);
  return pkt;
}

WorldPacket PacketSender::BuildLoot(std::uint64_t target_guid) {
  WorldPacket pkt(Opcode::CMSG_LOOT);
  pkt.AppendU64(target_guid);
  return pkt;
}

WorldPacket PacketSender::BuildAutoStoreLootItem(std::uint8_t loot_slot) {
  WorldPacket pkt(Opcode::CMSG_AUTOSTORE_LOOT_ITEM);
  pkt.AppendU8(loot_slot);
  return pkt;
}

WorldPacket PacketSender::BuildGroupInvite(std::string_view player_name, std::uint32_t role_flags) {
  WorldPacket pkt(Opcode::CMSG_GROUP_INVITE);
  AppendNullString(pkt, player_name);
  pkt.AppendU32(role_flags);
  return pkt;
}

WorldPacket PacketSender::BuildGroupAccept(std::uint32_t role_flags) {
  WorldPacket pkt(Opcode::CMSG_GROUP_ACCEPT);
  pkt.AppendU32(role_flags);
  return pkt;
}

WorldPacket PacketSender::BuildGroupDecline() {
  return WorldPacket(Opcode::CMSG_GROUP_DECLINE);
}

WorldPacket PacketSender::BuildGroupDisband() {
  return WorldPacket(Opcode::CMSG_GROUP_DISBAND);
}

WorldPacket PacketSender::BuildGroupSetLeader(std::uint64_t target_guid) {
  WorldPacket pkt(Opcode::CMSG_GROUP_SET_LEADER);
  pkt.AppendU64(target_guid);
  return pkt;
}

WorldPacket PacketSender::BuildGroupUninviteByGuid(std::uint64_t target_guid,
                                                   std::string_view reason) {
  WorldPacket pkt(Opcode::CMSG_GROUP_UNINVITE_GUID);
  pkt.AppendU64(target_guid);
  AppendNullString(pkt, reason);
  return pkt;
}

WorldPacket PacketSender::BuildGroupAssistantLeader(std::uint64_t target_guid, bool set) {
  WorldPacket pkt(Opcode::CMSG_GROUP_ASSISTANT_LEADER);
  pkt.AppendU64(target_guid);
  pkt.AppendU8(set ? 1u : 0u);
  return pkt;
}

WorldPacket PacketSender::BuildLootMethod(std::uint32_t method, std::uint64_t master_guid,
                                          std::uint32_t threshold) {
  WorldPacket pkt(Opcode::CMSG_LOOT_METHOD);
  pkt.AppendU32(method);
  pkt.AppendU64(master_guid);
  pkt.AppendU32(threshold);
  return pkt;
}

WorldPacket PacketSender::BuildReadyCheck() {
  return WorldPacket(Opcode::MSG_RAID_READY_CHECK);
}

WorldPacket PacketSender::BuildReadyCheckFinished() {
  return WorldPacket(Opcode::MSG_RAID_READY_CHECK_FINISHED);
}

WorldPacket PacketSender::BuildRequestPartyMemberStats(const std::uint64_t target_guid) {
  WorldPacket pkt(Opcode::CMSG_REQUEST_PARTY_MEMBER_STATS);
  AppendPackedGuid(pkt, game::ObjectGuid(target_guid));
  return pkt;
}

WorldPacket PacketSender::BuildAddFriend(std::string_view name, std::string_view note) {
  WorldPacket pkt(Opcode::CMSG_ADD_FRIEND);
  AppendNullString(pkt, name);
  AppendNullString(pkt, note);
  return pkt;
}

WorldPacket PacketSender::BuildDelFriend(std::uint64_t guid) {
  WorldPacket pkt(Opcode::CMSG_DEL_FRIEND);
  pkt.AppendU64(guid);
  return pkt;
}

WorldPacket PacketSender::BuildAddIgnore(std::string_view name) {
  WorldPacket pkt(Opcode::CMSG_ADD_IGNORE);
  AppendNullString(pkt, name);
  return pkt;
}

WorldPacket PacketSender::BuildDelIgnore(std::uint64_t guid) {
  WorldPacket pkt(Opcode::CMSG_DEL_IGNORE);
  pkt.AppendU64(guid);
  return pkt;
}

WorldPacket PacketSender::BuildWho(const WhoQuery &query) {
  WorldPacket pkt(Opcode::CMSG_WHO);
  pkt.AppendU32(query.min_level);
  pkt.AppendU32(query.max_level);
  AppendNullString(pkt, query.player_name);
  AppendNullString(pkt, query.guild_name);
  pkt.AppendU32(query.race_mask);
  pkt.AppendU32(query.class_mask);

  auto zone_count = static_cast<std::uint32_t>(query.zones.size());
  if (zone_count > 10)
    zone_count = 10;
  pkt.AppendU32(zone_count);
  for (std::uint32_t i = 0; i < zone_count; ++i) {
    pkt.AppendU32(query.zones[i]);
  }

  auto str_count = static_cast<std::uint32_t>(query.strings.size());
  if (str_count > 4)
    str_count = 4;
  pkt.AppendU32(str_count);
  for (std::uint32_t i = 0; i < str_count; ++i) {
    AppendNullString(pkt, query.strings[i]);
  }
  return pkt;
}

WorldPacket PacketSender::BuildLearnTalent(std::uint32_t talent_id, std::uint32_t talent_rank) {
  WorldPacket pkt(Opcode::CMSG_LEARN_TALENT);
  pkt.AppendU32(talent_id);
  pkt.AppendU32(talent_rank);
  return pkt;
}

WorldPacket PacketSender::BuildLearnPetTalent(std::uint64_t pet_guid,
                                               std::uint32_t talent_id,
                                               std::uint32_t talent_rank) {
  WorldPacket pkt(Opcode::CMSG_PET_LEARN_TALENT);
  pkt.AppendU64(pet_guid);
  pkt.AppendU32(talent_id);
  pkt.AppendU32(talent_rank);
  return pkt;
}

WorldPacket PacketSender::BuildBuySkillStep(std::uint32_t skill_id) {
  WorldPacket pkt(Opcode::CMSG_SKILL_BUY_STEP);
  pkt.AppendU32(skill_id);
  return pkt;
}

WorldPacket PacketSender::BuildBuySkillRanks(
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> &queued_ranks) {
  WorldPacket pkt(Opcode::CMSG_SKILL_BUY_RANK);
  pkt.AppendU32(static_cast<std::uint32_t>(queued_ranks.size()));
  for (const auto &[skill_id, queued_points] : queued_ranks) {
    pkt.AppendU32(skill_id);
    pkt.AppendU32(queued_points);
  }
  return pkt;
}

WorldPacket PacketSender::BuildSetActiveMover(std::uint64_t mover_guid) {
  WorldPacket pkt(Opcode::CMSG_SET_ACTIVE_MOVER);
  pkt.AppendU64(mover_guid);
  return pkt;
}

WorldPacket PacketSender::BuildFarSight(const bool enable) {
  WorldPacket pkt(Opcode::CMSG_FAR_SIGHT);
  pkt.AppendU8(enable ? 1u : 0u);
  return pkt;
}

WorldPacket PacketSender::BuildSetActionBarToggles(std::uint8_t toggles) {
  WorldPacket pkt(Opcode::CMSG_SET_ACTIONBAR_TOGGLES);
  pkt.AppendU8(toggles);
  return pkt;
}

WorldPacket PacketSender::BuildBattlefieldPort(const std::uint64_t battlefield_instance_guid,
                                               const bool accepted) {

  WorldPacket pkt(Opcode::CMSG_BATTLEFIELD_PORT);
  pkt.AppendU64(battlefield_instance_guid);
  pkt.AppendU8(accepted ? 1 : 0);
  return pkt;
}

WorldPacket PacketSender::BuildInstanceLockResponse(const bool accept) {
  WorldPacket pkt(Opcode::CMSG_INSTANCE_LOCK_RESPONSE);

  pkt.AppendU8(static_cast<std::uint8_t>(accept));
  return pkt;
}

WorldPacket PacketSender::BuildBattlemasterJoin(std::uint64_t battlemaster_guid,
                                                std::uint32_t bg_type_id, std::uint32_t instance_id,
                                                bool join_as_group) {
  WorldPacket pkt(Opcode::CMSG_BATTLEMASTER_JOIN);
  pkt.AppendU64(battlemaster_guid);
  pkt.AppendU32(bg_type_id);
  pkt.AppendU32(instance_id);
  pkt.AppendU8(join_as_group ? 1 : 0);
  return pkt;
}

WorldPacket PacketSender::BuildBattlemasterJoinArena(std::uint64_t battlemaster_guid,
                                                     std::uint8_t slot, bool as_group,
                                                     bool is_rated) {
  WorldPacket pkt(Opcode::CMSG_BATTLEMASTER_JOIN_ARENA);
  pkt.AppendU64(battlemaster_guid);
  pkt.AppendU8(slot);
  pkt.AppendU8(as_group ? 1 : 0);
  pkt.AppendU8(is_rated ? 1 : 0);
  return pkt;
}

WorldPacket PacketSender::BuildCommentatorEnable(const std::uint32_t mode) {
  WorldPacket pkt(Opcode::CMSG_COMMENTATOR_ENABLE);
  pkt.AppendU32(mode);
  return pkt;
}

WorldPacket PacketSender::BuildCommentatorGetMapInfo(std::string_view zone) {
  WorldPacket pkt(Opcode::CMSG_COMMENTATOR_GET_MAP_INFO);
  AppendNullString(pkt, zone);
  return pkt;
}

WorldPacket
PacketSender::BuildCommentatorGetPlayerInfo(const game::CommentatorInstanceKey &instance_key) {
  WorldPacket pkt(Opcode::CMSG_COMMENTATOR_GET_PLAYER_INFO);
  pkt.AppendU32(instance_key.map_id);
  AppendCommentatorInstanceKeyTail(pkt, instance_key);
  return pkt;
}

WorldPacket
PacketSender::BuildCommentatorEnterInstance(const game::CommentatorInstanceKey &instance_key,
                                            const std::uint64_t instance_guid) {
  WorldPacket pkt(Opcode::CMSG_COMMENTATOR_ENTER_INSTANCE);
  pkt.AppendU32(instance_key.map_id);
  AppendCommentatorInstanceKeyTail(pkt, instance_key);
  pkt.AppendU64(instance_guid);
  return pkt;
}

WorldPacket PacketSender::BuildCommentatorStartInstance(
    const std::uint64_t battlemaster_guid,
    const std::uint32_t map_id,
    const std::uint32_t team_size,
    const std::uint32_t min_level,
    const std::uint32_t max_level) {
  WorldPacket pkt(Opcode::CMSG_COMMENTATOR_INSTANCE_COMMAND);
  pkt.AppendU64(0);
  pkt.AppendU64(0);
  pkt.AppendU64(battlemaster_guid);
  pkt.AppendU32(0);
  pkt.AppendU32(map_id);
  pkt.AppendU32(team_size);
  pkt.AppendU32(min_level);
  pkt.AppendU32(max_level);
  pkt.AppendU32(0);
  return pkt;
}

WorldPacket PacketSender::BuildCommentatorAddPlayer(
    const game::CommentatorMapInfo& map,
    const game::CommentatorInstanceInfo& instance,
    const std::uint64_t player_guid,
    const std::uint64_t battlemaster_guid,
    const std::uint32_t team_index) {
  WorldPacket pkt(Opcode::CMSG_COMMENTATOR_INSTANCE_COMMAND);
  pkt.AppendU64(instance.guid.GetRawValue());
  pkt.AppendU64(player_guid);
  pkt.AppendU64(battlemaster_guid);
  pkt.AppendU32(1);
  pkt.AppendU32(instance.extra_u32);
  pkt.AppendU32(map.field0);
  pkt.AppendU32(map.field1);
  pkt.AppendU32(map.field2);
  pkt.AppendU32(team_index);
  return pkt;
}

WorldPacket PacketSender::BuildCommentatorRemovePlayer(
    const game::CommentatorMapInfo& map,
    const game::CommentatorInstanceInfo& instance,
    const std::uint64_t player_guid,
    const std::uint64_t battlemaster_guid,
    const std::uint32_t team_index) {
  WorldPacket pkt(Opcode::CMSG_COMMENTATOR_INSTANCE_COMMAND);
  pkt.AppendU64(instance.guid.GetRawValue());
  pkt.AppendU64(player_guid);
  pkt.AppendU64(battlemaster_guid);
  pkt.AppendU32(2);
  pkt.AppendU32(instance.extra_u32);
  pkt.AppendU32(map.field0);
  pkt.AppendU32(map.field1);
  pkt.AppendU32(map.field2);
  pkt.AppendU32(team_index);
  return pkt;
}

WorldPacket PacketSender::BuildCommentatorExitInstance() {
  return WorldPacket(Opcode::CMSG_COMMENTATOR_EXIT_INSTANCE);
}

WorldPacket PacketSender::BuildCommentatorSetSkirmishMatchmakingMode(const std::uint8_t mode) {
  return BuildCommentatorSkirmishQueueCommand(0, mode, 0, 0, 0);
}

WorldPacket PacketSender::BuildCommentatorRequestSkirmishQueueData() {
  return BuildCommentatorSkirmishQueueCommand(1, 1, 0, 0, 0);
}

WorldPacket PacketSender::BuildCommentatorStartSkirmishMatch(const std::uint64_t first_guid,
                                                             const std::uint64_t second_guid,
                                                             const std::int32_t match_size) {
  return BuildCommentatorSkirmishQueueCommand(2, 1, first_guid, second_guid,
                                              static_cast<std::uint32_t>(match_size));
}

WorldPacket PacketSender::BuildCommentatorRequestSkirmishMode() {
  return BuildCommentatorSkirmishQueueCommand(3, 1, 0, 0, 0);
}

WorldPacket PacketSender::BuildLeaveBattlefield(const game::ObjectGuid battlefield_guid) {
  WorldPacket pkt(Opcode::CMSG_LEAVE_BATTLEFIELD);
  pkt.AppendU64(battlefield_guid.GetRawValue());
  return pkt;
}

WorldPacket PacketSender::BuildArenaTeamAccept() {
  return WorldPacket(Opcode::CMSG_ARENA_TEAM_ACCEPT);
}

WorldPacket PacketSender::BuildArenaTeamDecline() {
  return WorldPacket(Opcode::CMSG_ARENA_TEAM_DECLINE);
}

WorldPacket PacketSender::BuildRequestVehicleExit() {
  return WorldPacket(Opcode::CMSG_REQUEST_VEHICLE_EXIT);
}

WorldPacket PacketSender::BuildRequestVehicleSwitchSeat(std::uint64_t vehicle_guid,
                                                        std::uint8_t seat_id) {
  WorldPacket pkt(Opcode::CMSG_REQUEST_VEHICLE_SWITCH_SEAT);
  AppendPackedGuid(pkt, game::ObjectGuid(vehicle_guid));
  pkt.AppendU8(seat_id);
  return pkt;
}

WorldPacket PacketSender::BuildRequestVehicleNextSeat() {
  return WorldPacket(Opcode::CMSG_REQUEST_VEHICLE_NEXT_SEAT);
}

WorldPacket PacketSender::BuildRequestVehiclePrevSeat() {
  return WorldPacket(Opcode::CMSG_REQUEST_VEHICLE_PREV_SEAT);
}

WorldPacket PacketSender::BuildSpellClick(std::uint64_t unit_guid) {
  WorldPacket pkt(Opcode::CMSG_SPELLCLICK);
  pkt.AppendU64(unit_guid);
  return pkt;
}

WorldPacket PacketSender::BuildPlayerVehicleEnter(std::uint64_t unit_guid) {
  WorldPacket pkt(Opcode::CMSG_PLAYER_VEHICLE_ENTER);
  pkt.AppendU64(unit_guid);
  return pkt;
}

WorldPacket PacketSender::BuildControllerEjectPassenger(std::uint64_t passenger_guid) {
  WorldPacket pkt(Opcode::CMSG_CONTROLLER_EJECT_PASSENGER);
  pkt.AppendU64(passenger_guid);
  return pkt;
}

WorldPacket PacketSender::BuildGuildCreate(std::string_view guild_name) {
  WorldPacket pkt(Opcode::CMSG_GUILD_CREATE);
  AppendNullString(pkt, guild_name);
  return pkt;
}

WorldPacket PacketSender::BuildGuildInvite(std::string_view player_name) {
  WorldPacket pkt(Opcode::CMSG_GUILD_INVITE);
  AppendNullString(pkt, player_name);
  return pkt;
}

WorldPacket PacketSender::BuildGuildAccept() {
  return WorldPacket(Opcode::CMSG_GUILD_ACCEPT);
}

WorldPacket PacketSender::BuildGuildDecline() {
  return WorldPacket(Opcode::CMSG_GUILD_DECLINE);
}

WorldPacket PacketSender::BuildGuildLeave() {
  return WorldPacket(Opcode::CMSG_GUILD_LEAVE);
}

WorldPacket PacketSender::BuildGuildDisband() {
  return WorldPacket(Opcode::CMSG_GUILD_DISBAND);
}

WorldPacket PacketSender::BuildGuildMotd(std::string_view motd) {
  WorldPacket pkt(Opcode::CMSG_GUILD_MOTD);
  const auto sanitized_motd = SanitizeGuildMotdText(motd);
  AppendNullString(pkt, sanitized_motd);
  return pkt;
}

WorldPacket PacketSender::BuildGuildInfoText(std::string_view info_text) {
  WorldPacket pkt(Opcode::CMSG_GUILD_INFO_TEXT);
  const auto sanitized_text = SanitizeGuildInfoText(info_text);
  AppendNullString(pkt, sanitized_text);
  return pkt;
}

WorldPacket PacketSender::BuildGuildRoster() {
  return WorldPacket(Opcode::CMSG_GUILD_ROSTER);
}

WorldPacket PacketSender::BuildGuildRank(std::uint32_t rank_id, std::uint32_t rights,
                                         std::string_view rank_name, std::uint32_t money_per_day,
                                         const std::array<std::uint32_t, 6> &bank_tab_flags,
                                         const std::array<std::uint32_t, 6> &bank_tab_withdraw) {
  WorldPacket pkt(Opcode::CMSG_GUILD_RANK);
  pkt.AppendU32(rank_id);
  pkt.AppendU32(rights);
  AppendNullString(pkt, rank_name);
  pkt.AppendU32(money_per_day);
  for (int i = 0; i < 6; ++i) {
    pkt.AppendU32(bank_tab_flags[static_cast<std::size_t>(i)]);
    pkt.AppendU32(bank_tab_withdraw[static_cast<std::size_t>(i)]);
  }
  return pkt;
}

WorldPacket PacketSender::BuildGuildPromote(std::string_view player_name) {
  WorldPacket pkt(Opcode::CMSG_GUILD_PROMOTE);
  AppendNullString(pkt, player_name);
  return pkt;
}

WorldPacket PacketSender::BuildGuildDemote(std::string_view player_name) {
  WorldPacket pkt(Opcode::CMSG_GUILD_DEMOTE);
  AppendNullString(pkt, player_name);
  return pkt;
}

WorldPacket PacketSender::BuildGuildRemove(std::string_view player_name) {
  WorldPacket pkt(Opcode::CMSG_GUILD_REMOVE);
  AppendNullString(pkt, player_name);
  return pkt;
}

WorldPacket PacketSender::BuildBattlemasterHello(std::uint64_t guid) {
  WorldPacket pkt(Opcode::CMSG_BATTLEMASTER_HELLO);
  pkt.AppendU64(guid);
  return pkt;
}

WorldPacket PacketSender::BuildBankerActivate(std::uint64_t banker_guid) {
  WorldPacket pkt(Opcode::CMSG_BANKER_ACTIVATE);
  pkt.AppendU64(banker_guid);
  return pkt;
}

WorldPacket PacketSender::BuildBuyBankSlot(std::uint64_t banker_guid) {
  WorldPacket pkt(Opcode::CMSG_BUY_BANK_SLOT);
  pkt.AppendU64(banker_guid);
  return pkt;
}

WorldPacket PacketSender::BuildAutoBankItem(std::uint8_t bag, std::uint8_t slot) {
  WorldPacket pkt(Opcode::CMSG_AUTOBANK_ITEM);
  pkt.AppendU8(bag);
  pkt.AppendU8(slot);
  return pkt;
}

WorldPacket PacketSender::BuildAutoStoreBankItem(std::uint8_t bag, std::uint8_t slot) {
  WorldPacket pkt(Opcode::CMSG_AUTOSTORE_BANK_ITEM);
  pkt.AppendU8(bag);
  pkt.AppendU8(slot);
  return pkt;
}

WorldPacket PacketSender::BuildGuildBankQueryTab(std::uint64_t banker_guid, std::uint8_t tab_id,
                                                 bool full_update) {
  WorldPacket pkt(Opcode::CMSG_GUILD_BANK_QUERY_TAB);
  pkt.AppendU64(banker_guid);
  pkt.AppendU8(tab_id);
  pkt.AppendU8(full_update ? 1 : 0);
  return pkt;
}

WorldPacket PacketSender::BuildGuildBankDepositMoney(std::uint64_t banker_guid,
                                                     std::uint32_t money) {
  WorldPacket pkt(Opcode::CMSG_GUILD_BANK_DEPOSIT_MONEY);
  pkt.AppendU64(banker_guid);
  pkt.AppendU32(money);
  return pkt;
}

WorldPacket PacketSender::BuildGuildBankWithdrawMoney(std::uint64_t banker_guid,
                                                      std::uint32_t money) {
  WorldPacket pkt(Opcode::CMSG_GUILD_BANK_WITHDRAW_MONEY);
  pkt.AppendU64(banker_guid);
  pkt.AppendU32(money);
  return pkt;
}

WorldPacket PacketSender::BuildGuildBankSwapItemsPlayerToBank(
    std::uint64_t banker_guid, std::uint8_t bank_tab, std::uint8_t bank_slot,
    std::uint32_t destination_item_entry, std::uint8_t player_bag, std::uint8_t player_slot,
    std::uint32_t stack_count) {
  WorldPacket pkt(Opcode::CMSG_GUILD_BANK_SWAP_ITEMS);
  pkt.AppendU64(banker_guid);
  pkt.AppendU8(0);
  pkt.AppendU8(bank_tab);
  pkt.AppendU8(bank_slot);
  pkt.AppendU32(destination_item_entry);
  pkt.AppendU8(0);
  pkt.AppendU8(player_bag);
  pkt.AppendU8(player_slot);
  pkt.AppendU8(0);
  pkt.AppendU32(stack_count);
  return pkt;
}

WorldPacket PacketSender::BuildGuildBankSwapItemsBankToPlayer(
    std::uint64_t banker_guid, std::uint8_t source_tab, std::uint8_t source_slot,
    std::uint32_t source_item_entry, std::uint8_t player_bag, std::uint8_t player_slot,
    std::uint32_t stack_count) {
  WorldPacket pkt(Opcode::CMSG_GUILD_BANK_SWAP_ITEMS);
  pkt.AppendU64(banker_guid);
  pkt.AppendU8(0);
  pkt.AppendU8(source_tab);
  pkt.AppendU8(source_slot);
  pkt.AppendU32(source_item_entry);
  pkt.AppendU8(0);
  pkt.AppendU8(player_bag);
  pkt.AppendU8(player_slot);
  pkt.AppendU8(1);
  pkt.AppendU32(stack_count);
  return pkt;
}

WorldPacket PacketSender::BuildGuildBankSwapItemsBankToBank(
    std::uint64_t banker_guid, std::uint8_t source_tab, std::uint8_t source_slot,
    std::uint32_t destination_item_entry, std::uint8_t destination_tab,
    std::uint8_t destination_slot, std::uint32_t held_item_entry, std::uint32_t stack_count) {
  WorldPacket pkt(Opcode::CMSG_GUILD_BANK_SWAP_ITEMS);
  pkt.AppendU64(banker_guid);
  pkt.AppendU8(1);
  pkt.AppendU8(source_tab);
  pkt.AppendU8(source_slot);
  pkt.AppendU32(destination_item_entry);
  pkt.AppendU8(destination_tab);
  pkt.AppendU8(destination_slot);
  pkt.AppendU32(held_item_entry);
  pkt.AppendU8(0);
  pkt.AppendU32(stack_count);
  return pkt;
}

WorldPacket PacketSender::BuildCalendarGetCalendar() {
  return WorldPacket(Opcode::CMSG_CALENDAR_GET_CALENDAR);
}

WorldPacket PacketSender::BuildCalendarGetNumPending() {
  return WorldPacket(Opcode::CMSG_CALENDAR_GET_NUM_PENDING);
}

WorldPacket PacketSender::BuildCalendarAddEvent(std::string_view title,
                                                std::string_view description,
                                                std::uint8_t event_type, std::uint8_t repeat_type,
                                                std::uint32_t max_invites, std::int32_t dungeon_id,
                                                std::uint32_t event_time,
                                                std::uint32_t time_zone_time, std::uint32_t flags,
                                                const std::vector<CalendarAddEventInvite> &invites) {
  WorldPacket pkt(Opcode::CMSG_CALENDAR_ADD_EVENT);
  AppendNullString(pkt, title);
  AppendNullString(pkt, description);
  pkt.AppendU8(event_type);
  pkt.AppendU8(repeat_type);
  pkt.AppendU32(max_invites);
  pkt.AppendU32(static_cast<std::uint32_t>(dungeon_id));
  pkt.AppendU32(event_time);
  pkt.AppendU32(time_zone_time);
  pkt.AppendU32(flags);
  if ((flags & 0x40u) == 0) {
    pkt.AppendU32(static_cast<std::uint32_t>(invites.size()));
    for (const auto &invite : invites) {
      AppendPackedGuid(pkt, invite.invitee);
      pkt.AppendU8(invite.status);
      pkt.AppendU8(invite.moderator_status);
    }
  }
  return pkt;
}

WorldPacket PacketSender::BuildCalendarRemoveEvent(std::uint64_t event_id, std::uint64_t invite_id,
                                                   std::uint32_t flags) {
  WorldPacket pkt(Opcode::CMSG_CALENDAR_REMOVE_EVENT);
  pkt.AppendU64(event_id);
  pkt.AppendU64(invite_id);
  pkt.AppendU32(flags);
  return pkt;
}

WorldPacket PacketSender::BuildCalendarRemoveEventBuffer(std::uint64_t event_id,
                                                         std::uint64_t invite_id,
                                                         const bool uses_guild_calendar) {
  WorldPacket pkt(Opcode::CMSG_CALENDAR_REMOVE_EVENT);
  pkt.AppendU64(event_id);
  pkt.AppendU64(invite_id);
  pkt.AppendU8(uses_guild_calendar ? 1u : 0u);
  return pkt;
}

WorldPacket PacketSender::BuildCalendarEventSignUp(std::uint64_t event_id,
                                                   const std::uint8_t tentative) {
  WorldPacket pkt(Opcode::CMSG_CALENDAR_EVENT_SIGNUP);
  pkt.AppendU64(event_id);
  pkt.AppendU8(tentative);
  return pkt;
}

WorldPacket PacketSender::BuildCalendarEventRsvp(std::uint64_t event_id, std::uint64_t invite_id,
                                                 std::uint32_t status) {
  WorldPacket pkt(Opcode::CMSG_CALENDAR_EVENT_RSVP);
  pkt.AppendU64(event_id);
  pkt.AppendU64(invite_id);
  pkt.AppendU32(status);
  return pkt;
}

WorldPacket PacketSender::BuildCalendarEventRemoveInvite(std::uint64_t target_invitee_guid,
                                                         std::uint64_t event_id,
                                                         std::uint64_t target_invite_id,
                                                         std::uint64_t self_invite_id) {
  WorldPacket pkt(Opcode::CMSG_CALENDAR_EVENT_REMOVE_INVITE);
  AppendPackedGuid(pkt, game::ObjectGuid(target_invitee_guid));
  pkt.AppendU64(target_invite_id);
  pkt.AppendU64(self_invite_id);
  pkt.AppendU64(event_id);
  return pkt;
}

WorldPacket PacketSender::BuildCalendarEventModeratorStatus(std::uint64_t target_invitee_guid,
                                                            std::uint64_t event_id,
                                                            std::uint64_t target_invite_id,
                                                            std::uint64_t self_invite_id,
                                                            std::uint32_t status) {
  WorldPacket pkt(Opcode::CMSG_CALENDAR_EVENT_MODERATOR_STATUS);
  pkt.AppendU64(target_invitee_guid);
  pkt.AppendU64(event_id);
  pkt.AppendU64(target_invite_id);
  pkt.AppendU64(self_invite_id);
  pkt.AppendU32(status);
  return pkt;
}

WorldPacket PacketSender::BuildCalendarEventStatus(std::uint64_t target_invitee_guid,
                                                   std::uint64_t event_id,
                                                   std::uint64_t target_invite_id,
                                                   std::uint64_t self_invite_id,
                                                   std::uint32_t status) {
  WorldPacket pkt(Opcode::CMSG_CALENDAR_EVENT_STATUS);
  pkt.AppendU64(target_invitee_guid);
  pkt.AppendU64(event_id);
  pkt.AppendU64(target_invite_id);
  pkt.AppendU64(self_invite_id);
  pkt.AppendU32(status);
  return pkt;
}

WorldPacket PacketSender::BuildLfgSetRoles(std::uint8_t roles) {
  WorldPacket pkt(Opcode::CMSG_LFG_SET_ROLES);
  pkt.AppendU8(roles);
  return pkt;
}

WorldPacket PacketSender::BuildLfgSetNeeds() {
  return WorldPacket(Opcode::CMSG_LFG_SET_NEEDS);
}

WorldPacket PacketSender::BuildLfgJoin(std::uint32_t roles,
                                       const std::vector<std::uint32_t> &dungeons,
                                       std::string_view comment) {
  WorldPacket pkt(Opcode::CMSG_LFG_JOIN);
  pkt.AppendU32(roles);

  pkt.AppendU8(0);
  pkt.AppendU8(0);

  auto count = static_cast<std::uint8_t>(dungeons.size() > 255 ? 255 : dungeons.size());
  pkt.AppendU8(count);
  for (std::uint8_t i = 0; i < count; ++i) {
    pkt.AppendU32(dungeons[i]);
  }
  pkt.AppendU8(3);
  pkt.AppendU8(0);
  pkt.AppendU8(0);
  pkt.AppendU8(0);
  AppendNullString(pkt, comment);
  return pkt;
}

WorldPacket PacketSender::BuildLfgLeave() {
  return WorldPacket(Opcode::CMSG_LFG_LEAVE);
}

WorldPacket PacketSender::BuildLfgProposalResult(std::uint32_t proposal_id, bool accept) {
  WorldPacket pkt(Opcode::CMSG_LFG_PROPOSAL_RESULT);
  pkt.AppendU32(proposal_id);
  pkt.AppendU8(accept ? 1 : 0);
  return pkt;
}

WorldPacket PacketSender::BuildLfgSetBootVote(bool agree) {
  WorldPacket pkt(Opcode::CMSG_LFG_SET_BOOT_VOTE);
  pkt.AppendU8(agree ? 1 : 0);
  return pkt;
}

WorldPacket PacketSender::BuildPetAction(std::uint64_t pet_guid, std::uint32_t action_data,
                                         std::uint64_t target_guid) {
  WorldPacket pkt(Opcode::CMSG_PET_ACTION);
  pkt.AppendU64(pet_guid);
  pkt.AppendU32(action_data);
  pkt.AppendU64(target_guid);
  return pkt;
}

WorldPacket PacketSender::BuildPetSetAction(std::uint64_t pet_guid,
                                            std::optional<PetSetActionSlotState> secondary_slot,
                                            PetSetActionSlotState target_slot) {
  WorldPacket pkt(Opcode::CMSG_PET_SET_ACTION);
  pkt.AppendU64(pet_guid);
  if (secondary_slot.has_value()) {
    pkt.AppendU32(secondary_slot->slot);
    pkt.AppendU32(secondary_slot->action_data);
  }
  pkt.AppendU32(target_slot.slot);
  pkt.AppendU32(target_slot.action_data);
  return pkt;
}

WorldPacket PacketSender::BuildPetRename(std::uint64_t pet_guid, std::string_view new_name,
                                         const std::array<std::string, 5> *declined_names) {
  WorldPacket pkt(Opcode::CMSG_PET_RENAME);
  pkt.AppendU64(pet_guid);
  AppendNullString(pkt, new_name);
  if (declined_names != nullptr) {
    pkt.AppendU8(1);
    for (const auto &form : *declined_names) {
      AppendNullString(pkt, form);
    }
  } else {
    pkt.AppendU8(0);
  }
  return pkt;
}

WorldPacket PacketSender::BuildPetitionRename(std::uint64_t petition_guid,
                                              std::string_view new_name) {
  WorldPacket pkt(Opcode::MSG_PETITION_RENAME);
  pkt.AppendU64(petition_guid);
  AppendNullString(pkt, new_name);
  return pkt;
}

WorldPacket PacketSender::BuildPetitionSign(std::uint64_t petition_guid,
                                            const std::uint8_t petition_choice) {
  WorldPacket pkt(Opcode::CMSG_PETITION_SIGN);
  pkt.AppendU64(petition_guid);
  pkt.AppendU8(petition_choice);
  return pkt;
}

WorldPacket PacketSender::BuildPetitionBuy(std::uint64_t npc_guid,
                                           const std::uint32_t petition_type,
                                           std::string_view petition_name) {
  WorldPacket pkt(Opcode::CMSG_PETITION_BUY);
  pkt.AppendU64(npc_guid);

  pkt.AppendU32(0);
  pkt.AppendU64(0);
  AppendNullString(pkt, petition_name);
  AppendNullString(pkt, "");
  pkt.AppendU32(0);
  pkt.AppendU32(0);
  for (int i = 0; i < 5; ++i) {
    pkt.AppendU32(0);
  }
  pkt.AppendU16(0);
  pkt.AppendU32(0);
  pkt.AppendU32(0);
  pkt.AppendU32(0);
  for (int i = 0; i < 10; ++i) {
    AppendNullString(pkt, "");
  }
  pkt.AppendU32(petition_type);
  pkt.AppendU32(0);
  return pkt;
}

WorldPacket PacketSender::BuildTurnInPetition(
    const std::uint64_t petition_guid,
    const std::array<std::uint32_t, 5>& extra_fields) {
  WorldPacket pkt(Opcode::CMSG_TURN_IN_PETITION);
  pkt.AppendU64(petition_guid);
  for (const auto value : extra_fields) {
    pkt.AppendU32(value);
  }
  return pkt;
}

WorldPacket PacketSender::BuildPetitionQuery(std::uint32_t petition_id,
                                             std::uint64_t petition_guid) {
  WorldPacket pkt(Opcode::CMSG_PETITION_QUERY);
  pkt.AppendU32(petition_id);
  pkt.AppendU64(petition_guid);
  return pkt;
}

WorldPacket PacketSender::BuildPetitionDecline(const std::uint64_t petition_guid) {
  WorldPacket pkt(Opcode::MSG_PETITION_DECLINE);
  pkt.AppendU64(petition_guid);
  return pkt;
}

WorldPacket PacketSender::BuildTabardVendorActivate(std::uint64_t vendor_guid) {
  WorldPacket pkt(Opcode::MSG_TABARDVENDOR_ACTIVATE);
  pkt.AppendU64(vendor_guid);
  return pkt;
}

WorldPacket PacketSender::BuildPetitionShowList(std::uint64_t npc_guid) {
  WorldPacket pkt(Opcode::CMSG_PETITION_SHOWLIST);
  pkt.AppendU64(npc_guid);
  return pkt;
}

WorldPacket PacketSender::BuildPetAbandon(std::uint64_t pet_guid) {
  WorldPacket pkt(Opcode::CMSG_PET_ABANDON);
  pkt.AppendU64(pet_guid);
  return pkt;
}

WorldPacket PacketSender::BuildDismissCritter(std::uint64_t critter_guid) {
  WorldPacket pkt(Opcode::CMSG_DISMISS_CRITTER);
  pkt.AppendU64(critter_guid);
  return pkt;
}

WorldPacket PacketSender::BuildPetSpellAutocast(std::uint64_t pet_guid, std::uint32_t spell_id,
                                                bool enabled) {
  WorldPacket pkt(Opcode::CMSG_PET_SPELL_AUTOCAST);
  pkt.AppendU64(pet_guid);
  pkt.AppendU32(spell_id);
  pkt.AppendU8(enabled ? 1 : 0);
  return pkt;
}

WorldPacket PacketSender::BuildRequestPetInfo() {
  return WorldPacket(Opcode::CMSG_REQUEST_PET_INFO);
}

WorldPacket PacketSender::BuildRepopRequest(bool auto_release) {
  WorldPacket pkt(Opcode::CMSG_REPOP_REQUEST);
  pkt.AppendU8(auto_release ? 1 : 0);
  return pkt;
}

WorldPacket PacketSender::BuildResurrectResponse(std::uint64_t resurrecter_guid, bool accept) {
  WorldPacket pkt(Opcode::CMSG_RESURRECT_RESPONSE);
  pkt.AppendU64(resurrecter_guid);
  pkt.AppendU8(accept ? 1 : 0);
  return pkt;
}

WorldPacket PacketSender::BuildSpiritHealerActivate(std::uint64_t healer_guid) {
  WorldPacket pkt(Opcode::CMSG_SPIRIT_HEALER_ACTIVATE);
  pkt.AppendU64(healer_guid);
  return pkt;
}

WorldPacket PacketSender::BuildAreaSpiritHealerQuery(std::uint64_t healer_guid) {
  WorldPacket pkt(Opcode::CMSG_AREA_SPIRIT_HEALER_QUERY);
  pkt.AppendU64(healer_guid);
  return pkt;
}

WorldPacket PacketSender::BuildAreaSpiritHealerQueue(std::uint64_t healer_guid) {
  WorldPacket pkt(Opcode::CMSG_AREA_SPIRIT_HEALER_QUEUE);
  pkt.AppendU64(healer_guid);
  return pkt;
}

WorldPacket PacketSender::BuildReclaimCorpse(const std::uint64_t corpse_guid) {
  WorldPacket pkt(Opcode::CMSG_RECLAIM_CORPSE);
  pkt.AppendU64(corpse_guid);
  return pkt;
}

WorldPacket PacketSender::BuildHearthAndResurrect() {
  WorldPacket pkt(Opcode::CMSG_HEARTH_AND_RESURRECT);
  return pkt;
}

WorldPacket PacketSender::BuildCompleteMovie() {
  WorldPacket pkt(Opcode::CMSG_COMPLETE_MOVIE);
  return pkt;
}

WorldPacket PacketSender::BuildReadyForAccountDataTimes() {
  return WorldPacket(Opcode::CMSG_READY_FOR_ACCOUNT_DATA_TIMES);
}

WorldPacket PacketSender::BuildRealmSplit(const std::uint32_t split_state) {
  WorldPacket packet(Opcode::CMSG_REALM_SPLIT);
  packet.AppendU32(split_state);
  return packet;
}

WorldPacket PacketSender::BuildWorldStateUiTimerUpdate() {
  WorldPacket pkt(Opcode::CMSG_WORLD_STATE_UI_TIMER_UPDATE);
  return pkt;
}

WorldPacket PacketSender::BuildInspect(std::uint64_t target_guid) {
  WorldPacket pkt(Opcode::CMSG_INSPECT);
  pkt.AppendU64(target_guid);
  return pkt;
}

WorldPacket PacketSender::BuildItemQuerySingle(std::uint32_t item_entry) {
  WorldPacket pkt(Opcode::CMSG_ITEM_QUERY_SINGLE);
  pkt.AppendU32(item_entry);
  return pkt;
}

WorldPacket PacketSender::BuildCreatureQuery(std::uint32_t entry, std::uint64_t guid) {
  WorldPacket pkt(Opcode::CMSG_CREATURE_QUERY);
  pkt.AppendU32(entry);
  pkt.AppendU64(guid);
  return pkt;
}

WorldPacket PacketSender::BuildGameObjectQuery(std::uint32_t entry, std::uint64_t guid) {
  WorldPacket pkt(Opcode::CMSG_GAMEOBJECT_QUERY);
  pkt.AppendU32(entry);
  pkt.AppendU64(guid);
  return pkt;
}

WorldPacket PacketSender::BuildSetPvP(const std::uint8_t flag) {
  WorldPacket pkt(Opcode::CMSG_TOGGLE_PVP);
  pkt.AppendU8(flag);
  return pkt;
}

WorldPacket PacketSender::BuildSetSheathed(std::uint32_t sheath_state) {
  WorldPacket pkt(Opcode::CMSG_SET_SHEATHED);
  pkt.AppendU32(sheath_state);
  return pkt;
}

WorldPacket PacketSender::BuildEmote(std::uint32_t emote_id) {
  WorldPacket pkt(Opcode::CMSG_EMOTE);
  pkt.AppendU32(emote_id);
  return pkt;
}

WorldPacket PacketSender::BuildTextEmote(std::uint32_t text_emote, std::uint32_t emote_num,
                                         std::uint64_t target_guid) {
  WorldPacket pkt(Opcode::CMSG_TEXT_EMOTE);
  pkt.AppendU32(text_emote);
  pkt.AppendU32(emote_num);
  pkt.AppendU64(target_guid);
  return pkt;
}

WorldPacket PacketSender::BuildPlayDance(std::uint32_t dance_id, std::uint32_t sequence_id) {
  WorldPacket pkt(Opcode::CMSG_PLAY_DANCE);
  pkt.AppendU32(dance_id);
  pkt.AppendU32(sequence_id);
  return pkt;
}

WorldPacket PacketSender::BuildCompleteCinematic() {
  return WorldPacket(Opcode::CMSG_COMPLETE_CINEMATIC);
}

WorldPacket PacketSender::BuildNextCinematicCamera() {
  return WorldPacket(Opcode::CMSG_NEXT_CINEMATIC_CAMERA);
}

WorldPacket PacketSender::BuildQueryTime() {
  return WorldPacket(Opcode::CMSG_QUERY_TIME);
}

WorldPacket PacketSender::BuildZoneUpdate(std::uint32_t zone_id) {
  WorldPacket pkt(Opcode::CMSG_ZONEUPDATE);
  pkt.AppendU32(zone_id);
  return pkt;
}

WorldPacket PacketSender::BuildSetActiveVoiceChannel(std::uint32_t channel_type,
                                                     std::string_view channel_name) {
  WorldPacket pkt(Opcode::CMSG_SET_ACTIVE_VOICE_CHANNEL);
  pkt.AppendU32(channel_type);
  AppendNullString(pkt, channel_name);
  return pkt;
}

}
