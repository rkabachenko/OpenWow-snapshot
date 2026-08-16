
#include "openwow/ui/animation/translation_anim.h"

namespace openwow::ui::anim {

void TranslationAnim::Apply(float progress) {
  current_x_ = StoredAnimationOffsetToPixels(stored_offset_x_) * progress;
  current_y_ = StoredAnimationOffsetToPixels(stored_offset_y_) * progress;
}

void TranslationAnim::ResetEffect() {
  current_x_ = 0.0f;
  current_y_ = 0.0f;
}

}
