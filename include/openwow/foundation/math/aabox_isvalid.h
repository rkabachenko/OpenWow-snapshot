
#pragma once

namespace openwow::math::aabox {

inline bool IsValid(const float* box) {
  return box[3] > box[0]
      && box[4] > box[1]
      && box[5] > box[2];
}

}
