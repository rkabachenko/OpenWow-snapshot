#pragma once

#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/terrain/adt_file.h"
#include "openwow/data/terrain/wdl_file.h"
#include "openwow/data/wmo/wmo_file.h"
#include "openwow/world/environment/weather.h"
#include "openwow/world/liquid/water_heightfield.h"
#include "openwow/world/streaming/stream_identity.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace openwow::world {

struct ResetPresentationCommand {};
struct PublishDistantTerrainCommand {
  std::shared_ptr<const data::terrain::WdlFile> wdl;
};
struct PublishTerrainTileCommand {
  std::uint64_t owner{};
  std::int32_t tile_x{};
  std::int32_t tile_y{};
  std::shared_ptr<const data::terrain::AdtFile> adt;
  std::shared_ptr<const std::vector<WaterHeightfield>> liquids;

  bool big_alpha{false};
};
struct RemoveTerrainTileCommand {
  std::uint64_t owner{};
  std::int32_t tile_x{};
  std::int32_t tile_y{};
};
struct BeginWorldModelCommand {
  std::string resource_key;
  std::shared_ptr<const data::wmo::WmoRoot> root;
  std::uint32_t group_count{};
};
struct PublishWorldModelGroupCommand {
  std::string resource_key;
  std::uint32_t group_index{};
  std::shared_ptr<const data::wmo::WmoGroup> group;
  std::vector<std::uint32_t> material_indices;
};
struct RemoveWorldModelCommand {
  std::string resource_key;
};

enum class WmoDoodadAnimation : std::uint8_t {
  kNone,
  kDestructibleTransition,

  kDestructibleAmbientStop,

  kDestructibleAmbientLoop,

  kDestructibleImpact,

  kTransportShipStart,
  kTransportShipMoving,
  kTransportShipStop,
};
struct WmoDoodadAnimationControl {

  std::uint16_t doodad_set{};
  WmoDoodadAnimation animation{WmoDoodadAnimation::kNone};

  [[nodiscard]] constexpr bool operator==(
      const WmoDoodadAnimationControl&) const = default;
};
struct PublishWorldModelInstanceCommand {
  std::uint64_t stable_id{};
  std::uint64_t doodad_owner{};

  std::uint64_t object_guid{};
  std::string resource_key;
  std::array<float, 16> transform{};
  std::uint16_t doodad_set{};
  std::array<std::uint16_t, 3> additional_doodad_sets{};
  std::array<WmoDoodadAnimationControl, 2> doodad_animation_controls{};
  bool visible{true};
  std::uint32_t group_count{};
};
struct RemoveWorldModelInstanceCommand {
  std::uint64_t stable_id{};
  std::uint64_t doodad_owner{};
};

struct UpdateWorldModelInstanceCommand {
  std::uint64_t stable_id{};
  std::uint64_t doodad_owner{};
  std::array<float, 16> transform{};
  std::uint16_t doodad_set{};
  std::array<std::uint16_t, 3> additional_doodad_sets{};
  std::array<WmoDoodadAnimationControl, 2> doodad_animation_controls{};
  bool visible{true};
};

struct TransferWorldModelDoodadSetCommand {
  std::uint64_t source_doodad_owner{};
  std::uint64_t destination_doodad_owner{};
  std::uint16_t source_doodad_set{};
  std::uint16_t destination_doodad_set{};
};
struct PublishWorldModelLiquidCommand {
  std::uint64_t stable_id{};
  std::uint32_t group_index{};
  std::shared_ptr<const WaterHeightfield> liquid;
};
struct SetWeatherPresentationCommand {
  WeatherKind type{WeatherKind::kNone};
  float density{};
  std::optional<data::dbc::WeatherEntry> row;
  bool smooth{true};
};

struct SpawnWaterRippleCommand {
  std::array<float, 3> position{};
  std::vector<std::array<float, 3>> control_points;
  float rotation_radians{};
  float initial_extent{};
  float duration_seconds{};
  float opacity_base{};
  float extent_rate{};
  bool use_splash_texture{};
  bool use_local_player_pool{};
};

using WorldPresentationCommand = std::variant<
    ResetPresentationCommand, PublishDistantTerrainCommand,
    PublishTerrainTileCommand, RemoveTerrainTileCommand,
    BeginWorldModelCommand, PublishWorldModelGroupCommand,
    RemoveWorldModelCommand, PublishWorldModelInstanceCommand,
    RemoveWorldModelInstanceCommand, UpdateWorldModelInstanceCommand,
    TransferWorldModelDoodadSetCommand,
    PublishWorldModelLiquidCommand,
    SetWeatherPresentationCommand, SpawnWaterRippleCommand>;

struct WorldPresentationCommandBatch {
  MapGeneration generation{};
  std::vector<WorldPresentationCommand> commands;
};

enum class WmoGroupPublicationStatus : std::uint8_t {
  kPending,
  kDrawableReady,
  kNoGeometry,
  kRetryableFailure,
  kFailed,
};

[[nodiscard]] inline constexpr bool IsWmoGroupPublicationComplete(
    const WmoGroupPublicationStatus status) noexcept {
  return status == WmoGroupPublicationStatus::kDrawableReady ||
         status == WmoGroupPublicationStatus::kNoGeometry;
}

[[nodiscard]] inline constexpr bool ShouldQueueWmoGroupPublication(
    const WmoGroupPublicationStatus status) noexcept {
  return status == WmoGroupPublicationStatus::kPending;
}

[[nodiscard]] inline constexpr std::uint64_t WmoGroupPublicationRetryDelay(
    const std::uint8_t retry_count) noexcept {
  return std::uint64_t{1u} << std::min<std::uint8_t>(retry_count, 6u);
}

struct WmoGroupPublicationRetryState {
  std::uint64_t retry_after_drain{};
  std::uint8_t retry_count{};

  [[nodiscard]] constexpr bool CanQueue(
      const std::uint64_t drain_sequence) const noexcept {
    return drain_sequence >= retry_after_drain;
  }

  constexpr void RecordRetryableFailure(
      const std::uint64_t drain_sequence) noexcept {
    retry_after_drain =
        drain_sequence + WmoGroupPublicationRetryDelay(retry_count);
    retry_count = std::min<std::uint8_t>(
        static_cast<std::uint8_t>(retry_count + 1u), 7u);
  }

  constexpr void Reset() noexcept {
    retry_after_drain = 0u;
    retry_count = 0u;
  }
};

[[nodiscard]] inline constexpr const char* WmoGroupPublicationStatusName(
    const WmoGroupPublicationStatus status) noexcept {
  switch (status) {
  case WmoGroupPublicationStatus::kPending:
    return "pending";
  case WmoGroupPublicationStatus::kDrawableReady:
    return "drawable-ready";
  case WmoGroupPublicationStatus::kNoGeometry:
    return "no-geometry";
  case WmoGroupPublicationStatus::kRetryableFailure:
    return "retryable-failure";
  case WmoGroupPublicationStatus::kFailed:
    return "terminal-malformed";
  }
  return "unknown";
}

struct WmoGroupPresentationResult {
  std::string resource_key;
  std::uint32_t group_index{};
  WmoGroupPublicationStatus status{WmoGroupPublicationStatus::kFailed};
};

struct WorldPresentationAcknowledgment {
  MapGeneration generation{};
  std::vector<WmoGroupPresentationResult> wmo_groups;
};

[[nodiscard]] inline constexpr bool TryConsumePublicationByteBudget(
    const std::uint64_t item_bytes, const std::uint64_t frame_budget,
    std::uint64_t& remaining_bytes) noexcept {
  if (item_bytes <= remaining_bytes) {
    remaining_bytes -= item_bytes;
    return true;
  }
  if (remaining_bytes == frame_budget && item_bytes > frame_budget) {
    remaining_bytes = 0u;
    return true;
  }
  return false;
}

}
