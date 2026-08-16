
#include "openwow/game/update_object_parser.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/network/serialization/zlib_compression.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>
#include <vector>

namespace openwow::game {

namespace {

std::uint32_t DefaultUpdateObjectTickCount() {
  return openwow::core::GameClock::GetTickCount32();
}

UpdateObjectTickProvider g_update_object_tick_provider = &DefaultUpdateObjectTickCount;

std::uint32_t GetUpdateObjectTickCount() {
  return g_update_object_tick_provider != nullptr ? g_update_object_tick_provider()
                                                  : DefaultUpdateObjectTickCount();
}

void ResetMovementUpdateScratch(MovementUpdate &update) {

  update = MovementUpdate{};
}

void RebuildSplineCurve(SplineInfo &spline) {
  if (spline.waypoints.empty()) {
    spline.curve.SetControlPoints(nullptr, 0);
    return;
  }

  std::vector<render::C3Vector> control_points;
  control_points.reserve(spline.point_count());
  for (std::size_t index = 0; index + 2 < spline.waypoints.size(); index += 3) {
    control_points.push_back(
        {spline.waypoints[index], spline.waypoints[index + 1], spline.waypoints[index + 2]});
  }
  spline.curve.SetControlPoints(control_points);
}

std::uint16_t ResolveObjectFieldCount(TypeID type_id, const ObjectGuid &guid,
                                      bool is_active_player) {
  if (type_id != TypeID::kPlayer) {
    return FieldCountFor(type_id);
  }

  return FieldCountForPlayer(is_active_player || guid == CGObject_C::GetActivePlayerGuid());
}

bool SkipUpdateFields(PacketReader &reader) {
  std::uint8_t block_count = 0;
  if (!reader.ReadU8(block_count)) {
    return false;
  }

  if (block_count > kMaxValuesMaskBlocks) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "SkipUpdateFields: block_count " +
                                                           std::to_string(block_count) +
                                                           " exceeds max 42");
    return false;
  }

  std::array<std::uint32_t, kMaxValuesMaskBlocks> bitmask{};
  for (std::uint8_t block_index = 0; block_index < block_count; ++block_index) {
    if (!reader.ReadU32(bitmask[block_index])) {
      return false;
    }
  }

  std::uint32_t discarded_value = 0;
  const std::uint32_t total_mask_bits = static_cast<std::uint32_t>(block_count) * 32u;
  for (std::uint32_t field_index = 0; field_index < total_mask_bits; ++field_index) {
    const std::size_t block_index = field_index / 32u;
    const std::uint32_t bit = field_index % 32u;
    if ((bitmask[block_index] & (1u << bit)) == 0) {
      continue;
    }

    if (!reader.ReadU32(discarded_value)) {
      return false;
    }
  }

  return true;
}

template <typename GuidListUpdate>
bool ReadGuidListUpdate(PacketReader &reader, GuidListUpdate &update) {
  std::uint32_t count = 0;
  if (!reader.ReadU32(count)) {
    return false;
  }

  if (count > reader.Remaining()) {

    return false;
  }

  update.guids.resize(count);
  for (auto &guid : update.guids) {
    if (!reader.ReadPackedGuid(guid)) {
      return false;
    }
  }
  return true;
}

bool LogUpdateObjectParseFailure(const PacketReader& reader,
                                  const std::uint32_t block_index,
                                  const std::uint8_t update_type,
                                  const std::string_view stage,
                                  const std::size_t payload_size,
                                  const std::uint32_t declared_blocks) {
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kWarn,
       "UpdateObject: parse failed block=" + std::to_string(block_index) +
          " parsed=" + std::to_string(block_index) +
          " declared=" + std::to_string(declared_blocks) +
          " type=" + std::to_string(update_type) + " stage=" +
          std::string(stage) + " offset=" +
          std::to_string(reader.Position()) + "/" +
          std::to_string(payload_size) + " remaining=" +
          std::to_string(reader.Remaining()));
  return false;
}

}

void UpdateFieldValues::BuildPrefixSum() const {
  if (prefix_sum_valid_)
    return;
  const std::uint32_t total_bits = static_cast<std::uint32_t>(bitmask.size()) * 32u;
  values_offset_.resize(total_bits);
  std::uint32_t running = 0;
  for (std::uint32_t i = 0; i < total_bits; ++i) {
    values_offset_[i] = running;
    const std::uint32_t block = i / 32;
    const std::uint32_t bit = i % 32;
    if (bitmask[block] & (1u << bit))
      ++running;
  }
  prefix_sum_valid_ = true;
}

std::uint32_t UpdateFieldValues::GetValue(std::uint16_t index) const {
  if (!HasField(index))
    return 0;
  BuildPrefixSum();
  if (index >= values_offset_.size())
    return 0;
  const std::uint32_t offset = values_offset_[index];
  return offset < values.size() ? values[offset] : 0;
}

float UpdateFieldValues::GetFloat(std::uint16_t index) const {
  std::uint32_t raw = GetValue(index);
  float result;
  std::memcpy(&result, &raw, 4);
  return result;
}

std::uint64_t UpdateFieldValues::GetU64(std::uint16_t index) const {
  std::uint32_t lo = GetValue(index);
  std::uint32_t hi = GetValue(index + 1);
  return static_cast<std::uint64_t>(lo) | (static_cast<std::uint64_t>(hi) << 32);
}

ObjectGuid UpdateFieldValues::GetGuid(std::uint16_t index) const {
  return ObjectGuid(GetU64(index));
}

bool ReadMovementInfo(PacketReader &reader, MovementInfo &out) {
  ResetMovementInfoScratch(out);

  if (!reader.ReadU32(out.flags))
    return false;
  if (!reader.ReadU16(out.flags2))
    return false;
  if (!reader.ReadU32(out.time))
    return false;
  if (!reader.ReadFloat(out.x))
    return false;
  if (!reader.ReadFloat(out.y))
    return false;
  if (!reader.ReadFloat(out.z))
    return false;
  if (!reader.ReadFloat(out.orientation))
    return false;

  if (out.HasFlag(kMoveFlagOnTransport)) {
    if (!reader.ReadPackedGuid(out.transport.guid))
      return false;
    if (!reader.ReadFloat(out.transport.offset_x))
      return false;
    if (!reader.ReadFloat(out.transport.offset_y))
      return false;
    if (!reader.ReadFloat(out.transport.offset_z))
      return false;
    if (!reader.ReadFloat(out.transport.offset_o))
      return false;
    if (!reader.ReadU32(out.transport.time))
      return false;
    std::uint8_t transport_seat = 0;
    if (!reader.ReadU8(transport_seat))
      return false;
    out.transport.seat = static_cast<std::int8_t>(transport_seat);

    if (out.HasFlag2(kMoveFlag2InterpolatedMovement)) {
      if (!reader.ReadU32(out.transport.time2))
        return false;
      out.flags2 &= ~static_cast<std::uint16_t>(kMoveFlag2InterpolatedMovement);
    } else {
      out.transport.time2 = out.transport.time;
    }
  } else {
    out.transport.guid = ObjectGuid{};
    out.transport.seat = -1;
  }

  if (out.HasFlag(kMoveFlagSwimming) || out.HasFlag(kMoveFlagFlying) ||
      out.HasFlag2(kMoveFlag2AlwaysAllowPitching)) {
    if (!reader.ReadFloat(out.pitch))
      return false;
  }

  if (!reader.ReadU32(out.fall_time))
    return false;

  if (out.HasFlag(kMoveFlagFalling)) {
    if (!reader.ReadFloat(out.jump.z_speed))
      return false;
    if (!reader.ReadFloat(out.jump.sin_angle))
      return false;
    if (!reader.ReadFloat(out.jump.cos_angle))
      return false;
    if (!reader.ReadFloat(out.jump.xy_speed))
      return false;
  }

  if (out.HasFlag(kMoveFlagSplineElevation)) {
    if (!reader.ReadFloat(out.spline_elevation))
      return false;
  }

  return true;
}

bool ReadLivingMovementBody(PacketReader &reader, MovementUpdate &out) {
  {

    if (!ReadMovementInfo(reader, out.movement))
      return false;

    for (int i = 0; i < kMaxSpeeds; ++i) {
      if (!reader.ReadFloat(out.speeds[i]))
        return false;
    }

    if (out.movement.HasFlag(kMoveFlagSplineEnabled)) {
      auto &sp = out.spline;
      sp.active = true;

      if (!reader.ReadU32(sp.flags))
        return false;

      if (sp.flags & 0x00020000) {

        sp.facing_type = SplineFacing::kAngle;
        if (!reader.ReadFloat(sp.facing_angle))
          return false;
      } else if (sp.flags & 0x00010000) {

        sp.facing_type = SplineFacing::kTarget;
        if (!reader.ReadGuid(sp.facing_target))
          return false;
      } else if (sp.flags & 0x00008000) {

        sp.facing_type = SplineFacing::kPoint;
        if (!reader.ReadFloat(sp.facing_x))
          return false;
        if (!reader.ReadFloat(sp.facing_y))
          return false;
        if (!reader.ReadFloat(sp.facing_z))
          return false;
      }

      if (!reader.ReadU32(sp.time_passed))
        return false;
      if (!reader.ReadU32(sp.duration))
        return false;

      if (!reader.ReadU32(sp.spline_id))
        return false;

      if (!reader.ReadFloat(sp.duration_mod))
        return false;
      if (!reader.ReadFloat(sp.duration_mod_next))
        return false;

      if (!reader.ReadFloat(sp.vertical_acceleration))
        return false;
      if (!reader.ReadI32(sp.effect_start_time))
        return false;

      std::uint32_t num_points;
      if (!reader.ReadU32(num_points))
        return false;
      constexpr std::size_t kSplineTrailerBytes = sizeof(std::uint8_t) + 3u * sizeof(float);
      if (reader.Remaining() < kSplineTrailerBytes ||
          num_points > (reader.Remaining() - kSplineTrailerBytes) /
                           (3u * sizeof(float))) {
        return false;
      }
      sp.waypoints.resize(static_cast<std::size_t>(num_points) * 3);
      for (std::uint32_t i = 0; i < num_points; ++i) {
        if (!reader.ReadFloat(sp.waypoints[i * 3 + 0]))
          return false;
        if (!reader.ReadFloat(sp.waypoints[i * 3 + 1]))
          return false;
        if (!reader.ReadFloat(sp.waypoints[i * 3 + 2]))
          return false;
      }
      RebuildSplineCurve(sp);

      if (!reader.ReadU8(sp.mode))
        return false;

      if (!reader.ReadFloat(sp.dest_x))
        return false;
      if (!reader.ReadFloat(sp.dest_y))
        return false;
      if (!reader.ReadFloat(sp.dest_z))
        return false;
    }
  }

  return true;
}

bool ReadMovementUpdate(PacketReader &reader, MovementUpdate &out) {
  ResetMovementUpdateScratch(out);

  if (!reader.ReadU16(out.update_flags))
    return false;

  if (out.HasUpdateFlag(kUpdateFlagLiving)) {
    if (!ReadLivingMovementBody(reader, out))
      return false;
  } else {

    if (out.HasUpdateFlag(kUpdateFlagPosition)) {

      if (!reader.ReadPackedGuid(out.transport_guid))
        return false;
      if (!reader.ReadFloat(out.position_x))
        return false;
      if (!reader.ReadFloat(out.position_y))
        return false;
      if (!reader.ReadFloat(out.position_z))
        return false;
      if (!reader.ReadFloat(out.transport_offset_x))
        return false;
      if (!reader.ReadFloat(out.transport_offset_y))
        return false;
      if (!reader.ReadFloat(out.transport_offset_z))
        return false;
      if (!reader.ReadFloat(out.position_o))
        return false;
      if (!reader.ReadFloat(out.corpse_o))
        return false;
    } else if (out.HasUpdateFlag(kUpdateFlagStationaryPosition)) {
      if (!reader.ReadFloat(out.stationary_x))
        return false;
      if (!reader.ReadFloat(out.stationary_y))
        return false;
      if (!reader.ReadFloat(out.stationary_z))
        return false;
      if (!reader.ReadFloat(out.stationary_o))
        return false;
    }
  }

  if (out.HasUpdateFlag(kUpdateFlagUnknown)) {
    if (!reader.ReadU32(out.unknown_value))
      return false;
  }

  if (out.HasUpdateFlag(kUpdateFlagLowGuid)) {
    if (!reader.ReadU32(out.low_guid_value))
      return false;
  }

  if (out.HasUpdateFlag(kUpdateFlagHasTarget)) {
    if (!reader.ReadPackedGuid(out.target_guid))
      return false;
  }

  if (out.HasUpdateFlag(kUpdateFlagTransport)) {
    if (!reader.ReadU32(out.transport_path_timer))
      return false;
  }

  if (out.HasUpdateFlag(kUpdateFlagVehicle)) {
    if (!reader.ReadU32(out.vehicle_id))
      return false;
    if (!reader.ReadFloat(out.vehicle_orientation))
      return false;
  }

  if (out.HasUpdateFlag(kUpdateFlagRotation)) {
    if (!reader.ReadI64(out.go_rotation))
      return false;
  }

  return true;
}

bool ReadStandaloneMovementUpdate(PacketReader &reader, MovementUpdate &out) {
  ResetMovementUpdateScratch(out);
  out.update_flags = kUpdateFlagLiving;
  return ReadLivingMovementBody(reader, out);
}

bool ReadUpdateFields(PacketReader &reader, std::uint16_t field_count, UpdateFieldValues &out) {
  out.field_count = field_count;
  std::uint8_t block_count;
  if (!reader.ReadU8(block_count))
    return false;

  if (block_count > kMaxValuesMaskBlocks) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "ReadUpdateFields: block_count " +
                                                           std::to_string(block_count) +
                                                           " exceeds max 42");
    return false;
  }

  out.bitmask.resize(block_count);
  for (std::uint8_t i = 0; i < block_count; ++i) {
    if (!reader.ReadU32(out.bitmask[i]))
      return false;
  }

  std::uint32_t set_bits = 0;
  const std::uint32_t max_fields = out.field_count;
  for (std::uint32_t field_index = 0; field_index < max_fields; ++field_index) {
    const std::size_t block = field_index / 32u;
    if (block >= out.bitmask.size()) {
      break;
    }
    const std::uint32_t bit = field_index % 32u;
    if ((out.bitmask[block] & (1u << bit)) != 0) {
      ++set_bits;
    }
  }

  out.values.resize(set_bits);
  for (std::uint32_t i = 0; i < set_bits; ++i) {
    if (!reader.ReadU32(out.values[i]))
      return false;
  }

  return true;
}

void SetUpdateObjectTickProviderForTests(UpdateObjectTickProvider provider) {
  g_update_object_tick_provider = provider != nullptr ? provider : &DefaultUpdateObjectTickCount;
}

bool ParseUpdateObject(const std::uint8_t *data, std::size_t len,
                       const UpdateObjectHandler &handler,
                       UpdateObjectParseStats *stats) {
  PacketReader reader(data, len);

  if (stats != nullptr) {
    *stats = {};
  }

  std::uint32_t block_count;
  if (!reader.ReadU32(block_count)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "UpdateObject: failed to read block count payload=" +
            std::to_string(len) + " bytes");
    return false;
  }
  if (stats != nullptr) {
    stats->declared_blocks = block_count;
  }

  std::array<std::uint32_t, 6> counts{};

  for (std::uint32_t i = 0; i < block_count; ++i) {
    std::uint8_t update_type_raw;
    if (!reader.ReadU8(update_type_raw)) {
      return LogUpdateObjectParseFailure(reader, i, 0xFFu, "update-type",
                                         len, block_count);
    }
    auto update_type = static_cast<UpdateType>(update_type_raw);

    switch (update_type) {
    case UpdateType::kValues: {
      ValuesUpdate upd;
      if (!reader.ReadPackedGuid(upd.guid)) {
        return LogUpdateObjectParseFailure(reader, i, update_type_raw,
                                           "values-guid", len, block_count);
      }

      std::optional<std::uint16_t> values_field_count = FieldCountFor(TypeID::kPlayer);
      if (handler.resolve_values_field_count) {
        values_field_count = handler.resolve_values_field_count(upd.guid);
      }
      if (!values_field_count.has_value()) {
        if (!SkipUpdateFields(reader)) {
          return LogUpdateObjectParseFailure(reader, i, update_type_raw,
                                             "unknown-values", len, block_count);
        }
        if (handler.on_values_skipped)
          handler.on_values_skipped(upd.guid);
        break;
      }
      if (!ReadUpdateFields(reader, *values_field_count, upd.fields)) {
        return LogUpdateObjectParseFailure(reader, i, update_type_raw,
                                           "values-fields", len, block_count);
      }
      if (handler.on_values)
        handler.on_values(upd);
      break;
    }

    case UpdateType::kMovement: {
      MovementOnlyUpdate upd;
      if (!reader.ReadPackedGuid(upd.guid)) {
        return LogUpdateObjectParseFailure(reader, i, update_type_raw,
                                           "movement-guid", len, block_count);
      }
      if (!ReadStandaloneMovementUpdate(reader, upd.movement)) {
        return LogUpdateObjectParseFailure(
            reader, i, update_type_raw,
            "movement-body flags=" +
                std::to_string(upd.movement.update_flags) +
                " moveFlags=" +
                std::to_string(upd.movement.movement.flags),
            len, block_count);
      }
      upd.client_receive_tick_ms = GetUpdateObjectTickCount();
      if (handler.on_movement)
        handler.on_movement(upd);
      break;
    }

    case UpdateType::kCreateObject:
    case UpdateType::kCreateObject2: {
      CreateObjectUpdate upd;
      upd.is_self = (update_type == UpdateType::kCreateObject2);
      if (!reader.ReadPackedGuid(upd.guid)) {
        return LogUpdateObjectParseFailure(reader, i, update_type_raw,
                                           "create-guid", len, block_count);
      }
      if (upd.guid.IsEmpty()) {
        return LogUpdateObjectParseFailure(reader, i, update_type_raw,
                                           "create-empty-guid", len,
                                           block_count);
      }
      std::uint8_t type_raw;
      if (!reader.ReadU8(type_raw)) {
        return LogUpdateObjectParseFailure(reader, i, update_type_raw,
                                           "create-type", len, block_count);
      }
      if (type_raw >= static_cast<std::uint8_t>(kNumClientObjectTypes)) {
        return LogUpdateObjectParseFailure(
            reader, i, update_type_raw,
            "create-invalid-object-type=" + std::to_string(type_raw), len,
            block_count);
      }
      upd.type_id = static_cast<TypeID>(type_raw);
      if (!ReadMovementUpdate(reader, upd.movement)) {
        return LogUpdateObjectParseFailure(
            reader, i, update_type_raw,
            "create-movement objectType=" + std::to_string(type_raw) +
                " flags=" + std::to_string(upd.movement.update_flags) +
                " moveFlags=" +
                std::to_string(upd.movement.movement.flags),
            len, block_count);
      }
      if (!ReadUpdateFields(reader,
                            ResolveObjectFieldCount(upd.type_id, upd.guid, upd.movement.IsSelf()),
                            upd.fields)) {
        return LogUpdateObjectParseFailure(
            reader, i, update_type_raw,
            "create-fields objectType=" + std::to_string(type_raw), len,
            block_count);
      }
      upd.client_receive_tick_ms = GetUpdateObjectTickCount();
      if (handler.on_create)
        handler.on_create(upd);
      break;
    }

    case UpdateType::kOutOfRangeObjects: {
      OutOfRangeUpdate upd;
      if (!ReadGuidListUpdate(reader, upd)) {
        return LogUpdateObjectParseFailure(reader, i, update_type_raw,
                                           "out-of-range-list", len,
                                           block_count);
      }
      if (handler.on_out_of_range)
        handler.on_out_of_range(upd);
      break;
    }

    case UpdateType::kNearObjects: {
      NearObjectsUpdate upd;
      if (!ReadGuidListUpdate(reader, upd)) {
        return LogUpdateObjectParseFailure(reader, i, update_type_raw,
                                           "near-list", len, block_count);
      }
      if (handler.on_near_objects)
        handler.on_near_objects(upd);
      break;
    }

    default:
      return LogUpdateObjectParseFailure(reader, i, update_type_raw,
                                         "unknown-update-type", len,
                                         block_count);
    }

    ++counts[update_type_raw];
    if (stats != nullptr) {
      stats->completed_blocks = i + 1u;
      stats->blocks_by_type = counts;
    }
  }

  return true;
}

bool ParseLeadingOutOfRangeUpdate(const std::uint8_t *data,
                                  const std::size_t len,
                                  const UpdateObjectHandler &handler) {
  PacketReader reader(data, len);
  std::uint32_t block_count = 0;
  if (!reader.ReadU32(block_count)) {
    return false;
  }
  if (block_count == 0) {
    return true;
  }

  std::uint8_t update_type = 0;
  if (!reader.ReadU8(update_type)) {
    return false;
  }
  if (update_type != static_cast<std::uint8_t>(UpdateType::kOutOfRangeObjects)) {
    return true;
  }

  OutOfRangeUpdate update;
  if (!ReadGuidListUpdate(reader, update)) {
    return false;
  }
  if (handler.on_out_of_range) {
    handler.on_out_of_range(update);
  }
  return true;
}

std::optional<std::vector<std::uint8_t>>
DecompressUpdateObjectPayload(const std::uint8_t *data, const std::size_t len) {
  if (data == nullptr) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "CompressedUpdateObject: null payload");
    return std::nullopt;
  }
  if (len < 4) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "CompressedUpdateObject: payload too small");
    return std::nullopt;
  }

  const std::uint32_t uncompressed_size =
      static_cast<std::uint32_t>(data[0]) |
      (static_cast<std::uint32_t>(data[1]) << 8u) |
      (static_cast<std::uint32_t>(data[2]) << 16u) |
      (static_cast<std::uint32_t>(data[3]) << 24u);

  std::vector<std::uint8_t> decompressed(
      std::max<std::size_t>(uncompressed_size, 1u));
  std::size_t dest_len = uncompressed_size;
  const auto result = openwow::network::serialization::DecompressZlib(
      decompressed.data(), &dest_len, data + 4, len - 4);
  if (result != openwow::network::serialization::ZlibResult::kOk) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "CompressedUpdateObject: zlib decompress failed ret=" +
            std::to_string(static_cast<int>(result)) + " expected=" +
            std::to_string(uncompressed_size) + " actual=" +
            std::to_string(dest_len));
    return std::nullopt;
  }

  decompressed.resize(dest_len);
  return decompressed;
}

}
