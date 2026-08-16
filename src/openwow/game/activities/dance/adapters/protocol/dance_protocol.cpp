#include "openwow/game/activities/dance/adapters/protocol/dance_protocol.h"

#include "openwow/network/serialization/packed_guid_codec.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace openwow::game::dance::protocol {
namespace {

constexpr std::size_t kDanceManagementNameMaxChars = 0x7FFFFFFF;

enum class DanceManagementWireFlag : std::uint32_t {
  kCreate = 0x1,
  kUpdate = 0x2,
  kRemove = 0x4,
  kError = 0x8,
};

[[nodiscard]] constexpr bool HasFlag(
    const std::uint32_t flags, const DanceManagementWireFlag flag) {
  return (flags & static_cast<std::uint32_t>(flag)) != 0;
}

class PayloadReader final {
public:
  explicit PayloadReader(const std::span<const std::uint8_t> payload)
      : payload_(payload) {}

  [[nodiscard]] bool ReadU32(std::uint32_t& value) {
    if (!CanReadFixedWidth(sizeof(value))) {
      return false;
    }
    value = static_cast<std::uint32_t>(payload_[offset_]) |
            (static_cast<std::uint32_t>(payload_[offset_ + 1]) << 8) |
            (static_cast<std::uint32_t>(payload_[offset_ + 2]) << 16) |
            (static_cast<std::uint32_t>(payload_[offset_ + 3]) << 24);
    offset_ += sizeof(value);
    return true;
  }

  [[nodiscard]] bool ReadU8(std::uint8_t& value) {
    if (!CanReadFixedWidth(sizeof(value))) {
      return false;
    }
    value = payload_[offset_++];
    return true;
  }

  [[nodiscard]] bool ReadI16(std::int16_t& value) {
    if (!CanReadFixedWidth(sizeof(value))) {
      return false;
    }
    const auto raw = static_cast<std::uint16_t>(payload_[offset_]) |
                     (static_cast<std::uint16_t>(payload_[offset_ + 1]) << 8);
    value = static_cast<std::int16_t>(raw);
    offset_ += sizeof(value);
    return true;
  }

  [[nodiscard]] bool ReadU64(std::uint64_t& value) {
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    if (!ReadU32(low) || !ReadU32(high)) {
      return false;
    }
    value = static_cast<std::uint64_t>(low) |
            (static_cast<std::uint64_t>(high) << 32);
    return true;
  }

  [[nodiscard]] bool ReadCString(const std::size_t capacity,
                                 std::string& value) {
    if (overrun_ || offset_ >= payload_.size()) {
      value.clear();
      overrun_ = true;
      return false;
    }
    const std::size_t end = std::min(payload_.size(), offset_ + capacity);
    std::size_t cursor = offset_;
    while (cursor < end && payload_[cursor] != 0) {
      ++cursor;
    }
    if (cursor == end) {
      value.clear();
      offset_ = payload_.size();
      overrun_ = true;
      return false;
    }
    value.assign(reinterpret_cast<const char*>(payload_.data() + offset_),
                 cursor - offset_);
    offset_ = cursor + 1;
    return true;
  }

  [[nodiscard]] std::size_t Remaining() const {
    return payload_.size() - offset_;
  }
  [[nodiscard]] std::span<const std::uint8_t> RemainingPayload() const {
    return payload_.subspan(offset_);
  }
  void Skip(const std::size_t byte_count) { offset_ += byte_count; }

private:
  [[nodiscard]] bool CanReadFixedWidth(const std::size_t byte_count) {

    if (overrun_ || Remaining() < byte_count) {
      overrun_ = true;
      return false;
    }
    return true;
  }

  std::span<const std::uint8_t> payload_;
  std::size_t offset_ = 0;
  bool overrun_ = false;
};

void AppendU32(std::vector<std::uint8_t>& payload,
               const std::uint32_t value) {
  payload.push_back(static_cast<std::uint8_t>(value));
  payload.push_back(static_cast<std::uint8_t>(value >> 8));
  payload.push_back(static_cast<std::uint8_t>(value >> 16));
  payload.push_back(static_cast<std::uint8_t>(value >> 24));
}

}

std::optional<StopDanceCommand>
DecodeStopDance(const std::span<const std::uint8_t> payload) {
  PayloadReader reader(payload);
  StopDanceCommand command;

  static_cast<void>(reader.ReadU64(command.unit_guid.value));
  return command;
}

std::optional<PlayDanceCommand>
DecodePlayDance(const std::span<const std::uint8_t> payload) {
  PayloadReader reader(payload);
  PlayDanceCommand command;

  static_cast<void>(reader.ReadU64(command.unit_guid.value));
  static_cast<void>(reader.ReadU32(command.dance_id.value));
  static_cast<void>(reader.ReadU32(command.start_step.value));
  static_cast<void>(reader.ReadU32(command.seed.value));
  static_cast<void>(reader.ReadU32(command.checksum.value));
  return command;
}

std::optional<DanceQueryResult>
DecodeDanceQueryResult(const std::span<const std::uint8_t> payload) {
  constexpr std::uint32_t kMissingDanceMask = 0x80000000u;

  PayloadReader reader(payload);
  std::uint32_t external_dance_id = 0;
  if (!reader.ReadU32(external_dance_id)) {
    return std::nullopt;
  }
  if ((external_dance_id & kMissingDanceMask) != 0) {
    return DanceQueryMissing{
        DanceId{external_dance_id & ~kMissingDanceMask}};
  }

  DanceCacheRecord dance;
  dance.id = DanceId{external_dance_id};
  const auto creator_guid =
      net::DecodePackedGuid(reader.RemainingPayload());
  if (!creator_guid) {
    return std::nullopt;
  }
  reader.Skip(creator_guid.bytes_consumed);
  dance.creator_guid = DanceUnitGuid{creator_guid.value};

  if (!reader.ReadCString(0x80, dance.name)) {
    return std::nullopt;
  }

  std::uint32_t move_count = 0;
  if (!reader.ReadU32(move_count) ||
      move_count > reader.Remaining() / 4) {
    return std::nullopt;
  }
  dance.moves.reserve(move_count);
  for (std::uint32_t index = 0; index < move_count; ++index) {
    DanceCacheMove move;
    std::uint8_t resolution_mode = 0;
    if (!reader.ReadI16(move.move_id.value) ||
        !reader.ReadU8(move.chance.value) ||
        !reader.ReadU8(resolution_mode)) {
      return std::nullopt;
    }
    move.resolution_mode =
        resolution_mode == 1
            ? DanceMoveResolutionMode::kCatalogFallback
            : DanceMoveResolutionMode::kDirect;
    dance.moves.push_back(move);
  }
  if (!reader.ReadU32(dance.checksum.value)) {
    return std::nullopt;
  }
  return DanceQueryFound{std::move(dance)};
}

std::optional<InvalidateDanceCommand>
DecodeInvalidateDance(const std::span<const std::uint8_t> payload) {
  PayloadReader reader(payload);
  InvalidateDanceCommand command;

  static_cast<void>(reader.ReadU32(command.dance_id.value));
  return command;
}

std::optional<LearnedDanceMovesUpdate>
DecodeLearnedDanceMoves(const std::span<const std::uint8_t> payload) {
  PayloadReader reader(payload);
  LearnedDanceMovesUpdate update;
  if (!reader.ReadU64(update.learned_move_mask.value)) {
    return std::nullopt;
  }
  return update;
}

std::optional<DanceManagementNotification>
DecodeDanceManagement(const std::span<const std::uint8_t> payload) {
  PayloadReader reader(payload);
  std::uint32_t flags = 0;

  static_cast<void>(reader.ReadU32(flags));
  if (HasFlag(flags, DanceManagementWireFlag::kError)) {
    std::uint32_t error_code = 0;
    static_cast<void>(reader.ReadU32(error_code));
    DanceManagementFailure failure;
    switch (error_code) {
    case 0:
      failure.error = DanceManagementError::kNameTaken;
      break;
    case 1:
      failure.error = DanceManagementError::kMaximumDancesReached;
      break;
    case 2:
      failure.error = DanceManagementError::kUnknownDance;
      break;
    default:
      break;
    }
    return DanceManagementNotification{std::move(failure)};
  }

  DanceManagementChange change;
  if (HasFlag(flags, DanceManagementWireFlag::kCreate)) {
    change.operations.Add(DanceManagementOperation::kCreate);
  }
  if (HasFlag(flags, DanceManagementWireFlag::kUpdate)) {
    change.operations.Add(DanceManagementOperation::kUpdate);
  }
  if (HasFlag(flags, DanceManagementWireFlag::kRemove)) {
    change.operations.Add(DanceManagementOperation::kRemove);
  }
  static_cast<void>(reader.ReadU32(change.dance_id.value));
  static_cast<void>(
      reader.ReadCString(kDanceManagementNameMaxChars, change.name));
  static_cast<void>(reader.ReadU32(change.sequence_id.value));
  return DanceManagementNotification{std::move(change)};
}

std::vector<std::uint8_t>
EncodeDanceCacheRecord(const DanceCacheRecord& dance) {
  const std::string_view name(
      dance.name.data(), std::min<std::size_t>(dance.name.size(), 0x7F));
  std::vector<std::uint8_t> payload;
  payload.reserve(4 + 9 + name.size() + 1 + 4 +
                  dance.moves.size() * 4 + 4);
  AppendU32(payload, dance.id.value);
  const auto encoded_guid = net::EncodePackedGuid(dance.creator_guid.value);
  payload.insert(payload.end(), encoded_guid.view().begin(),
                 encoded_guid.view().end());
  payload.insert(payload.end(), name.begin(), name.end());
  payload.push_back(0);
  AppendU32(payload, static_cast<std::uint32_t>(dance.moves.size()));
  for (const DanceCacheMove& move : dance.moves) {
    const auto move_id = static_cast<std::uint16_t>(move.move_id.value);
    payload.push_back(static_cast<std::uint8_t>(move_id));
    payload.push_back(static_cast<std::uint8_t>(move_id >> 8));
    payload.push_back(move.chance.value);
    payload.push_back(static_cast<std::uint8_t>(move.resolution_mode));
  }
  AppendU32(payload, dance.checksum.value);
  return payload;
}

}
