
#include "openwow/game/ground_walk.h"
#include "openwow/foundation/math/packed_mat3x3.h"

namespace openwow::game {

float* ComputeInverseAffineTranslation(const float* row2,
                                        const float* row0,
                                        const float* row1,
                                        float* out) {

    float matrix[9];
    matrix[0] = row0[0];
    matrix[1] = row0[1];
    matrix[2] = row0[2];
    matrix[3] = row1[0];
    matrix[4] = row1[1];
    matrix[5] = row1[2];
    matrix[6] = row2[0];
    matrix[7] = row2[1];
    matrix[8] = row2[2];

    const float neg_t0 = -row0[3];
    const float neg_t1 = -row1[3];
    const float neg_t2 = -row2[3];

    const float det =
        static_cast<float>(math::packed_mat3x3::Determinant(matrix));
    float inv[9];
    math::packed_mat3x3::InvertWithDeterminant(inv, matrix, det);

    out[0] = inv[0] * neg_t0 + inv[1] * neg_t1 + inv[2] * neg_t2;
    out[1] = inv[3] * neg_t0 + inv[4] * neg_t1 + inv[5] * neg_t2;
    out[2] = inv[6] * neg_t0 + inv[7] * neg_t1 + inv[8] * neg_t2;

    return out;
}

}
