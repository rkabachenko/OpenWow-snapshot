#include "openwow/game/activities/dance/adapters/data/dbc_dance_move_catalog.h"

#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/data/formats/dbc/dbc_store.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace openwow::game {
namespace {

[[nodiscard]] DanceMoveAction DecodeDanceMoveAction(
    const std::uint32_t external_type,
    const std::uint32_t external_parameter) {
  switch (external_type) {
    case 0:
      return DanceEmoteAnimationAction{
          DanceEmoteAnimationId{external_parameter}};
    case 1:
      return DanceAnimationDataAction{
          DanceAnimationDataId{external_parameter}};
    case 2:
      return DanceSoundAction{DanceSoundKitId{external_parameter}};
    case 3:
      return DanceDelayAction{
          DanceDelayDuration{std::chrono::seconds(external_parameter)}};
    case 4:
      return DanceRepeatPreviousAction{
          DanceDelayDuration{std::chrono::seconds(external_parameter)}};
    default:
      return UnsupportedDanceMoveAction{};
  }
}

[[nodiscard]] std::optional<DanceLearnedMoveIndex>
DecodeLearnedMoveRequirement(const std::uint32_t external_index) {
  if (external_index == 0) {
    return std::nullopt;
  }
  return DanceLearnedMoveIndex{external_index - 1};
}

}

DanceMoveCatalog BuildDanceMoveCatalog(
    const data::dbc::DbcStore<data::dbc::DanceMovesEntry>& store) {
  std::vector<DanceMoveRecord> records;
  records.reserve(store.size());

  for (const data::dbc::DanceMovesEntry& entry : store) {
    records.push_back({
        .id = DanceMoveId{static_cast<std::int16_t>(entry.id)},
        .action = DecodeDanceMoveAction(entry.type, entry.action_parameter),
        .fallback_step_id =
            DanceMoveId{static_cast<std::int16_t>(entry.fallback_step_id)},
        .required_classes = DanceClassMask{entry.required_class_mask},
        .required_learned_move = DecodeLearnedMoveRequirement(
            entry.required_learned_move_index),
        .name = std::string(entry.name),
    });
  }

  return DanceMoveCatalog(std::move(records));
}

}
