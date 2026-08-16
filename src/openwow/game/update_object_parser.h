#pragma once

#include "openwow/game/movement_info.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/object_types.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/update_fields.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace openwow::game {

using UpdateObjectTickProvider = std::uint32_t (*)();

struct UpdateFieldValues {
  std::vector<std::uint32_t> bitmask;
  std::vector<std::uint32_t> values;
  std::uint16_t field_count{0};

  [[nodiscard]] bool HasField(std::uint16_t index) const {
    if (index >= field_count)
      return false;
    std::uint32_t block = index / 32;
    std::uint32_t bit = index % 32;
    if (block >= bitmask.size())
      return false;
    return (bitmask[block] & (1u << bit)) != 0;
  }

  [[nodiscard]] std::uint32_t GetValue(std::uint16_t index) const;

  [[nodiscard]] float GetFloat(std::uint16_t index) const;

  [[nodiscard]] std::uint64_t GetU64(std::uint16_t index) const;

  [[nodiscard]] ObjectGuid GetGuid(std::uint16_t index) const;

  void BuildPrefixSum() const;

private:

  mutable std::vector<std::uint32_t> values_offset_;
  mutable bool prefix_sum_valid_{false};
};

template <typename Fn>
void ForEachAppliedUpdateField(const UpdateFieldValues &field_data, Fn &&fn) {
  if (field_data.field_count == 0) {
    return;
  }

  std::uint32_t value_index = 0;
  for (std::uint32_t field_index = 0; field_index < field_data.field_count; ++field_index) {
    const std::size_t block = field_index / 32u;
    if (block >= field_data.bitmask.size()) {
      break;
    }
    const std::uint32_t bit = field_index % 32u;
    if ((field_data.bitmask[block] & (1u << bit)) == 0) {
      continue;
    }

    fn(static_cast<std::uint16_t>(field_index), value_index);
    ++value_index;
  }
}

struct ValuesUpdate {
  ObjectGuid guid;
  UpdateFieldValues fields;
};

struct MovementOnlyUpdate {
  ObjectGuid guid;
  MovementUpdate movement;
  std::uint32_t client_receive_tick_ms{0};
  std::uint32_t presentation_tick_ms{0};
  bool has_resolved_presentation_tick{false};
};

struct CreateObjectUpdate {
  ObjectGuid guid;
  TypeID type_id{TypeID::kObject};
  MovementUpdate movement;
  UpdateFieldValues fields;
  bool is_self{false};
  std::uint32_t client_receive_tick_ms{0};

  bool defer_post_init{false};

  bool movement_applied_before_post_init{false};
};

struct OutOfRangeUpdate {
  std::vector<ObjectGuid> guids;
};

struct NearObjectsUpdate {
  std::vector<ObjectGuid> guids;
};

struct UpdateObjectHandler {
  std::function<void(const CreateObjectUpdate &)> on_create;
  std::function<void(const ValuesUpdate &)> on_values;

  std::function<void(ObjectGuid)> on_values_skipped;
  std::function<void(const MovementOnlyUpdate &)> on_movement;
  std::function<void(const OutOfRangeUpdate &)> on_out_of_range;
  std::function<void(const NearObjectsUpdate &)> on_near_objects;

  std::function<std::optional<std::uint16_t>(ObjectGuid)> resolve_values_field_count;
};

struct UpdateObjectParseStats {
  std::uint32_t declared_blocks{0};
  std::uint32_t completed_blocks{0};

  std::array<std::uint32_t, 6> blocks_by_type{};
};

bool ParseUpdateObject(const std::uint8_t *data, std::size_t len,
                       const UpdateObjectHandler &handler,
                       UpdateObjectParseStats *stats = nullptr);

bool ParseLeadingOutOfRangeUpdate(const std::uint8_t *data, std::size_t len,
                                  const UpdateObjectHandler &handler);

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
DecompressUpdateObjectPayload(const std::uint8_t *data, std::size_t len);

bool ReadMovementUpdate(PacketReader &reader, MovementUpdate &out);

bool ReadLivingMovementBody(PacketReader &reader, MovementUpdate &out);

bool ReadStandaloneMovementUpdate(PacketReader &reader, MovementUpdate &out);

bool ReadMovementInfo(PacketReader &reader, MovementInfo &out);

bool ReadUpdateFields(PacketReader &reader, std::uint16_t field_count, UpdateFieldValues &out);

void SetUpdateObjectTickProviderForTests(UpdateObjectTickProvider provider);

}
