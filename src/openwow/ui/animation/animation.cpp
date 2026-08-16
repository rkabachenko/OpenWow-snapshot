
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/animation/animation.h"
#include "openwow/ui/animation/animation_group.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/foundation/text/ascii.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <cmath>

namespace openwow::ui::anim {

namespace {
constexpr float kAnimationDurationEpsilon = 0.00000023841858f;
constexpr float kSmoothingWeightThreshold = 0.001f;

bool HasSmoothingComponent(const float value) {
  return std::fabs(value) >= kSmoothingWeightThreshold;
}

AnimCurveType CurveTypeFromSmoothingWeights(const float smooth_in,
                                           const float smooth_out) {
  const bool has_smooth_in = HasSmoothingComponent(smooth_in);
  const bool has_smooth_out = HasSmoothingComponent(smooth_out);
  if (has_smooth_in) {
    return has_smooth_out ? AnimCurveType::InOut : AnimCurveType::In;
  }
  return has_smooth_out ? AnimCurveType::Out : AnimCurveType::None;
}

}

Animation::Animation() = default;
Animation::~Animation() {
  if (group_ != nullptr) {
    group_->OnAnimationDestroyed(*this);
    group_ = nullptr;
  }
}

const char* Animation::GetObjectTypeName() const {
  switch (GetKind()) {
  case AnimKind::Alpha:
    return "Alpha";
  case AnimKind::Scale:
    return "Scale";
  case AnimKind::Translation:
    return "Translation";
  case AnimKind::Rotation:
    return "Rotation";
  case AnimKind::Path:
    return "Path";
  case AnimKind::Animation:
  default:
    return "Animation";
  }
}

bool Animation::IsObjectType(const char* type_name) const {
  if (type_name == nullptr) {
    return false;
  }

  return openwow::text::EqualsIgnoreCaseAscii(type_name, GetObjectTypeName()) ||
         openwow::text::EqualsIgnoreCaseAscii(type_name, "Animation") ||
         openwow::text::EqualsIgnoreCaseAscii(type_name, "Object");
}

void Animation::Play() {
  if (group_ != nullptr && !group_->Play()) {
    return;
  }

  BeginPlayFromGroup();
}

void Animation::BeginPlayFromGroup() {
  if (state_ == AnimState::Playing) {
    return;
  }

  if (direction_ == 0) {
    direction_ = 1;
  }

  state_ = AnimState::Playing;
  done_ = false;
  FireScript("OnPlay", nullptr);
}

void Animation::Pause() {
  if (group_ != nullptr) {
    group_->Pause();
  }
  PauseFromGroup();
}

void Animation::Stop(const bool requested) {
  if (state_ == AnimState::Stopped) return;
  elapsed_ = 0.0f;
  progress_with_delay_ = 0.0f;
  active_progress_ = 0.0f;
  direction_ = 0;
  state_ = AnimState::Stopped;
  done_ = false;
  FireScriptWithBoolean("OnStop", nullptr, requested);
  if (group_ != nullptr) {
    group_->HandleChildStop(*this);
  }
}

void Animation::SetMaxFramerate(float fps) {
  if (fps > 0.000099999997f) {
    max_framerate_ = fps;
    inv_max_framerate_ = 1.0f / fps;
  } else {
    max_framerate_ = 0.0f;
    inv_max_framerate_ = 0.0f;
  }
}

bool Animation::GetSmoothWeights(float& smooth_in, float& smooth_out) const {
  if (smooth_cp_) {
    smooth_in = smooth_cp_->smooth_in;
    smooth_out = smooth_cp_->smooth_out;
    return true;
  }
  smooth_in = 0.0f;
  smooth_out = 0.0f;
  return false;
}

void Animation::SetSmoothControlPoint(float smooth_in, float smooth_out) {
  bool in_valid = (smooth_in > 0.0f && smooth_in <= 1.0f);
  bool out_valid = (smooth_out > 0.0f && smooth_out <= 1.0f);
  if (!in_valid && !out_valid) return;

  if (!smooth_cp_) {
    smooth_cp_ = std::make_unique<SmoothControlPoint>();
  }
  smooth_cp_->smooth_in = std::clamp(smooth_in, 0.0f, 1.0f);
  smooth_cp_->smooth_out = std::clamp(smooth_out, 0.0f, 1.0f);
}

void Animation::SetSmoothing(const AnimCurveType type) {
  switch (type) {
  case AnimCurveType::In:
    SetSmoothControlPoint(1.0f, 0.0f);
    return;
  case AnimCurveType::Out:
    SetSmoothControlPoint(0.0f, 1.0f);
    return;
  case AnimCurveType::InOut:
  case AnimCurveType::OutIn:
    SetSmoothControlPoint(1.0f, 1.0f);
    return;
  case AnimCurveType::None:
  default:
    smooth_cp_.reset();
    return;
  }
}

AnimCurveType Animation::GetSmoothing() const {
  float smooth_in = 0.0f;
  float smooth_out = 0.0f;
  GetSmoothWeights(smooth_in, smooth_out);
  return CurveTypeFromSmoothingWeights(smooth_in, smooth_out);
}

void Animation::SetStartDelay(float seconds, bool ) {
  start_delay_ = seconds;
  if (start_delay_ < 0.0f) start_delay_ = 0.0f;
}

void Animation::SetEndDelay(float seconds, bool ) {
  end_delay_ = seconds;
  if (end_delay_ < 0.0f) end_delay_ = 0.0f;
}

void Animation::SetDuration(float seconds, bool ) {
  duration_ = seconds;
}

void Animation::SetOrder(int order, bool notify_parent, bool validate) {
  const int old_order = order_;
  int clamped;
  if (validate) {
    clamped = (order >= 0 && order <= 99) ? order : 0;
  } else {
    clamped = order;
    if (clamped < 0) clamped = 0;
    if (clamped > 99) clamped = 99;
  }
  order_ = clamped;
  if (group_ != nullptr && notify_parent && old_order != order) {
    group_->OnAnimationOrderChanged(*this, old_order);
  }
}

float Animation::GetProgress() const {
  return active_progress_;
}

float Animation::GetSmoothProgress() const {
  return smooth_progress_;
}

float Animation::GetProgressWithDelay() const {
  return progress_with_delay_;
}

bool Animation::IsDelaying() const {
  const float pwd = GetProgressWithDelay();
  if (pwd <= 0.0f || pwd >= 1.0f) return false;

  const float total = end_delay_ + duration_ + start_delay_;
  if (std::fabs(total) < kAnimationDurationEpsilon) return false;

  if (direction_ == 2) {

    if (end_delay_ > elapsed_) return true;

    float after_active = end_delay_ + duration_;
    return (after_active <= elapsed_ && elapsed_ < total);
  }

  if (start_delay_ > elapsed_) return true;
  float after_active = start_delay_ + duration_;
  return (after_active <= elapsed_ && elapsed_ < total);
}

void Animation::Update(float dt) {
  if (AdvancePlayback(dt) && state_ != AnimState::Stopped) {
    state_ = AnimState::Stopped;
  }
}

void Animation::ApplyNegativeFactor(const float factor) {
  ApplySignedFactor(-factor);
}

bool Animation::AdvancePlayback(float dt, float* consumed_time) {
  if (consumed_time != nullptr) {
    *consumed_time = 0.0f;
  }

  if (GetProgressWithDelay() >= 1.0f) {
    done_ = true;
    Apply(smooth_progress_);
    return true;
  }

  if (state_ == AnimState::Stopped) {
    return true;
  }

  if (state_ == AnimState::Paused) {
    Apply(smooth_progress_);
    return false;
  }

  const float consumed = AdvanceTimeline(dt);
  if (consumed_time != nullptr) {
    *consumed_time = consumed;
  }

  FireScriptWithNumber("OnUpdate", nullptr, dt);
  Apply(smooth_progress_);

  if (GetProgressWithDelay() < 1.0f) {
    return false;
  }

  done_ = true;
  FireScriptWithBoolean("OnFinished", nullptr, false);
  return true;
}

float Animation::ApplySmoothing(float t, AnimCurveType type) {
  switch (type) {
  case AnimCurveType::In:
    return ApplySmoothing(t, 1.0f, 0.0f);
  case AnimCurveType::Out:
    return ApplySmoothing(t, 0.0f, 1.0f);
  case AnimCurveType::InOut:
  case AnimCurveType::OutIn:
    return ApplySmoothing(t, 1.0f, 1.0f);
  case AnimCurveType::None:
  default:
    return ApplySmoothing(t, 0.0f, 0.0f);
  }
}

void Animation::SetScriptRef(const std::string& handler, lua_State* L,
                             int lua_ref) {
  SetScriptRef(handler, L, lua_ref,
               openwow::ui::game::SecureExecution::Get().CurrentTaint(L));
}

void Animation::SetScriptRef(const std::string& handler, lua_State* L,
                             int lua_ref, const int taint_source) {
  script_host_.SetHandler(handler, L, lua_ref, taint_source);
}

int Animation::GetScriptRef(const std::string& handler) const {
  return script_host_.GetHandlerRef(handler);
}

int Animation::ScriptTaintSource(const std::string& handler) const {
  return script_host_.HandlerTaintSource(handler);
}

bool Animation::HasScript(const std::string& handler) const {
  return script_host_.HasHandler(handler);
}

void Animation::SetLuaObjectRef(lua_State* L, int lua_ref) {
  script_host_.SetObjectRef(L, lua_ref);
}

void Animation::FireScript(const std::string& handler, lua_State* L) {
  script_host_.Fire(handler, L);
}

void Animation::FireScriptWithNumber(const std::string& handler, lua_State* L,
                                     const float value) {
  script_host_.Fire(handler, L, value);
}

void Animation::FireScriptWithBoolean(const std::string& handler, lua_State* L,
                                      const bool value) {
  script_host_.Fire(handler, L, value);
}

void Animation::FinalizeXmlLoad() {
  FireScript("OnLoad", nullptr);
}

void Animation::PauseFromGroup() {
  if (state_ != AnimState::Playing) {
    return;
  }

  state_ = AnimState::Paused;
  FireScript("OnPause", nullptr);
}

void Animation::PrepareForPlayback(const uint8_t direction) {
  elapsed_ = 0.0f;
  progress_with_delay_ = 0.0f;
  active_progress_ = 0.0f;
  state_ = AnimState::Stopped;
  direction_ = direction;
  done_ = false;
}

float Animation::AdvanceTimeline(const float dt) {
  const float previous_elapsed = elapsed_;
  elapsed_ += dt;

  const float total_duration = start_delay_ + end_delay_ + duration_;
  if (elapsed_ >= total_duration) {
    elapsed_ = total_duration;
    progress_with_delay_ = 1.0f;
    active_progress_ = 1.0f;
    smooth_progress_ = TransformActiveProgress(active_progress_);
    return elapsed_ - previous_elapsed;
  }

  progress_with_delay_ = std::clamp(elapsed_ / total_duration, 0.0f, 1.0f);

  if (std::fabs(duration_) < kAnimationDurationEpsilon) {
    active_progress_ = 1.0f;
  } else {
    frame_accumulator_ += dt;
    if (frame_accumulator_ >= inv_max_framerate_) {
      const float delay = (direction_ == 2) ? end_delay_ : start_delay_;
      active_progress_ = std::clamp((elapsed_ - delay) / duration_, 0.0f, 1.0f);
      frame_accumulator_ = 0.0f;
    }
  }

  smooth_progress_ = TransformActiveProgress(active_progress_);
  return elapsed_ - previous_elapsed;
}

float Animation::TransformActiveProgress(const float progress) const {
  const float directional_progress = (direction_ == 2) ? (1.0f - progress) : progress;
  float smooth_in = 0.0f;
  float smooth_out = 0.0f;
  GetSmoothWeights(smooth_in, smooth_out);
  return ApplySmoothing(directional_progress, smooth_in, smooth_out);
}

void Animation::ApplySignedFactor(const float factor) {
  Apply(factor);
}

float Animation::ApplySmoothing(float t, const float smooth_in, const float smooth_out) {
  t = std::clamp(t, 0.0f, 1.0f);
  constexpr float kPi = 3.1415927f;
  constexpr float kHalfPi = 1.5707964f;

  const bool has_smooth_in = HasSmoothingComponent(smooth_in);
  const bool has_smooth_out = HasSmoothingComponent(smooth_out);
  if (has_smooth_in) {
    if (!has_smooth_out) {
      return 1.0f - std::cos(t * kHalfPi);
    }
    return 0.5f - std::cos(t * kPi) * 0.5f;
  }

  if (!has_smooth_out) {
    return t;
  }

  return std::cos((t + 1.0f) * kHalfPi) * -1.0f;
}

}
