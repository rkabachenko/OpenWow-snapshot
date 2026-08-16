#pragma once

#include <cstdint>

namespace openwow::render {

enum class RenderResourceKind : std::uint8_t {
  Texture,
  VertexBuffer,
  IndexBuffer,
  VertexShader,
  PixelShader,
  VertexFormat,
};

struct RenderResourceKey {
  RenderResourceKind kind = RenderResourceKind::Texture;
  std::uint64_t id = 0;

  friend bool operator==(const RenderResourceKey&,
                         const RenderResourceKey&) = default;
};

}
