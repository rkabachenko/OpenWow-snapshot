
#pragma once

namespace openwow::math {

inline bool PointAABBDistanceTest2D(const float* aabb_min,
                                    const float* aabb_max,
                                    const float* center, float radius,
                                    int mode) {
  const float r2 = radius * radius;

  switch (mode) {

    case 0: {
      float min_dist_sq = 0.0f;
      float max_dist_sq_x;
      bool near_x_edge = false;

      const float dx_min_sq =
          (center[0] - aabb_min[0]) * (center[0] - aabb_min[0]);
      const float dx_max_sq =
          (center[0] - aabb_max[0]) * (center[0] - aabb_max[0]);
      const float x_far_sq = (dx_max_sq >= dx_min_sq) ? dx_max_sq : dx_min_sq;
      max_dist_sq_x = x_far_sq;

      if (aabb_min[0] > center[0]) {

        near_x_edge = true;
        min_dist_sq = dx_min_sq;
      } else if (aabb_max[0] >= center[0]) {

        const float x_near_sq =
            (dx_min_sq < dx_max_sq) ? dx_min_sq : dx_max_sq;
        if (x_near_sq <= r2) {
          near_x_edge = true;
        }

      } else {

        near_x_edge = true;
        min_dist_sq = dx_max_sq;
      }

      const float dy_min_sq =
          (center[1] - aabb_min[1]) * (center[1] - aabb_min[1]);
      const float dy_max_sq =
          (center[1] - aabb_max[1]) * (center[1] - aabb_max[1]);
      const float y_far_sq = (dy_max_sq >= dy_min_sq) ? dy_max_sq : dy_min_sq;

      const float max_dist_sq = max_dist_sq_x + y_far_sq;

      if (aabb_min[1] > center[1]) {

        min_dist_sq += dy_min_sq;
      } else if (aabb_max[1] >= center[1]) {

        const float y_near_sq =
            (dy_max_sq <= dy_min_sq) ? dy_max_sq : dy_min_sq;
        if (y_near_sq > r2 && !near_x_edge) {
          return false;
        }
      } else {

        min_dist_sq += dy_max_sq;
      }

      return min_dist_sq <= r2 && max_dist_sq >= r2;
    }

    case 1: {
      float dist_sq = 0.0f;
      bool has_edge = false;

      if (aabb_min[0] > center[0]) {
        const float dx = center[0] - aabb_min[0];
        dist_sq = dx * dx;
        has_edge = true;
      } else if (aabb_max[0] < center[0]) {
        const float dx = center[0] - aabb_max[0];
        dist_sq = dx * dx;
        has_edge = true;
      } else {
        if (center[0] - aabb_min[0] <= radius ||
            aabb_max[0] - center[0] <= radius) {
          has_edge = true;
        }
      }

      if (aabb_min[1] > center[1]) {
        const float dy = center[1] - aabb_min[1];
        dist_sq += dy * dy;
      } else if (aabb_max[1] >= center[1]) {

        if (center[1] - aabb_min[1] > radius) {
          const float gap = aabb_max[1] - center[1];
          if (gap > radius && !has_edge) {
            return false;
          }
        }
      } else {
        const float dy = center[1] - aabb_max[1];
        dist_sq += dy * dy;
      }

      return r2 >= dist_sq;
    }

    case 2: {
      float min_dist_sq = 0.0f;
      float max_dist_sq;

      const float dx_min = center[0] - aabb_min[0];
      const float dx_max = center[0] - aabb_max[0];
      const float dx_min_sq = dx_min * dx_min;
      const float dx_max_sq = dx_max * dx_max;
      const float x_far_sq = (dx_max_sq >= dx_min_sq) ? dx_max_sq : dx_min_sq;

      if (aabb_min[0] > center[0]) {
        max_dist_sq = x_far_sq;
        min_dist_sq = dx_min_sq;
      } else if (aabb_max[0] < center[0]) {
        min_dist_sq = dx_max_sq;
        max_dist_sq = x_far_sq;
      } else {
        max_dist_sq = x_far_sq;
      }

      const float dy_min = center[1] - aabb_min[1];
      const float dy_max = center[1] - aabb_max[1];
      const float dy_min_sq = dy_min * dy_min;
      const float dy_max_sq = dy_max * dy_max;
      const float y_far_sq = (dy_max_sq >= dy_min_sq) ? dy_max_sq : dy_min_sq;

      max_dist_sq += y_far_sq;

      if (aabb_min[1] > center[1]) {
        min_dist_sq += dy_min_sq;
      } else if (aabb_max[1] < center[1]) {
        min_dist_sq += dy_max_sq;
      }

      return min_dist_sq <= r2 && max_dist_sq >= r2;
    }

    case 3: {
      float dist_sq = 0.0f;

      if (aabb_min[0] > center[0]) {
        const float dx = center[0] - aabb_min[0];
        dist_sq = dx * dx;
      } else if (aabb_max[0] < center[0]) {
        const float dx = center[0] - aabb_max[0];
        dist_sq = dx * dx;
      }

      if (aabb_min[1] > center[1]) {
        const float dy = center[1] - aabb_min[1];
        dist_sq += dy * dy;
      } else if (aabb_max[1] < center[1]) {
        const float dy = center[1] - aabb_max[1];
        dist_sq += dy * dy;
      }

      return dist_sq <= r2;
    }

    default:
      return false;
  }
}

inline bool SphereAABBTest2D(const float* aabb6, const float* sphere4,
                             int mode) {
  return PointAABBDistanceTest2D(aabb6, aabb6 + 3, sphere4, sphere4[3], mode);
}

}
