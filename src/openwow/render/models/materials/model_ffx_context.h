#pragma once

#include "openwow/render/models/animation/model_light_record.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace openwow::render {

struct ModelFfxContextBlock {
  static constexpr std::size_t kMaxLightRecords = 4;

  std::vector<ModelLightRecord> lights;
  bool ghost_branch_enabled = true;

  void Reset() {
    lights.clear();
  }

  [[nodiscard]] std::uint8_t ghost_branch_gate() const noexcept {
    return ghost_branch_enabled ? 0u : 1u;
  }

  void set_ghost_branch_gate(const std::uint8_t value) noexcept {
    ghost_branch_enabled = value == 0u;
  }

  bool SetLightRecord(const std::size_t index,
                      const ModelLightRecord& record) {
    if (index >= kMaxLightRecords) {
      return false;
    }
    if (lights.size() <= index) {
      lights.resize(index + 1u);
    }
    lights[index] = record;
    return true;
  }
};

inline constexpr std::size_t kModelFfxBackgroundLiveBlockIndex = 0;
inline constexpr std::size_t kModelFfxBackgroundGhostBlockIndex = 1;
inline constexpr std::size_t kModelFfxCharacterLiveBlockIndex = 2;
inline constexpr std::size_t kModelFfxCharacterGhostBlockIndex = 3;
inline constexpr std::size_t kModelFfxPetLiveBlockIndex = 4;
inline constexpr std::size_t kModelFfxPetGhostBlockIndex = 5;
inline constexpr std::size_t kModelFfxBlockCount = 6;

struct ModelFfxContext {
  std::array<ModelFfxContextBlock, kModelFfxBlockCount> blocks;

  void Reset() {
    for (auto& block : blocks) {
      block.Reset();
    }
  }
};

}
