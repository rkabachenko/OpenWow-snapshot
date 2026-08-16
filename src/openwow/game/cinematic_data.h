
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

struct CinematicEntry {
    std::uint32_t cinematic_id = 0;
    std::string name;
    std::uint32_t sound_id = 0;
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    float origin_z = 0.0f;
    float origin_facing = 0.0f;
    std::uint32_t num_camera_points = 0;
};

struct CinematicCameraPoint {
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    float pos_z = 0.0f;
    float target_x = 0.0f;
    float target_y = 0.0f;
    float target_z = 0.0f;
    float time_ms = 0.0f;
    float speed = 1.0f;
    float fov = 70.0f;
};

class CinematicData {
 public:
  static CinematicData& Get();

  void AddCinematic(const CinematicEntry& entry);
  [[nodiscard]] std::optional<CinematicEntry> GetCinematic(std::uint32_t id) const;
  [[nodiscard]] bool HasCinematic(std::uint32_t id) const;
  [[nodiscard]] bool IsValidCinematic(std::uint32_t id) const;
  [[nodiscard]] std::vector<std::uint32_t> GetAllCinematicIds() const;

  void ForEach(const std::function<void(const CinematicEntry&)>& fn) const;

  void AddCameraPoint(std::uint32_t cinematic_id, const CinematicCameraPoint& pt);
  [[nodiscard]] std::vector<CinematicCameraPoint> GetCameraPoints(
      std::uint32_t cinematic_id) const;

  [[nodiscard]] float GetDuration(std::uint32_t cinematic_id) const;

  void Reset();

 private:
  CinematicData() = default;

  mutable std::mutex mutex_;
  std::unordered_map<std::uint32_t, CinematicEntry> cinematics_;
  std::unordered_map<std::uint32_t, std::vector<CinematicCameraPoint>> camera_points_;
};

}
