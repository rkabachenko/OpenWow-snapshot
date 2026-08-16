#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/render/m2/m2_public_types.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace openwow::render::m2 {
class M2System;
}

namespace openwow::render {

struct PortraitTexture {
  bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
  std::uint16_t width{};
  std::uint16_t height{};
};

struct PortraitAcquireResult {
  m2::M2ResultStatus status{m2::M2ResultStatus::kNotReady};
  m2::M2ResultReason reason{m2::M2ResultReason::kNone};
  std::string detail;
  std::optional<PortraitTexture> texture;
};

class PortraitRenderer final {
 public:
  explicit PortraitRenderer(m2::M2System& models);
  ~PortraitRenderer();

  PortraitRenderer(const PortraitRenderer&) = delete;
  PortraitRenderer& operator=(const PortraitRenderer&) = delete;

  void Initialize();
  void Shutdown();
  void InvalidatePlayer(game::ObjectGuid guid);
  void InvalidateAll();

  [[nodiscard]] PortraitAcquireResult Acquire(
      game::ObjectGuid guid, std::uint32_t display_id,
      std::uint32_t model_instance_id, std::uint8_t& next_view_id,
      std::uint16_t view_id_limit);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}
