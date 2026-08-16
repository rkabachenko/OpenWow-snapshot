#pragma once

#include "openwow/foundation/math/retail_camera_matrix.h"

#include <cmath>

namespace openwow::math {

inline void BuildM2CameraViewMatrix(
    const float position[3],
    const float target[3],
    float roll,
    float out_matrix4x4[16]) {

    const float reference_up[3] = {
        0.0f,
        -std::sin(roll),
        std::cos(roll),
    };
    BuildRetailCameraViewMatrix(position, target, reference_up, out_matrix4x4);
}

}
