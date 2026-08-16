#include "openwow/ui/lua_c_api_convenience.h"

#include "openwow/ui/animation/animation_group.h"
#include "openwow/ui/animation/alpha_anim.h"
#include "openwow/ui/animation/animation_coordinate_space.h"
#include "openwow/ui/animation/animation_xml.h"
#include "openwow/ui/animation/path_anim.h"
#include "openwow/ui/animation/rotation_anim.h"
#include "openwow/ui/animation/scale_anim.h"
#include "openwow/ui/animation/translation_anim.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/ui/game/framescript/core/frame_alpha.h"
#include "openwow/ui/game/secure_execution.h"

#include "openwow/ui/game/runtime/lua_interned_field_key.h"
#include "openwow/ui/game/runtime/texture_render_state_source.h"
#include "openwow/ui/widgets/script_region.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openwow::ui::anim {

namespace {
std::string CanonicalGroupScriptHandlerName(const std::string& handler);
}

AnimationGroup::AnimationGroup() = default;
AnimationGroup::~AnimationGroup() {

  if (ownerRegion_ != nullptr) {
    ownerRegion_->UnregisterOwnedAnimationGroup(this);
    ownerRegion_ = nullptr;
  }

  animations_.clear();
}

void AnimationGroup::SetOwnerRegion(openwow::ui::widgets::CScriptRegion* region) {
  if (ownerRegion_ == region) {
    return;
  }

  if (ownerRegion_ != nullptr) {
    ownerRegion_->UnregisterOwnedAnimationGroup(this);
  }

  ownerRegion_ = region;

  if (ownerRegion_ != nullptr) {
    ownerRegion_->RegisterOwnedAnimationGroup(this);
  }
}

namespace {

constexpr float kAnimGroupDoneEpsilon = 0.000099999997f;
constexpr int kMaxParentAlphaDepth = 64;

std::uint8_t GetLuaFrameAlphaByteOrDefault(lua_State* state,
                                           const int index,
                                           const std::uint8_t fallback = 0xFF) {
  const int abs_index = lua_absindex(state, index);
  openwow::ui::game::runtime::GetInternedLuaField(state, abs_index, "__ow_alpha");
  double alpha = openwow::ui::game::NormalizeFrameAlphaByte(fallback);
  if (lua_isnumber(state, -1) != 0) {
    alpha = lua_tonumber(state, -1);
  }
  lua_pop(state, 1);
  return openwow::ui::game::QuantizeFrameAlphaByteTruncated(alpha);
}

std::uint8_t ComputeParentInheritedAlpha(lua_State* state, const int frame_index) {
  std::uint8_t inherited_alpha = 0xFF;
  openwow::ui::game::runtime::GetInternedLuaField(state, lua_absindex(state, frame_index), "__ow_parent");
  for (int depth = 0; depth < kMaxParentAlphaDepth && lua_istable(state, -1) != 0; ++depth) {
    inherited_alpha = openwow::ui::game::MultiplyFrameAlphaBytes(
        inherited_alpha, GetLuaFrameAlphaByteOrDefault(state, -1));
    openwow::ui::game::runtime::GetInternedLuaField(state, -1, "__ow_parent");
    lua_remove(state, -2);
  }
  lua_pop(state, 1);
  return inherited_alpha;
}

uint8_t PlaybackDirectionForLoopState(const AnimLoopState state) {
  return state == AnimLoopState::Reverse ? 2 : 1;
}

float ClampAdvanceDelta(const float dt) {
  return std::max(dt, 0.0f);
}

const char* LoopStateToString(const AnimLoopState state) {
  switch (state) {
    case AnimLoopState::Forward:
      return "FORWARD";
    case AnimLoopState::Reverse:
      return "REVERSE";
    case AnimLoopState::None:
    default:
      return "NONE";
  }
}

template <typename AnimationType>
AnimationType* AppendAnimation(std::vector<std::unique_ptr<Animation>>& animations,
                               std::unordered_map<std::string, Animation*>& named_anims,
                               AnimationGroup& group,
                               const std::string& name) {
  auto anim = std::make_unique<AnimationType>();
  anim->SetGroup(&group);
  anim->SetName(name);
  auto* ptr = anim.get();
  if (!name.empty()) {
    named_anims[name] = ptr;
  }
  animations.push_back(std::move(anim));
  return ptr;
}

std::string CanonicalGroupScriptHandlerName(const std::string& handler) {
  if (const char* canonical = NormalizeAnimGroupScriptHandler(handler.c_str());
      canonical != nullptr) {
    return canonical;
  }

  return {};
}

}

static bool IcaseEqual(const char* a, const char* b) {
  while (*a && *b) {
    if (std::tolower(static_cast<unsigned char>(*a)) !=
        std::tolower(static_cast<unsigned char>(*b)))
      return false;
    ++a;
    ++b;
  }
  return *a == *b;
}

Animation* AnimationGroup::CreateAnimation(const std::string& anim_type,
                                           const std::string& name) {
  const char* t = anim_type.c_str();
  if (IcaseEqual(t, "Animation"))   return CreateBasicAnimation(name);
  if (IcaseEqual(t, "Alpha"))       return CreateAlpha(name);
  if (IcaseEqual(t, "Scale"))       return CreateScale(name);
  if (IcaseEqual(t, "Translation")) return CreateTranslation(name);
  if (IcaseEqual(t, "Rotation"))    return CreateRotation(name);
  if (IcaseEqual(t, "Path"))        return CreatePath(name);
  return CreateBasicAnimation(name);
}

Animation* AnimationGroup::CreateBasicAnimation(const std::string& name) {
  auto* animation = AppendAnimation<Animation>(animations_, named_anims_, *this, name);
  TouchOrderBucketMembership(*animation);
  InvalidateOrderBuckets();
  return animation;
}

AlphaAnim* AnimationGroup::CreateAlpha(const std::string& name) {
  auto* animation = AppendAnimation<AlphaAnim>(animations_, named_anims_, *this, name);
  TouchOrderBucketMembership(*animation);
  InvalidateOrderBuckets();
  return animation;
}

ScaleAnim* AnimationGroup::CreateScale(const std::string& name) {
  auto* animation = AppendAnimation<ScaleAnim>(animations_, named_anims_, *this, name);
  TouchOrderBucketMembership(*animation);
  InvalidateOrderBuckets();
  return animation;
}

TranslationAnim* AnimationGroup::CreateTranslation(const std::string& name) {
  auto* animation =
      AppendAnimation<TranslationAnim>(animations_, named_anims_, *this, name);
  TouchOrderBucketMembership(*animation);
  InvalidateOrderBuckets();
  return animation;
}

RotationAnim* AnimationGroup::CreateRotation(const std::string& name) {
  auto* animation = AppendAnimation<RotationAnim>(animations_, named_anims_, *this, name);
  TouchOrderBucketMembership(*animation);
  InvalidateOrderBuckets();
  return animation;
}

PathAnim* AnimationGroup::CreatePath(const std::string& name) {
  auto* animation = AppendAnimation<PathAnim>(animations_, named_anims_, *this, name);
  TouchOrderBucketMembership(*animation);
  InvalidateOrderBuckets();
  return animation;
}

void AnimationGroup::SetInitialOffsetStored(const float x, const float y) {
  if (y * y + x * x <= kAnimationStoredOffsetEpsilon) {
    initial_offset_x_ = 0.0f;
    initial_offset_y_ = 0.0f;
    return;
  }

  initial_offset_x_ = x;
  initial_offset_y_ = y;
}

void AnimationGroup::GetInitialOffsetStored(float& x, float& y) const {
  x = initial_offset_x_;
  y = initial_offset_y_;
}

void AnimationGroup::SetInitialOffsetPixels(const float x, const float y) {
  const float aspect_ratio = ResolveAnimationCoordinateAspectRatio();
  SetInitialOffsetStored(PixelAnimationOffsetToStored(x, aspect_ratio),
                         PixelAnimationOffsetToStored(y, aspect_ratio));
}

void AnimationGroup::GetInitialOffsetPixels(float& x, float& y) const {
  const float aspect_ratio = ResolveAnimationCoordinateAspectRatio();
  x = StoredAnimationOffsetToPixels(initial_offset_x_, aspect_ratio);
  y = StoredAnimationOffsetToPixels(initial_offset_y_, aspect_ratio);
}

bool AnimationGroup::ApplyParentFrameAlphaStep(const std::int16_t step,
                                               std::uint8_t* out_alpha_byte) {
  lua_State* state = script_host_.ObjectLua();
  if (state == nullptr || script_host_.ObjectRef() == LUA_NOREF) {
    return false;
  }

  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, script_host_.ObjectRef());
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }

  openwow::ui::game::runtime::GetInternedLuaField(state, -1, "__ow_parent");
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }

  const int frame_index = lua_absindex(state, -1);
  const std::uint8_t current_alpha = GetLuaFrameAlphaByteOrDefault(state, frame_index);
  const std::uint8_t inherited_alpha = ComputeParentInheritedAlpha(state, frame_index);
  const auto effective = static_cast<std::uint8_t>(
      static_cast<std::uint32_t>(current_alpha) * inherited_alpha / 255u);
  const std::int16_t next_alpha = step + static_cast<std::int16_t>(effective);
  const auto clamped = static_cast<std::uint8_t>(std::clamp<int>(next_alpha, 0, 255));

  if (clamped != current_alpha) {
    lua_pushnumber(state, openwow::ui::game::NormalizeFrameAlphaByte(clamped));
    lua_setfield(state, frame_index, "__ow_alpha");

    openwow::ui::game::runtime::GetInternedLuaField(state, frame_index, "__ow_type");
    const char* region_type = lua_tostring(state, -1);
    const bool is_region =
        region_type != nullptr &&
        (openwow::text::EqualsIgnoreCaseAscii(region_type, "Texture") ||
         openwow::text::EqualsIgnoreCaseAscii(region_type, "FontString"));
    lua_pop(state, 1);
    if (is_region) {
      openwow::ui::game::runtime::SetTextureRenderStateNumber(
          state, frame_index,
          openwow::ui::game::runtime::TextureRenderStateField::kVertexColorA,
          openwow::ui::game::NormalizeFrameAlphaByte(clamped));
    }
  }

  if (out_alpha_byte != nullptr) {
    *out_alpha_byte = clamped;
  }

  lua_settop(state, top);
  return true;
}

bool AnimationGroup::Play() {
  const auto& order_buckets = GetOrderBuckets();
  if (order_buckets.empty()) {
    return false;
  }

  if (loop_state_ != AnimLoopState::None && state_ != AnimState::Paused) {
    return true;
  }

  const int order_bucket_count = static_cast<int>(order_buckets.size());
  if (current_order_slot_index_ < 0) {
    current_order_slot_index_ = 0;
    loop_state_ = AnimLoopState::Forward;
  } else if (current_order_slot_index_ >= order_bucket_count) {
    current_order_slot_index_ = order_bucket_count - 1;
    loop_state_ = AnimLoopState::Reverse;
  } else if (loop_state_ == AnimLoopState::None) {
    loop_state_ = AnimLoopState::Forward;
  }

  done_ = false;
  state_ = AnimState::Playing;
  ResetChildren(loop_state_);
  BeginOrderPlayback(current_order_slot_index_);
  FireScript("OnPlay", nullptr);
  return true;
}

void AnimationGroup::Pause() {
  if (state_ != AnimState::Playing) return;
  state_ = AnimState::Paused;
  for (auto& a : animations_) {
    if (a->IsPlaying()) {
      a->PauseFromGroup();
    }
  }
  FireScript("OnPause", nullptr);
}

void AnimationGroup::Stop(const bool requested) {
  StopInternal(requested);
}

void AnimationGroup::StopInternal(const bool requested,
                                  const Animation* excluded_animation) {
  if (loop_state_ == AnimLoopState::None || stop_in_progress_) {
    return;
  }

  stop_in_progress_ = true;
  if (const OrderBucket* bucket = GetOrderBucket(current_order_slot_index_)) {
    for (Animation* animation : bucket->animations) {
      if (animation != nullptr && animation != excluded_animation) {
        animation->Stop(requested);
      }
    }
  }

  FireScriptWithBoolean("OnStop", nullptr, requested);
  ResetPlaybackState();
}

void AnimationGroup::Finish() {
  if (!IsDone() && loop_state_ != AnimLoopState::None) {
    pending_finish_ = true;
  }
}

bool AnimationGroup::IsDone() const {
  const float duration = GetDuration();
  if (duration <= kAnimGroupDoneEpsilon) {
    return true;
  }

  if (done_) {
    return true;
  }

  if (loop_type_ != AnimLoopType::None || loop_state_ != AnimLoopState::None) {
    return false;
  }

  return completion_ratio_ >= 1.0f;
}

float AnimationGroup::GetDuration() const {
  float total = 0.0f;
  for (const auto& bucket : GetOrderBuckets()) {
    float longest = 0.0f;
    for (const Animation* animation : bucket.animations) {
      if (animation != nullptr) {
        longest = std::max(longest, animation->GetTotalDuration());
      }
    }
    total += longest;
  }
  return total;
}

float AnimationGroup::GetProgress() const {

  return elapsed_ / GetDuration();
}

void AnimationGroup::Update(float dt) {
  if (state_ != AnimState::Playing || IsDone()) {
    return;
  }

  bool group_on_update_fired = false;
  const auto fire_group_on_update = [&]() {
    if (group_on_update_fired) {
      return;
    }

    FireScriptWithNumber("OnUpdate", nullptr, dt);
    group_on_update_fired = true;
  };

  const float clamped_dt = ClampAdvanceDelta(dt);
  elapsed_ += clamped_dt;
  const float duration = GetDuration();
  completion_ratio_ =
      (duration <= kAnimGroupDoneEpsilon)
          ? 1.0f
          : std::clamp(elapsed_ / duration, 0.0f, 1.0f);

  float remaining_dt = clamped_dt;
  while (state_ == AnimState::Playing) {
    const OrderBucket* bucket = GetOrderBucket(current_order_slot_index_);
    if (bucket == nullptr) {
      ResetPlaybackState();
      fire_group_on_update();
      return;
    }

    float max_consumed = 0.0f;
    bool all_done = true;
    auto advance_animation = [&](Animation* animation) {
      if (animation == nullptr) {
        return;
      }

      float consumed = 0.0f;
      const bool finished = animation->AdvancePlayback(remaining_dt, &consumed);
      max_consumed = std::max(max_consumed, consumed);
      if (!finished) {
        all_done = false;
      }
    };

    if (loop_state_ == AnimLoopState::Reverse) {
      for (auto it = bucket->animations.rbegin(); it != bucket->animations.rend(); ++it) {
        advance_animation(*it);
        if (state_ != AnimState::Playing) {
          fire_group_on_update();
          return;
        }
      }
    } else {
      for (Animation* animation : bucket->animations) {
        advance_animation(animation);
        if (state_ != AnimState::Playing) {
          fire_group_on_update();
          return;
        }
      }
    }

    if (!all_done) {
      fire_group_on_update();
      return;
    }

    remaining_dt = std::max(0.0f, remaining_dt - max_consumed);
    if (!AdvanceOrder(dt, &group_on_update_fired) || remaining_dt <= kAnimGroupDoneEpsilon) {
      fire_group_on_update();
      return;
    }
  }

  fire_group_on_update();
}

Animation* AnimationGroup::GetAnimation(size_t index) {
  return (index < animations_.size()) ? animations_[index].get() : nullptr;
}

Animation* AnimationGroup::GetAnimationByName(const std::string& name) {
  auto it = named_anims_.find(name);
  return (it != named_anims_.end()) ? it->second : nullptr;
}

bool AnimationGroup::ContainsAnimationKind(AnimKind kind) const {
  return std::any_of(
      animations_.begin(), animations_.end(),
      [kind](const std::unique_ptr<Animation>& animation) {
        return animation != nullptr && animation->IsKindOf(kind);
      });
}

bool AnimationGroup::DestroyAnimation(Animation& animation) {
  if (animation.GetGroup() != this) {
    return false;
  }

  if (WouldRemovalEmptyOrderBucket(animation, animation.GetOrder())) {
    StopInternal(false, &animation);
  }

  std::unique_ptr<Animation> owned = ExtractOwnedAnimation(animation);
  return owned != nullptr;
}

bool AnimationGroup::ReparentAnimation(Animation& animation,
                                       AnimationGroup* new_parent,
                                       const int old_order) {
  if (!new_parent || animation.GetGroup() == new_parent) {
    return false;
  }

  AnimationGroup* old_parent = animation.GetGroup();
  if (!old_parent) {
    return false;
  }

  if (old_parent->WouldRemovalEmptyOrderBucket(animation, old_order)) {
    old_parent->StopInternal(false, &animation);
  }

  std::unique_ptr<Animation> owned = old_parent->ExtractOwnedAnimation(animation);
  if (!owned) {
    return false;
  }

  new_parent->AdoptAnimation(std::move(owned));
  return true;
}

void AnimationGroup::SetScriptRef(const std::string& handler, lua_State* L,
                                  int lua_ref) {
  SetScriptRef(handler, L, lua_ref,
               openwow::ui::game::SecureExecution::Get().CurrentTaint(L));
}

void AnimationGroup::SetScriptRef(const std::string& handler, lua_State* L,
                                  int lua_ref, const int taint_source) {
  const std::string key = CanonicalGroupScriptHandlerName(handler);
  if (key.empty()) {
    if (L != nullptr && lua_ref != LUA_NOREF) {
      luaL_unref(L, LUA_REGISTRYINDEX, lua_ref);
    }
    return;
  }

  script_host_.SetHandler(key, L, lua_ref, taint_source);
}

int AnimationGroup::GetScriptRef(const std::string& handler) const {
  const std::string key = CanonicalGroupScriptHandlerName(handler);
  if (key.empty()) {
    return LUA_NOREF;
  }

  return script_host_.GetHandlerRef(key);
}

int AnimationGroup::ScriptTaintSource(const std::string& handler) const {
  const std::string key = CanonicalGroupScriptHandlerName(handler);
  if (key.empty()) {
    return 0;
  }
  return script_host_.HandlerTaintSource(key);
}

bool AnimationGroup::HasScript(const std::string& handler) const {
  const std::string key = CanonicalGroupScriptHandlerName(handler);
  if (key.empty()) {
    return false;
  }

  return script_host_.HasHandler(key);
}

void AnimationGroup::SetLuaObjectRef(lua_State* L, int lua_ref) {
  script_host_.SetObjectRef(L, lua_ref);
}

void AnimationGroup::FireScript(const std::string& handler, lua_State* L) {
  const std::string key = CanonicalGroupScriptHandlerName(handler);
  if (key.empty()) {
    return;
  }

  script_host_.Fire(key, L);
}

void AnimationGroup::FireScriptWithNumber(const std::string& handler, lua_State* L,
                                          const float value) {
  const std::string key = CanonicalGroupScriptHandlerName(handler);
  if (key.empty()) {
    return;
  }

  script_host_.Fire(key, L, value);
}

void AnimationGroup::FireScriptWithBoolean(const std::string& handler, lua_State* L,
                                           const bool value) {
  const std::string key = CanonicalGroupScriptHandlerName(handler);
  if (key.empty()) {
    return;
  }

  script_host_.Fire(key, L, value);
}

void AnimationGroup::FireScriptWithString(const std::string& handler, lua_State* L,
                                          const char* value) {
  const std::string key = CanonicalGroupScriptHandlerName(handler);
  if (key.empty()) {
    return;
  }

  script_host_.Fire(key, L, value);
}

void AnimationGroup::FinalizeXmlLoad() {
  StopInternal(false);

  int next_order = 0;
  for (auto& owned_animation : animations_) {
    Animation* animation = owned_animation.get();
    if (animation == nullptr) {
      continue;
    }

    const int current_order = animation->GetOrder();
    if (current_order == -1) {
      animation->SetOrder(next_order, false, true);
      ++next_order;
    } else {
      next_order = current_order + 1;
    }

    if (next_order > 99) {
      next_order = 0;
    }
  }

  InvalidateOrderBuckets();
  (void)GetOrderBuckets();
  (void)GetDuration();
  FireScript("OnLoad", nullptr);
}

int AnimationGroup::GetMaxOrder() const {
  const auto& buckets = GetOrderBuckets();
  return buckets.empty() ? -1 : buckets.back().order;
}

int AnimationGroup::MaxOrder() const {
  return GetMaxOrder();
}

bool AnimationGroup::AdvanceOrder(float raw_dt, bool* group_on_update_fired) {
  const int step = loop_state_ == AnimLoopState::Reverse ? -1 : 1;
  const int next = current_order_slot_index_ + step;
  if (GetOrderBucket(next) != nullptr) {
    current_order_slot_index_ = next;
    BeginOrderPlayback(current_order_slot_index_);
    return true;
  }

  if (loop_type_ == AnimLoopType::None || pending_finish_) {
    if (group_on_update_fired != nullptr && !*group_on_update_fired) {
      FireScriptWithNumber("OnUpdate", nullptr, raw_dt);
      *group_on_update_fired = true;
    }
    FireScriptWithBoolean("OnFinished", nullptr, pending_finish_);
    ResetPlaybackState();
    return false;
  }

  loop_state_ = (loop_type_ == AnimLoopType::Bounce && loop_state_ == AnimLoopState::Forward)
                    ? AnimLoopState::Reverse
                    : AnimLoopState::Forward;
  current_order_slot_index_ =
      loop_state_ == AnimLoopState::Reverse ? static_cast<int>(GetOrderBuckets().size()) - 1 : 0;
  elapsed_ = 0.0f;
  completion_ratio_ = 0.0f;
  done_ = false;
  ResetChildren(loop_state_);
  BeginOrderPlayback(current_order_slot_index_);
  FireScriptWithString("OnLoop", nullptr, LoopStateToString(loop_state_));
  return true;
}

void AnimationGroup::BeginOrderPlayback(const int order_slot_index) {
  const OrderBucket* bucket = GetOrderBucket(order_slot_index);
  if (bucket == nullptr) {
    return;
  }

  if (loop_state_ == AnimLoopState::Reverse) {
    for (auto it = bucket->animations.rbegin(); it != bucket->animations.rend(); ++it) {
      if (*it != nullptr) {
        (*it)->BeginPlayFromGroup();
      }
    }
    return;
  }

  for (Animation* animation : bucket->animations) {
    if (animation != nullptr) {
      animation->BeginPlayFromGroup();
    }
  }
}

void AnimationGroup::ResetChildren(const AnimLoopState loop_state) {
  const uint8_t direction = PlaybackDirectionForLoopState(loop_state);
  for (auto& a : animations_) {
    a->PrepareForPlayback(direction);
  }
}

void AnimationGroup::ResetPlaybackState() {
  stop_in_progress_ = false;

  done_ = false;
  pending_finish_ = false;
  loop_state_ = AnimLoopState::None;
  state_ = AnimState::Stopped;
  elapsed_ = 0.0f;
  completion_ratio_ = 0.0f;
  current_order_slot_index_ = -1;
}

bool AnimationGroup::HandleChildStop(Animation& animation) {
  if (loop_state_ == AnimLoopState::None || stop_in_progress_) {
    return false;
  }

  for (const auto& owned_animation : animations_) {
    Animation* sibling = owned_animation.get();
    if (sibling != nullptr && sibling != &animation && sibling->IsPlaying()) {
      return false;
    }
  }

  const OrderBucket* bucket = GetOrderBucket(current_order_slot_index_);
  if (bucket == nullptr) {
    return false;
  }

  stop_in_progress_ = true;
  for (Animation* sibling : bucket->animations) {
    if (sibling != nullptr && sibling != &animation) {
      sibling->Stop(false);
    }
  }

  FireScriptWithBoolean("OnStop", nullptr, false);
  ResetPlaybackState();
  return true;
}

void AnimationGroup::OnAnimationDestroyed(const Animation& animation) {
  UnregisterNamedAnimation(animation);
  InvalidateOrderBuckets();
}

void AnimationGroup::OnAnimationOrderChanged(Animation& animation, const int old_order) {
  if (WouldRemovalEmptyOrderBucket(animation, old_order)) {
    StopInternal(false, &animation);
  }
  TouchOrderBucketMembership(animation);
  InvalidateOrderBuckets();
}

std::unique_ptr<Animation> AnimationGroup::ExtractOwnedAnimation(Animation& animation) {
  auto it = std::find_if(animations_.begin(), animations_.end(),
                         [&animation](const auto& owned) { return owned.get() == &animation; });
  if (it == animations_.end()) {
    return nullptr;
  }

  UnregisterNamedAnimation(animation);
  auto extracted = std::move(*it);
  animations_.erase(it);
  InvalidateOrderBuckets();
  if (extracted) {
    extracted->SetGroup(nullptr);
  }
  return extracted;
}

void AnimationGroup::AdoptAnimation(std::unique_ptr<Animation> animation) {
  if (!animation) {
    return;
  }

  animation->SetGroup(this);
  TouchOrderBucketMembership(*animation);
  RegisterNamedAnimation(animation.get());
  animations_.insert(animations_.begin(), std::move(animation));
  InvalidateOrderBuckets();
}

void AnimationGroup::InvalidateOrderBuckets() {
  order_buckets_dirty_ = true;
}

void AnimationGroup::TouchOrderBucketMembership(Animation& animation) {
  animation.order_bucket_sequence_ = next_order_bucket_sequence_++;
}

bool AnimationGroup::WouldRemovalEmptyOrderBucket(const Animation& animation,
                                                  const int removal_order) const {
  if (removal_order < 0) {
    return false;
  }

  int matches = 0;
  for (const auto& owned : animations_) {
    const Animation* candidate = owned.get();
    if (candidate == nullptr) {
      continue;
    }

    const int candidate_order = candidate == &animation ? removal_order : candidate->GetOrder();
    if (candidate_order != removal_order) {
      continue;
    }

    ++matches;
    if (matches > 1) {
      return false;
    }
  }

  return matches == 1;
}

void AnimationGroup::RebuildOrderBuckets() const {
  order_buckets_.clear();

  for (const auto& owned : animations_) {
    Animation* animation = owned.get();
    if (animation == nullptr) {
      continue;
    }

    const int order = animation->GetOrder();
    auto bucket_it = std::lower_bound(
        order_buckets_.begin(), order_buckets_.end(), order,
        [](const OrderBucket& bucket, const int candidate_order) {
          return bucket.order < candidate_order;
        });
    if (bucket_it == order_buckets_.end() || bucket_it->order != order) {
      bucket_it = order_buckets_.insert(bucket_it, OrderBucket{order, {}});
    }
    bucket_it->animations.push_back(animation);
  }

  for (OrderBucket& bucket : order_buckets_) {
    std::stable_sort(bucket.animations.begin(), bucket.animations.end(),
                     [](const Animation* lhs, const Animation* rhs) {
                       if (lhs == nullptr || rhs == nullptr) {
                         return lhs != nullptr;
                       }
                       return lhs->order_bucket_sequence_ > rhs->order_bucket_sequence_;
                     });
  }

  order_buckets_dirty_ = false;
}

const AnimationGroup::OrderBucket* AnimationGroup::GetOrderBucket(
    const int order_slot_index) const {
  const auto& buckets = GetOrderBuckets();
  if (order_slot_index < 0 || order_slot_index >= static_cast<int>(buckets.size())) {
    return nullptr;
  }
  return &buckets[order_slot_index];
}

const std::vector<AnimationGroup::OrderBucket>& AnimationGroup::GetOrderBuckets() const {
  if (order_buckets_dirty_) {
    RebuildOrderBuckets();
  }
  return order_buckets_;
}

void AnimationGroup::RegisterNamedAnimation(Animation* animation) {
  if (!animation || animation->GetName().empty()) {
    return;
  }

  named_anims_[animation->GetName()] = animation;
}

void AnimationGroup::UnregisterNamedAnimation(const Animation& animation) {
  if (animation.GetName().empty()) {
    return;
  }

  auto it = named_anims_.find(animation.GetName());
  if (it != named_anims_.end() && it->second == &animation) {
    named_anims_.erase(it);
  }
}

}
