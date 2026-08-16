
#pragma once

namespace openwow::math::aabox {

inline const float* Translate(float* box, const float* offset) {
  box[0] += offset[0];
  box[1] += offset[1];
  box[2] += offset[2];
  box[3] += offset[0];
  box[4] += offset[1];
  box[5] += offset[2];
  return offset;

}

}
