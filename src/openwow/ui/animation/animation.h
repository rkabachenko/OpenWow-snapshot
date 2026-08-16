
#pragma once

#include "openwow/ui/animation/animation_script_host.h"
#include "openwow/ui/animation/animation_types.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

namespace openwow::ui::anim {

class AnimationGroup;

class Animation {
 public:
  Animation();
  virtual ~Animation();

  virtual AnimKind GetKind() const { return AnimKind::Animation; }

  [[nodiscard]] bool IsKindOf(AnimKind kind) const {
    return kind == AnimKind::Animation || GetKind() == kind;
  }
  [[nodiscard]] const char* GetObjectTypeName() const;
  [[nodiscard]] bool IsObjectType(const char* type_name) const;

  void SetDuration(float seconds, bool notify_parent = true);
  float GetDuration() const             { return duration_; }

  void SetStartDelay(float seconds, bool notify_parent = true);
  float GetStartDelay() const           { return start_delay_; }

  void SetEndDelay(float seconds, bool notify_parent = true);
  float GetEndDelay() const             { return end_delay_; }

  void SetOrder(int order, bool notify_parent = true, bool validate = false);
  int GetOrder() const                  { return order_; }

  void SetSmoothing(AnimCurveType t);
  AnimCurveType GetSmoothing() const;

  void SetName(const std::string& n)    { name_ = n; }
  const std::string& GetName() const    { return name_; }
  void MarkLoadedFromXml()              { loaded_from_xml_ = true; }
  bool WasLoadedFromXml() const         { return loaded_from_xml_; }

  void Play();
  void Pause();
  void Stop(bool requested = false);

  bool IsPlaying() const  { return state_ == AnimState::Playing; }
  bool IsPaused() const   { return state_ == AnimState::Paused; }
  bool IsStopped() const  { return state_ == AnimState::Stopped; }
  bool IsDone() const     { return done_ || GetProgressWithDelay() >= 1.0f; }
  void SetDone(bool d)    { done_ = d; }
  bool IsDelaying() const;

  AnimState GetState() const { return state_; }

  AnimationGroup* GetGroup() const { return group_; }
  AnimationGroup* SetGroup(AnimationGroup* g) {
    group_ = g;
    return g;
  }

  float GetProgress() const;
  float GetSmoothProgress() const;
  float GetProgressWithDelay() const;
  float GetElapsed() const { return elapsed_; }

  void SetSmoothProgress(float p) { smooth_progress_ = p; }
  void SetMaxFramerate(float fps);
  float GetMaxFramerate() const { return max_framerate_; }

  bool GetSmoothWeights(float& smooth_in, float& smooth_out) const;
  void SetSmoothControlPoint(float smooth_in, float smooth_out);

  float GetTotalDuration() const { return start_delay_ + duration_ + end_delay_; }

  virtual void Update(float dt);
  bool AdvancePlayback(float dt, float* consumed_time = nullptr);

  void ApplyNegativeFactor(float factor);

  static float ApplySmoothing(float t, AnimCurveType type);

  void SetScriptRef(const std::string& handler, lua_State* L, int lua_ref);

  void SetScriptRef(const std::string& handler, lua_State* L, int lua_ref,
                    int taint_source);
  int  GetScriptRef(const std::string& handler) const;

  [[nodiscard]] int ScriptTaintSource(const std::string& handler) const;
  bool HasScript(const std::string& handler) const;
  void SetLuaObjectRef(lua_State* L, int lua_ref);
  void FireScript(const std::string& handler, lua_State* L);
  void FireScriptWithNumber(const std::string& handler, lua_State* L, float value);
  void FireScriptWithBoolean(const std::string& handler, lua_State* L, bool value);
  virtual void FinalizeXmlLoad();

  virtual void Apply(float ) {}

  virtual void ResetEffect() {}
  virtual void ApplySignedFactor(float factor);

 protected:
  std::string name_;
  float duration_    = 0.0f;
  float start_delay_ = 0.0f;
  float end_delay_   = 0.0f;
  float max_framerate_ = 0.0f;
  float inv_max_framerate_ = 0.0f;
  float elapsed_     = 0.0f;
  float frame_accumulator_ = 0.0f;
  float progress_with_delay_ = 0.0f;
  float active_progress_ = 0.0f;
  float smooth_progress_ = 0.0f;
  int   order_       = -1;
  AnimState state_ = AnimState::Stopped;
  uint8_t direction_ = 0;

  bool done_ = false;
  bool loaded_from_xml_ = false;

  struct SmoothControlPoint {
    float smooth_in{0.0f};
    float smooth_out{0.0f};
  };
  std::unique_ptr<SmoothControlPoint> smooth_cp_;

  AnimationGroup* group_{nullptr};

  AnimationScriptHost script_host_;
  std::uint64_t order_bucket_sequence_{0};

 private:
  friend class AnimationGroup;

  void BeginPlayFromGroup();
  void PauseFromGroup();
  void PrepareForPlayback(uint8_t direction);
  float AdvanceTimeline(float dt);
  float TransformActiveProgress(float progress) const;
  static float ApplySmoothing(float t, float smooth_in, float smooth_out);
};

}
