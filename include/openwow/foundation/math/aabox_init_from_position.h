
#pragma once

namespace openwow::math::aabox {

inline float* InitFromPosition(float* box, const float* position) {
  box[0] = position[0];
  box[1] = position[1];
  box[2] = position[2];
  box[3] = position[0];
  box[4] = position[1];
  box[5] = position[2];
  return box;

}

}
