
#pragma once

#include "openwow/ui/animation/animation.h"
#include "openwow/ui/animation/animation_group.h"

namespace openwow::ui::anim {

inline void* GetAnimRegionParent(Animation* anim) {
  if (!anim) return nullptr;
  auto* group = anim->GetGroup();
  if (!group) return nullptr;
  return group->GetParentFrame();
}

inline bool FinishAnimation(Animation* anim) {
  if (!anim) return false;
  if (anim->GetState() == AnimState::Stopped) return true;

  anim->SetDone(true);
  return false;
}

inline bool SetAnimParent(Animation* anim, AnimationGroup* new_parent) {
  if (!anim || !new_parent) return false;
  auto* old_parent = anim->GetGroup();
  if (!old_parent || old_parent == new_parent) return false;
  return old_parent->ReparentAnimation(*anim, new_parent, anim->GetOrder());
}

inline bool OnAnimationHide(Animation* anim, float dt) {
  if (!anim) return true;
  if (anim->GetState() == AnimState::Stopped) return true;

  anim->Update(dt);
  return anim->GetState() == AnimState::Stopped;
}

}
