
#include "openwow/game/cinematic_player.h"

#include "openwow/game/world_session.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/client_misc.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/chat_cache.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/event_scheduler.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/foundation/math/m2_camera_matrix.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace openwow::game {

namespace {

constexpr float kCinematicStepDelaySeconds = 0.25f;
constexpr std::size_t kMaxCinematicSequenceCameras = 8;

constexpr std::uint16_t CMSG_COMPLETE_CINEMATIC = 0x0FC;
constexpr std::uint16_t CMSG_NEXT_CINEMATIC_CAMERA = 0x0FB;

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

void BuildPackedYawTranslationAffine4x3(const float origin[3],
                                        float facing_radians,
                                        float out_affine4x3[12]) {
  const float sine = std::sin(facing_radians);
  const float cosine = std::cos(facing_radians);

  out_affine4x3[0] = cosine;
  out_affine4x3[1] = sine;
  out_affine4x3[2] = 0.0f;
  out_affine4x3[3] = -sine;
  out_affine4x3[4] = cosine;
  out_affine4x3[5] = 0.0f;
  out_affine4x3[6] = 0.0f;
  out_affine4x3[7] = 0.0f;
  out_affine4x3[8] = 1.0f;
  out_affine4x3[9] = origin[0];
  out_affine4x3[10] = origin[1];
  out_affine4x3[11] = origin[2];
}

std::size_t CountActiveCameraIds(
    const std::array<std::uint32_t, kMaxCinematicSequenceCameras>& camera_ids) {
  return static_cast<std::size_t>(std::count_if(
      camera_ids.begin(), camera_ids.end(),
      [](const std::uint32_t camera_id) { return camera_id != 0; }));
}

void ReleaseCinematicInputState() {
  SetCinematicInputBlocked(false);
  ResetInputAfterCinematicTransition();
}

}

CinematicPlayer::CinematicPlayer(render::m2::M2System& m2_system,
                                 openwow::audio::SoundRuntime& sound_runtime)
    : m2_system_(m2_system), sound_runtime_(sound_runtime) {}

CinematicPlayer::~CinematicPlayer() {
  CancelPendingSequenceEvent();
}

void CinematicPlayer::SetModelFileLoader(
    std::function<std::vector<std::uint8_t>(const std::string&)> loader) {
  model_file_loader_ = std::move(loader);
  m2_system_.SetFileLoader(model_file_loader_);
}

bool CinematicPlayer::PlaySequence(WorldSession& session,
                                   std::uint32_t cinematic_sequence_id) {
  CancelPendingSequenceEvent();
  StopTrackedSoundHandle(active_sequence_sound_handle_,
                         false);
  StopTrackedSoundHandle(active_camera_sound_handle_,
                         true);
  ResetSequenceRuntimeState();
  active_seq_.reset();

  state_ = CinematicState::Playing;
  elapsed_ = 0.0f;
  duration_ = kCinematicStepDelaySeconds;
  legacy_orbit_ = false;
  sequence_runtime_active_ = true;
  pending_sequence_callback_ = true;
  active_sequence_id_ = cinematic_sequence_id;
  SetCinematicInputBlocked(true);

  bool sequence_entry_found = false;
  if (dbc_ != nullptr) {
    if (const auto* seq =
            dbc_->cinematic_sequences().LookupEntry(cinematic_sequence_id)) {
      sequence_entry_found = true;
      active_sequence_sound_id_ = seq->sound_id;
      active_camera_ids_ = seq->camera_ids;
    }
  }

  const auto camera_count = CountActiveCameraIds(active_camera_ids_);
  if (camera_count != 0) {
    duration_ = static_cast<float>(camera_count) * kCinematicStepDelaySeconds;
  }

  auto &sound_interface = sound_runtime_;
  if (sequence_entry_found && sound_interface.IsMusicCVarEnabled()) {

    sound_interface.ResetZoneAndScriptMusicRuntime();
    if (active_sequence_sound_id_ != 0) {
      openwow::audio::SoundKitPlaybackOptions options;
      options.sound_type = 3;
      const int result = sound_interface.PlaySoundKit(
          active_sequence_sound_id_, nullptr, &active_sequence_sound_handle_, options);
      if (result != 0) {
        active_sequence_sound_handle_ = 0;
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kWarn,
            "CinematicPlayer: sequence sound kit " +
                std::to_string(active_sequence_sound_id_) +
                " failed result=" + std::to_string(result));
      } else {
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kInfo,
            "CinematicPlayer: sequence sound kit " +
                std::to_string(active_sequence_sound_id_) + " queued handle=" +
                std::to_string(active_sequence_sound_handle_));
      }
    }
  }

  ScheduleSequenceEvent(
      kCinematicStepDelaySeconds,
      [this, &session]() { RunSequenceAdvanceCallback(session); },
      "CinematicPlayer_RunSequenceAdvanceCallback");

  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "CinematicPlayer: queued sequence "
          + std::to_string(cinematic_sequence_id) + " with "
          + std::to_string(camera_count) + " camera step(s)");
  return true;
}

void CinematicPlayer::Play(const CinematicSequence& seq) {
  CancelPendingSequenceEvent();
  ResetSequenceRuntimeState();
  active_seq_ = seq;
  elapsed_ = 0.0f;
  duration_ = seq.totalDuration;
  state_ = CinematicState::Playing;
  legacy_orbit_ = false;
  SetCinematicInputBlocked(true);
}

bool CinematicPlayer::Play(std::uint32_t cinematicId) {
  const auto it = sequences_.find(cinematicId);
  if (it == sequences_.end()) return false;
  Play(it->second);
  return true;
}

void CinematicPlayer::Stop(WorldSession& session) {
  if (state_ == CinematicState::Idle) return;

  if (sequence_runtime_active_ || pending_sequence_callback_) {
    if (sequence_stop_pending_) {
      return;
    }

    CancelPendingSequenceEvent();
    StopTrackedSoundHandle(active_camera_sound_handle_,
                           true);
    sequence_stop_pending_ = true;
    ScheduleSequenceEvent(
        kCinematicStepDelaySeconds,
        [this, &session]() {
          FinishSequence(session, true);
        },
        "CinematicPlayer_CompleteStoppedSequence");
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "CinematicPlayer: stop transition armed");
    return;
  }

  state_ = CinematicState::Finished;
  elapsed_ = 0.0f;
  active_seq_.reset();
  legacy_orbit_ = false;

  SendCompleteCinematic(session);
  if (stop_callback_) {
    stop_callback_();
  }
  openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
      openwow::ui::game::events::CINEMATIC_STOP);
  ReleaseCinematicInputState();

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "CinematicPlayer: stopped");
}

void CinematicPlayer::Pause() {
  if (state_ == CinematicState::Playing) {
    state_ = CinematicState::Paused;
  }
}

void CinematicPlayer::Resume() {
  if (state_ == CinematicState::Paused) {
    state_ = CinematicState::Playing;
  }
}

void CinematicPlayer::Skip(WorldSession& session) {
  if (can_skip_ &&
      (state_ == CinematicState::Playing || state_ == CinematicState::Paused)) {
    Stop(session);
  }
}

float CinematicPlayer::GetTotalDuration() const {
  if (active_seq_) return active_seq_->totalDuration;
  if (state_ != CinematicState::Idle) return duration_;
  return 0.0f;
}

float CinematicPlayer::GetProgress() const {
  const float dur = GetTotalDuration();
  if (dur <= 0.0f) return 0.0f;
  const float p = elapsed_ / dur;
  return std::clamp(p, 0.0f, 1.0f);
}

CinematicCameraState CinematicPlayer::GetCurrentCamera() const {
  if (active_seq_ && !active_seq_->keyframes.empty()) {
    return InterpolateKeyframes(active_seq_->keyframes, elapsed_);
  }

  if (const auto sampled = SampleSequenceCamera()) {
    return *sampled;
  }

  CinematicCameraState cs;
  cs.x = origin_x_;
  cs.y = origin_y_;
  cs.z = origin_z_;
  cs.tx = origin_x_;
  cs.ty = origin_y_;
  cs.tz = origin_z_;
  return cs;
}

std::optional<SubtitleEntry> CinematicPlayer::GetCurrentSubtitle() const {
  if (!active_seq_) return std::nullopt;
  for (const auto& sub : active_seq_->subtitles) {
    if (elapsed_ >= sub.startTime && elapsed_ <= sub.endTime) {
      return sub;
    }
  }
  return std::nullopt;
}

void CinematicPlayer::SetCinematic(std::uint32_t cinematicId,
                                   const CinematicSequence& seq) {
  CinematicSequence s = seq;
  s.cinematicId = cinematicId;
  sequences_[cinematicId] = std::move(s);
}

std::optional<CinematicSequence> CinematicPlayer::GetCinematic(
    std::uint32_t cinematicId) const {
  const auto it = sequences_.find(cinematicId);
  if (it == sequences_.end()) return std::nullopt;
  return it->second;
}

std::uint32_t CinematicPlayer::GetCinematicCount() const {
  return static_cast<std::uint32_t>(sequences_.size());
}

void CinematicPlayer::Update(WorldSession& session, float dt_seconds) {
  if (state_ != CinematicState::Playing) return;

  if (sequence_runtime_active_ && pending_sequence_callback_) {
    return;
  }

  elapsed_ += dt_seconds;

  if (sequence_runtime_active_) {
    return;
  }

  if (elapsed_ >= duration_) {
    Stop(session);
  }
}

void CinematicPlayer::AbortForWorldLeave(WorldSession& session) {
  if (state_ == CinematicState::Idle && !sequence_runtime_active_ &&
      !pending_sequence_callback_ && !active_seq_.has_value()) {
    return;
  }

  if (sequence_runtime_active_ || pending_sequence_callback_) {
    FinishSequence(session, false);
    state_ = CinematicState::Idle;
    ResetSequenceCameraModel();
  } else {
    state_ = CinematicState::Idle;
    elapsed_ = 0.0f;
    duration_ = 0.0f;
    active_seq_.reset();
    legacy_orbit_ = false;
    StopTrackedSoundHandle(active_sequence_sound_handle_,
                           false);
    StopTrackedSoundHandle(active_camera_sound_handle_,
                           true);
    ResetSequenceRuntimeState();
    ResetSequenceCameraModel();
    if (stop_callback_) {
      stop_callback_();
    }
    openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
        openwow::ui::game::events::CINEMATIC_STOP);
    ReleaseCinematicInputState();
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "CinematicPlayer: aborted for world leave");
}

void CinematicPlayer::Reset() {
  const bool had_active_state =
      state_ != CinematicState::Idle || sequence_runtime_active_ ||
      pending_sequence_callback_ || active_seq_.has_value();

  CancelPendingSequenceEvent();
  StopTrackedSoundHandle(active_sequence_sound_handle_,
                         false);
  StopTrackedSoundHandle(active_camera_sound_handle_,
                         true);
  ResetSequenceRuntimeState();
  state_ = CinematicState::Idle;
  elapsed_ = 0.0f;
  duration_ = 30.0f;
  active_seq_.reset();
  sequences_.clear();
  legacy_orbit_ = false;
  can_skip_ = true;
  origin_x_ = origin_y_ = origin_z_ = origin_facing_ = 0.0f;
  ResetSequenceCameraModel();
  if (had_active_state) {
    ReleaseCinematicInputState();
  }
}

bool CinematicPlayer::GetCameraOverride(float* out_view, float& out_x,
                                        float& out_y, float& out_z) const {
  float ignored_vertical_fov = 0.0f;
  return GetCameraOverride(out_view, out_x, out_y, out_z, 1.0f,
                           ignored_vertical_fov);
}

bool CinematicPlayer::GetCameraOverride(float* out_view, float& out_x,
                                        float& out_y, float& out_z,
                                        const float aspect_ratio,
                                        float& out_vertical_fov_radians) const {
  if (state_ != CinematicState::Playing) return false;
  if (sequence_runtime_active_ && pending_sequence_callback_) return false;

  if (const auto sampled = SampleSequenceCamera()) {
    const float position[3] = {sampled->x, sampled->y, sampled->z};
    const float target[3] = {sampled->tx, sampled->ty, sampled->tz};
    openwow::math::BuildM2CameraViewMatrix(position, target, sampled->roll, out_view);
    out_x = sampled->x;
    out_y = sampled->y;
    out_z = sampled->z;
    out_vertical_fov_radians = openwow::render::m2::M2System::BuildCameraVerticalFov(
        sampled->fov, aspect_ratio);
    return true;
  }

  if (legacy_orbit_) {
    const float angle = origin_facing_ + elapsed_ * kOrbitSpeed;

    const float eye_x = origin_x_ + kOrbitRadius * std::cos(angle);
    const float eye_y = origin_y_ + kOrbitRadius * std::sin(angle);
    const float eye_z = origin_z_ + kHeightBias;

    const float eye[3] = {eye_x, eye_y, eye_z};
    const float target[3] = {origin_x_, origin_y_, origin_z_};

    openwow::math::BuildM2CameraViewMatrix(eye, target, 0.0f, out_view);

    out_x = eye_x;
    out_y = eye_y;
    out_z = eye_z;
    out_vertical_fov_radians = 0.0f;
    return true;
  }

  const auto cam = GetCurrentCamera();
  const float eye[3] = {cam.x, cam.y, cam.z};
  const float target[3] = {cam.tx, cam.ty, cam.tz};
  openwow::math::BuildM2CameraViewMatrix(eye, target, cam.roll, out_view);
  out_x = cam.x;
  out_y = cam.y;
  out_z = cam.z;
  constexpr float kPi = 3.14159265358979323846f;
  out_vertical_fov_radians =
      cam.fov > kPi ? cam.fov * (kPi / 180.0f) : cam.fov;
  return true;
}

CinematicCameraState CinematicPlayer::InterpolateKeyframes(
    const std::vector<CinematicKeyframe>& kfs, float t) {
  if (kfs.empty()) return {};

  if (t <= kfs.front().time || kfs.size() == 1) {
    const auto& k = kfs.front();
    return {k.cameraX, k.cameraY, k.cameraZ,
            k.targetX, k.targetY, k.targetZ, k.fov, k.roll};
  }

  if (t >= kfs.back().time) {
    const auto& k = kfs.back();
    return {k.cameraX, k.cameraY, k.cameraZ,
            k.targetX, k.targetY, k.targetZ, k.fov, k.roll};
  }

  for (std::size_t i = 0; i + 1 < kfs.size(); ++i) {
    if (t >= kfs[i].time && t <= kfs[i + 1].time) {
      const auto& a = kfs[i];
      const auto& b = kfs[i + 1];
      const float range = b.time - a.time;
      const float frac = (range > 0.0f) ? (t - a.time) / range : 0.0f;

      CinematicCameraState cs;
      cs.x = Lerp(a.cameraX, b.cameraX, frac);
      cs.y = Lerp(a.cameraY, b.cameraY, frac);
      cs.z = Lerp(a.cameraZ, b.cameraZ, frac);
      cs.tx = Lerp(a.targetX, b.targetX, frac);
      cs.ty = Lerp(a.targetY, b.targetY, frac);
      cs.tz = Lerp(a.targetZ, b.targetZ, frac);
      cs.fov = Lerp(a.fov, b.fov, frac);
      cs.roll = Lerp(a.roll, b.roll, frac);
      return cs;
    }
  }

  const auto& k = kfs.back();
  return {k.cameraX, k.cameraY, k.cameraZ,
          k.targetX, k.targetY, k.targetZ, k.fov, k.roll};
}

void CinematicPlayer::CancelPendingSequenceEvent() {
  if (pending_sequence_event_id_ != 0) {
    EventScheduler::Get().Cancel(pending_sequence_event_id_);
    pending_sequence_event_id_ = 0;
  }
}

void CinematicPlayer::ScheduleSequenceEvent(
    float delay_seconds, std::function<void()> callback,
    const char* debug_name) {
  CancelPendingSequenceEvent();
  pending_sequence_event_id_ =
      EventScheduler::Get().ScheduleOnce(delay_seconds, std::move(callback),
                                         debug_name ? debug_name : "");
}

void CinematicPlayer::RunSequenceAdvanceCallback(WorldSession& session) {
  pending_sequence_event_id_ = 0;
  pending_sequence_callback_ = false;
  if (!sequence_runtime_active_) {
    return;
  }

  openwow::core::LoadingScreen_CleanupResources(session.sound_runtime());
  openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
      openwow::ui::game::events::CINEMATIC_START);

  if (!StartCurrentSequenceCamera(session)) {
    FinishSequence(session, true);
  }
}

void CinematicPlayer::AdvanceToNextSequenceCamera(WorldSession& session) {
  pending_sequence_event_id_ = 0;
  if (!sequence_runtime_active_) {
    return;
  }

  StopTrackedSoundHandle(active_camera_sound_handle_,
                         true);

  if (active_camera_index_ + 1 < active_camera_ids_.size()) {
    ++active_camera_index_;
  } else {
    active_camera_index_ = active_camera_ids_.size();
  }

  if (!StartCurrentSequenceCamera(session)) {
    FinishSequence(session, true);
  }
}

bool CinematicPlayer::StartCurrentSequenceCamera(WorldSession& session) {
  if (!sequence_runtime_active_ || active_camera_index_ >= active_camera_ids_.size()) {
    return false;
  }

  const auto camera_id = active_camera_ids_[active_camera_index_];
  if (camera_id == 0 || dbc_ == nullptr) {
    return false;
  }

  const auto* cam = dbc_->cinematic_camera().LookupEntry(camera_id);
  if (cam == nullptr || cam->model.empty()) {
    return false;
  }

  origin_x_ = cam->origin_x;
  origin_y_ = cam->origin_y;
  origin_z_ = cam->origin_z;
  origin_facing_ = cam->origin_facing;
  elapsed_ = 0.0f;

  const std::string model_path(cam->model);
  float position[3] = {origin_x_, origin_y_, origin_z_};
  if (!openwow::world::CinematicScene_LoadModel(
          this, model_path.c_str(), position, origin_facing_, 0, 0, 0)) {
    return false;
  }

  if (active_camera_model_ != nullptr) {
    duration_ = active_camera_model_->animation_duration_seconds
                + kCinematicStepDelaySeconds;
  }

  SendNextCinematicCamera();

  auto &sound_interface = sound_runtime_;
  sound_interface.ResetZoneAndScriptMusicRuntime();
  active_camera_sound_handle_ = 0;
  if (cam->sound_id != 0) {
    openwow::audio::SoundKitPlaybackOptions options;
    options.sound_type = 6;
    const int result = sound_interface.PlaySoundKit(
        cam->sound_id, nullptr, &active_camera_sound_handle_, options);
    if (result != 0) {
      active_camera_sound_handle_ = 0;
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "CinematicPlayer: camera sound kit " + std::to_string(cam->sound_id) +
              " failed result=" + std::to_string(result));
    } else {
      std::string path;
      if (const auto handle = sound_interface.GetSoundHandle(active_camera_sound_handle_)) {
        path = handle->selected_file_path;
      }
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kInfo,
          "CinematicPlayer: camera sound kit " + std::to_string(cam->sound_id) +
              " queued handle=" + std::to_string(active_camera_sound_handle_) +
              (path.empty() ? std::string{} : " path=" + path));
    }
  }
  sound_interface.SetCinematicSoundHandle(active_camera_sound_handle_);

  HandleCurrentCameraLoaded(session, 0);
  return true;
}

void CinematicPlayer::HandleCurrentCameraLoaded(WorldSession& session, int error) {
  if (!sequence_runtime_active_ || error != 0) {
    return;
  }

  float delay_seconds = kCinematicStepDelaySeconds;
  if (active_camera_model_ != nullptr
      && active_camera_model_->animation_duration_seconds > 0.0f) {
    delay_seconds += active_camera_model_->animation_duration_seconds;
  }
  duration_ = delay_seconds;

  ScheduleSequenceEvent(
      delay_seconds,
      [this, &session]() { AdvanceToNextSequenceCamera(session); },
      "CinematicPlayer_AdvanceToNextSequenceCamera");
}

void CinematicPlayer::FinishSequence(WorldSession& session,
                                     bool send_complete_packet) {
  const bool was_active = sequence_runtime_active_ || pending_sequence_callback_;
  CancelPendingSequenceEvent();

  state_ = CinematicState::Finished;
  elapsed_ = 0.0f;
  duration_ = 0.0f;
  legacy_orbit_ = false;
  active_seq_.reset();
  StopTrackedSoundHandle(active_sequence_sound_handle_,
                         false);
  StopTrackedSoundHandle(active_camera_sound_handle_,
                         true);
  ResetSequenceRuntimeState();

  if (send_complete_packet) {
    SendCompleteCinematic(session);
  }
  if (was_active) {
    openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
        openwow::ui::game::events::CINEMATIC_STOP);
    if (stop_callback_) {
      stop_callback_();
    }
  }
  ReleaseCinematicInputState();
}

void CinematicPlayer::StopTrackedSoundHandle(std::uint32_t& handle,
                                             const bool clear_cinematic_handle) {
  if (handle == 0) {
    if (clear_cinematic_handle) {
      sound_runtime_.SetCinematicSoundHandle(0);
    }
    return;
  }

  auto& sound_interface = sound_runtime_;
  (void)sound_interface.StopActiveSoundHandle(handle, true, -1.0f, true);
  handle = 0;

  if (clear_cinematic_handle) {
    sound_interface.SetCinematicSoundHandle(0);
  }
}

void CinematicPlayer::ResetSequenceCameraModel() {
  active_camera_model_.reset();
}

bool CinematicPlayer::LoadSequenceCameraModel(const char* model_path,
                                              const float* position,
                                              const float orientation) {
  ResetSequenceCameraModel();
  if (model_path == nullptr || position == nullptr || !model_file_loader_) {
    return false;
  }

  auto& m2_system = m2_system_;
  m2_system.SetFileLoader(model_file_loader_);
  const auto load_result = m2_system.LoadModelForSampling(model_path);
  if (load_result.status != render::m2::M2ResultStatus::kReady || load_result.model_id == 0) {
    return false;
  }

  const std::uint32_t model_id = load_result.model_id;
  const auto model_info = m2_system.QueryModelInfo(model_id);
  if (model_info.status != render::m2::M2ResultStatus::kReady ||
      model_info.info.camera_count == 0) {
    return false;
  }

  auto camera_model = std::make_unique<SequenceCameraModel>();
  camera_model->normalized_path = model_info.info.model_path;
  camera_model->model_id = model_id;
  float packed_scene_transform[12];
  BuildPackedYawTranslationAffine4x3(position, orientation,
                                     packed_scene_transform);
  openwow::math::row_major_mat4x4::BuildRowMajorAffine4x4FromPackedAffine4x3(
      camera_model->scene_transform, packed_scene_transform);
  const auto duration = m2_system.QueryFirstAnimationDuration(model_id);
  camera_model->animation_duration_seconds =
      duration.status == render::m2::M2ResultStatus::kReady
          ? static_cast<float>(duration.duration_ms) / 1000.0f
          : 0.0f;

  active_camera_model_ = std::move(camera_model);
  return true;
}

std::optional<CinematicCameraState> CinematicPlayer::SampleSequenceCamera() const {
  if (active_camera_model_ == nullptr || active_camera_model_->model_id == 0) {
    return std::nullopt;
  }

  const auto* sequence_model = active_camera_model_.get();

  std::uint32_t sample_time_ms = static_cast<std::uint32_t>(
      std::max(0.0f, elapsed_) * 1000.0f);
  const std::uint32_t duration_ms = static_cast<std::uint32_t>(
      std::max(0.0f, sequence_model->animation_duration_seconds) * 1000.0f);
  if (duration_ms > 0 && sample_time_ms >= duration_ms) {
    sample_time_ms = duration_ms - 1;
  }

  const auto pose = m2_system_.QueryCameraSample(
      sequence_model->model_id, 0, 0, sample_time_ms);
  if (pose.status != render::m2::M2ResultStatus::kReady) {
    return std::nullopt;
  }

  float local_position[3] = {
      pose.pose.position[0],
      pose.pose.position[1],
      pose.pose.position[2],
  };
  float local_target[3] = {
      pose.pose.target[0],
      pose.pose.target[1],
      pose.pose.target[2],
  };
  float world_position[3]{};
  float world_target[3]{};
  openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
      world_position, local_position, sequence_model->scene_transform);
  openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
      world_target, local_target, sequence_model->scene_transform);

  CinematicCameraState camera_state;
  camera_state.x = world_position[0];
  camera_state.y = world_position[1];
  camera_state.z = world_position[2];
  camera_state.tx = world_target[0];
  camera_state.ty = world_target[1];
  camera_state.tz = world_target[2];
  camera_state.fov = pose.pose.fov_rad;
  camera_state.roll = pose.pose.roll_rad;
  return camera_state;
}

void CinematicPlayer::ResetSequenceRuntimeState() {
  sequence_runtime_active_ = false;
  pending_sequence_callback_ = false;
  sequence_stop_pending_ = false;
  active_sequence_id_ = 0;
  active_sequence_sound_id_ = 0;
  active_sequence_sound_handle_ = 0;
  active_camera_ids_.fill(0);
  active_camera_index_ = 0;
  active_camera_sound_handle_ = 0;
  pending_sequence_event_id_ = 0;
  ResetSequenceCameraModel();
}

void CinematicPlayer::SendNextCinematicCamera() {
  if (send_fn_) {
    send_fn_(CMSG_NEXT_CINEMATIC_CAMERA, nullptr, 0);
  }
}

void CinematicPlayer::SendCompleteCinematic(WorldSession& session) {
  if (send_fn_) {
    send_fn_(CMSG_COMPLETE_CINEMATIC, nullptr, 0);
  }
  TrySyncLoadedChatChannels(session);
}

}

namespace openwow::world {

int CinematicScene_LoadModel(void* scene, const char* model_path,
                             float* position, float orientation,
                             int , int , int ) {
  auto* player = static_cast<openwow::game::CinematicPlayer*>(scene);
  if (player == nullptr || model_path == nullptr || position == nullptr) {
    return 0;
  }

  return player->LoadSequenceCameraModel(model_path, position, orientation)
             ? 1
             : 0;
}

}
