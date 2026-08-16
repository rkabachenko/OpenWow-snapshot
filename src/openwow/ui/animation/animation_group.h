
#pragma once

#include "openwow/ui/animation/animation.h"
#include "openwow/ui/animation/animation_script_host.h"
#include "openwow/ui/animation/animation_types.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui::widgets {
class CScriptRegion;
}

namespace openwow::ui::anim {

class AlphaAnim;
class ScaleAnim;
class TranslationAnim;
class RotationAnim;
class PathAnim;

class AnimationGroup {
 public:
  AnimationGroup();
  ~AnimationGroup();

  void SetName(const std::string& n) { name_ = n; }
  const std::string& GetName() const { return name_; }

  Animation* CreateAnimation(const std::string& anim_type, const std::string& name = "");
  Animation* CreateBasicAnimation(const std::string& name = "");
  AlphaAnim*       CreateAlpha(const std::string& name = "");
  ScaleAnim*       CreateScale(const std::string& name = "");
  TranslationAnim* CreateTranslation(const std::string& name = "");
  RotationAnim*    CreateRotation(const std::string& name = "");
  PathAnim*        CreatePath(const std::string& name = "");

  bool Play();
  void Pause();
  void Stop(bool requested = false);
  void Finish();

  bool IsPlaying() const { return state_ == AnimState::Playing; }
  bool IsPaused() const  { return state_ == AnimState::Paused; }
  bool IsDone() const;

  float GetProgress() const;
  float GetDuration() const;

  void SetLooping(AnimLoopType t) { loop_type_ = t; }
  AnimLoopType GetLooping() const { return loop_type_; }

  AnimLoopState GetLoopState() const { return loop_state_; }

  bool IsPendingFinish() const { return pending_finish_; }
  void SetPendingFinish(bool v) { pending_finish_ = v; }

  void SetInitialOffsetStored(float x, float y);
  void GetInitialOffsetStored(float& x, float& y) const;
  void SetInitialOffsetPixels(float x, float y);
  void GetInitialOffsetPixels(float& x, float& y) const;

  void SetParentFrame(void* frame) { parent_frame_ = frame; }
  void* GetParentFrame() const { return parent_frame_; }

  void SetOwnerRegion(openwow::ui::widgets::CScriptRegion* region);
  [[nodiscard]] openwow::ui::widgets::CScriptRegion* GetOwnerRegion() const noexcept {
    return ownerRegion_;
  }
  bool ApplyParentFrameAlphaStep(std::int16_t step,
                                 std::uint8_t* out_alpha_byte = nullptr);
  void Update(float dt);
  void FinalizeXmlLoad();

  size_t GetNumAnimations() const { return animations_.size(); }
  Animation* GetAnimation(size_t index);
  Animation* GetAnimationByName(const std::string& name);
  const std::vector<std::unique_ptr<Animation>>& GetAnimations() const { return animations_; }
  [[nodiscard]] bool ContainsAnimationKind(AnimKind kind) const;

  bool DestroyAnimation(Animation& animation);
  bool ReparentAnimation(Animation& animation, AnimationGroup* new_parent, int old_order);

  int GetMaxOrder() const;

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
  void FireScriptWithString(const std::string& handler, lua_State* L,
                            const char* value);

 private:
  friend class Animation;

  struct OrderBucket {
    int order = 0;
    std::vector<Animation*> animations;
  };

  int MaxOrder() const;

  bool AdvanceOrder(float raw_dt, bool* group_on_update_fired);
  void BeginOrderPlayback(int order_slot_index);
  void ResetChildren(AnimLoopState loop_state);
  void ResetPlaybackState();
  bool HandleChildStop(Animation& animation);
  void OnAnimationDestroyed(const Animation& animation);
  void OnAnimationOrderChanged(Animation& animation, int old_order);
  std::unique_ptr<Animation> ExtractOwnedAnimation(Animation& animation);
  void AdoptAnimation(std::unique_ptr<Animation> animation);
  void InvalidateOrderBuckets();
  void TouchOrderBucketMembership(Animation& animation);
  bool WouldRemovalEmptyOrderBucket(const Animation& animation, int removal_order) const;
  void RebuildOrderBuckets() const;
  const OrderBucket* GetOrderBucket(int order_slot_index) const;
  const std::vector<OrderBucket>& GetOrderBuckets() const;
  void RegisterNamedAnimation(Animation* animation);
  void UnregisterNamedAnimation(const Animation& animation);
  void StopInternal(bool requested, const Animation* excluded_animation = nullptr);

  std::string name_;
  std::vector<std::unique_ptr<Animation>> animations_;
  std::unordered_map<std::string, Animation*> named_anims_;

  AnimLoopType loop_type_ = AnimLoopType::None;
  AnimLoopState loop_state_ = AnimLoopState::None;
  AnimState state_ = AnimState::Stopped;
  bool done_ = false;
  bool pending_finish_ = false;
  bool stop_in_progress_ = false;
  float elapsed_ = 0.0f;
  float completion_ratio_ = 0.0f;
  int current_order_slot_index_ = -1;
  float initial_offset_x_ = 0.0f;
  float initial_offset_y_ = 0.0f;
  void* parent_frame_{nullptr};

  openwow::ui::widgets::CScriptRegion* ownerRegion_{nullptr};
  mutable std::vector<OrderBucket> order_buckets_;
  mutable bool order_buckets_dirty_ = true;
  std::uint64_t next_order_bucket_sequence_ = 1;

  AnimationScriptHost script_host_;
};

}
