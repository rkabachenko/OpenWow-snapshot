#pragma once

#include <cstdint>

namespace openwow::game {

class UnitFootprintComponent final {
public:
  static constexpr float kDefaultScale = 0.27777779f;

  UnitFootprintComponent() = default;
  UnitFootprintComponent(const UnitFootprintComponent &) = delete;
  UnitFootprintComponent &operator=(const UnitFootprintComponent &) = delete;
  UnitFootprintComponent(UnitFootprintComponent &&) noexcept = default;
  UnitFootprintComponent &operator=(UnitFootprintComponent &&) noexcept = default;
  ~UnitFootprintComponent() = default;

  void SetTextureId(std::uint32_t id) noexcept { texture_id_ = id; }
  [[nodiscard]] std::uint32_t TextureId() const noexcept { return texture_id_; }
  void SetTerrainTypeId(std::uint32_t id) noexcept { terrain_type_id_ = id; }
  [[nodiscard]] std::uint32_t TerrainTypeId() const noexcept {
    return terrain_type_id_;
  }
  void SetWidth(float w) noexcept { width_ = w; }
  [[nodiscard]] float Width() const noexcept { return width_; }
  void SetLength(float l) noexcept { length_ = l; }
  [[nodiscard]] float Length() const noexcept { return length_; }
  void SetParticleScale(float s) noexcept { particle_scale_ = s; }
  [[nodiscard]] float ParticleScale() const noexcept { return particle_scale_; }

  void SetMountedTextureId(std::uint32_t id) noexcept {
    mounted_texture_id_ = id;
  }
  [[nodiscard]] std::uint32_t MountedTextureId() const noexcept {
    return mounted_texture_id_;
  }
  void SetMountedWidth(float w) noexcept { mounted_width_ = w; }
  [[nodiscard]] float MountedWidth() const noexcept { return mounted_width_; }
  void SetMountedLength(float l) noexcept { mounted_length_ = l; }
  [[nodiscard]] float MountedLength() const noexcept { return mounted_length_; }

private:
  std::uint32_t texture_id_{0xFFFFFFFFu};
  std::uint32_t terrain_type_id_{0xFFFFFFFFu};
  float width_{kDefaultScale};
  float length_{kDefaultScale};
  float particle_scale_{0.0f};
  std::uint32_t mounted_texture_id_{0};
  float mounted_width_{0.0f};
  float mounted_length_{0.0f};
};

}
