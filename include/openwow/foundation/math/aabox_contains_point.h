
#pragma once

namespace openwow::math::aabox {

inline bool ContainsPoint(const float* box, const float* point) {
  return box[0] < point[0]
      && box[1] < point[1]
      && box[2] < point[2]
      && box[3] > point[0]
      && box[4] > point[1]
      && box[5] > point[2];
}

}
