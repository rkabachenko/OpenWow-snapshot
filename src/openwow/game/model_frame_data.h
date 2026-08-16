
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace openwow::game {

enum class SheatheState : std::uint8_t {
    Unsheathed = 0,
    Melee      = 1,
    Ranged     = 2,
};

struct ModelDisplayInfo {
    std::uint8_t race = 0;
    std::uint8_t gender = 0;
    std::uint8_t skin_color = 0;
    std::uint8_t face_type = 0;
    std::uint8_t hair_style = 0;
    std::uint8_t hair_color = 0;
    std::uint8_t facial_hair = 0;

    static constexpr std::uint32_t kEquipSlots = 19;
    std::array<std::uint32_t, kEquipSlots> equipment{};

    std::uint32_t mount_display_id = 0;
    SheatheState sheathe_state = SheatheState::Unsheathed;
};

class ModelFrameData {
 public:
  static ModelFrameData& Get();

  std::uint32_t CreateFrame(const std::string& name);

  void DestroyFrame(std::uint32_t frame_id);

  [[nodiscard]] std::uint32_t GetNumActiveFrames() const;

  void ForEachFrame(
      const std::function<void(std::uint32_t id, const std::string& name)>& fn) const;

  void SetDisplayInfo(std::uint32_t frame_id, const ModelDisplayInfo& info);
  [[nodiscard]] std::optional<ModelDisplayInfo> GetDisplayInfo(std::uint32_t frame_id) const;

  void SetRotation(std::uint32_t frame_id, float yaw, float pitch);
  [[nodiscard]] std::pair<float, float> GetRotation(std::uint32_t frame_id) const;

  void SetZoom(std::uint32_t frame_id, float zoom);
  [[nodiscard]] float GetZoom(std::uint32_t frame_id) const;

  void SetAnimation(std::uint32_t frame_id, std::uint32_t anim_id);
  [[nodiscard]] std::uint32_t GetAnimation(std::uint32_t frame_id) const;

  void SetAutoRotate(std::uint32_t frame_id, bool enabled);
  [[nodiscard]] bool GetAutoRotate(std::uint32_t frame_id) const;

  void SetPaused(std::uint32_t frame_id, bool paused);
  [[nodiscard]] bool IsPaused(std::uint32_t frame_id) const;

  void Reset();

 private:
  ModelFrameData() = default;

  struct FrameState {
      std::string name;
      ModelDisplayInfo display_info;
      float yaw = 0.0f;
      float pitch = 0.0f;
      float zoom = 1.0f;
      std::uint32_t anim_id = 0;
      bool auto_rotate = false;
      bool paused = false;
  };

  mutable std::mutex mutex_;
  std::unordered_map<std::uint32_t, FrameState> frames_;
  std::uint32_t next_id_{1};
};

}
