#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::world {
int CinematicScene_LoadModel(void* scene, const char* model_path,
                             float* position, float orientation,
                             int anim_id, int anim_sub, int flags);
}

namespace openwow::render::m2 {
class M2System;
}
namespace openwow::audio { class SoundRuntime; }

namespace openwow::game {

class WorldSession;

using CinematicSendPacketFn =
    std::function<void(std::uint16_t opcode,
                       const std::uint8_t* data, std::size_t len)>;

enum class CinematicState : std::uint8_t {
  Idle,
  Playing,
  Paused,
  Finished,
};

struct CinematicKeyframe {
  float time = 0.0f;
  float cameraX = 0.0f;
  float cameraY = 0.0f;
  float cameraZ = 0.0f;
  float targetX = 0.0f;
  float targetY = 0.0f;
  float targetZ = 0.0f;
  float fov = 70.0f;
  float roll = 0.0f;
};

struct SubtitleEntry {
  float startTime = 0.0f;
  float endTime = 0.0f;
  std::string text;
};

struct CinematicSequence {
  std::uint32_t cinematicId = 0;
  std::vector<CinematicKeyframe> keyframes;
  float totalDuration = 0.0f;
  std::string musicPath;
  std::vector<SubtitleEntry> subtitles;
};

struct CinematicCameraState {
  float x = 0.0f, y = 0.0f, z = 0.0f;
  float tx = 0.0f, ty = 0.0f, tz = 0.0f;
  float fov = 70.0f;
  float roll = 0.0f;
};

class CinematicPlayer {
 public:
  CinematicPlayer(openwow::render::m2::M2System& m2_system,
                  openwow::audio::SoundRuntime& sound_runtime);
  ~CinematicPlayer();

  CinematicPlayer(const CinematicPlayer&) = delete;
  CinematicPlayer& operator=(const CinematicPlayer&) = delete;

  void BindDbc(const openwow::data::dbc::DbcLoader* dbc) { dbc_ = dbc; }

  void SetModelFileLoader(
      std::function<std::vector<std::uint8_t>(const std::string&)> loader);

  void SetSendPacketFn(CinematicSendPacketFn fn) { send_fn_ = std::move(fn); }

  void SetStopCallback(std::function<void()> fn) { stop_callback_ = std::move(fn); }

  bool PlaySequence(WorldSession& session,
                    std::uint32_t cinematic_sequence_id);

  void Play(const CinematicSequence& seq);

  bool Play(std::uint32_t cinematicId);

  void Stop(WorldSession& session);

  void Pause();

  void Resume();

  [[nodiscard]] CinematicState GetState() const { return state_; }

  [[nodiscard]] float GetCurrentTime() const { return elapsed_; }

  [[nodiscard]] float GetTotalDuration() const;

  [[nodiscard]] float GetProgress() const;

  [[nodiscard]] CinematicCameraState GetCurrentCamera() const;

  [[nodiscard]] std::optional<SubtitleEntry> GetCurrentSubtitle() const;

  [[nodiscard]] bool IsPlaying() const { return state_ == CinematicState::Playing; }

  [[nodiscard]] bool IsPresenting() const {
    return state_ == CinematicState::Playing &&
           !(sequence_runtime_active_ && pending_sequence_callback_);
  }

  [[nodiscard]] bool CanSkip() const { return can_skip_; }
  void SetCanSkip(bool v) { can_skip_ = v; }

  void Skip(WorldSession& session);

  void SetCinematic(std::uint32_t cinematicId, const CinematicSequence& seq);

  [[nodiscard]] std::optional<CinematicSequence> GetCinematic(std::uint32_t cinematicId) const;

  [[nodiscard]] std::uint32_t GetCinematicCount() const;

  void Update(WorldSession& session, float dt_seconds);

  void AbortForWorldLeave(WorldSession& session);

  void Reset();

  bool GetCameraOverride(float* out_view,
                         float& out_x, float& out_y, float& out_z) const;

  bool GetCameraOverride(float* out_view,
                         float& out_x, float& out_y, float& out_z,
                         float aspect_ratio,
                         float& out_vertical_fov_radians) const;

 private:
 friend int openwow::world::CinematicScene_LoadModel(void* scene,
                                                      const char* model_path,
                                                      float* position,
                                                      float orientation,
                                                      int anim_id,
                                                      int anim_sub,
                                                      int flags);

  struct SequenceCameraModel {
    std::string normalized_path;
    std::uint32_t model_id{0};
    float scene_transform[16]{};
    float animation_duration_seconds{0.0f};
  };

  void CancelPendingSequenceEvent();
  void ScheduleSequenceEvent(float delay_seconds, std::function<void()> callback,
                             const char* debug_name);
  void RunSequenceAdvanceCallback(WorldSession& session);
  void AdvanceToNextSequenceCamera(WorldSession& session);
  bool StartCurrentSequenceCamera(WorldSession& session);
  void HandleCurrentCameraLoaded(WorldSession& session, int error);
  void FinishSequence(WorldSession& session, bool send_complete_packet);
  void StopTrackedSoundHandle(std::uint32_t& handle, bool clear_cinematic_handle);
  void ResetSequenceRuntimeState();
  void ResetSequenceCameraModel();
  bool LoadSequenceCameraModel(const char* model_path, const float* position,
                              float orientation);
  [[nodiscard]] std::optional<CinematicCameraState> SampleSequenceCamera() const;
  void SendNextCinematicCamera();
  void SendCompleteCinematic(WorldSession& session);
  static CinematicCameraState InterpolateKeyframes(
      const std::vector<CinematicKeyframe>& kfs, float t);

  const openwow::data::dbc::DbcLoader* dbc_{nullptr};
  CinematicSendPacketFn send_fn_;
  std::function<void()> stop_callback_;
  std::function<std::vector<std::uint8_t>(const std::string&)> model_file_loader_;
  openwow::render::m2::M2System& m2_system_;
  openwow::audio::SoundRuntime& sound_runtime_;

  CinematicState state_{CinematicState::Idle};
  bool can_skip_{true};
  float elapsed_{0.0f};
  float duration_{30.0f};

  std::optional<CinematicSequence> active_seq_;

  std::unordered_map<std::uint32_t, CinematicSequence> sequences_;

  bool legacy_orbit_{false};
  bool sequence_runtime_active_{false};
  bool pending_sequence_callback_{false};
  bool sequence_stop_pending_{false};
  std::uint32_t active_sequence_id_{0};
  std::uint32_t active_sequence_sound_id_{0};
  std::uint32_t active_sequence_sound_handle_{0};
  std::array<std::uint32_t, 8> active_camera_ids_{};
  std::size_t active_camera_index_{0};
  std::uint32_t active_camera_sound_handle_{0};
  std::uint32_t pending_sequence_event_id_{0};
  std::unique_ptr<SequenceCameraModel> active_camera_model_;

  float origin_x_{0.0f};
  float origin_y_{0.0f};
  float origin_z_{0.0f};
  float origin_facing_{0.0f};

  static constexpr float kOrbitRadius = 50.0f;
  static constexpr float kOrbitSpeed  = 0.15f;
  static constexpr float kHeightBias  = 20.0f;
};

}
