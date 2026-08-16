
#include "openwow/game/unit_path_utils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openwow::game {

namespace {

constexpr float kMinDistSq = 0.00077160494f;

constexpr float kMinStartDistSq = 1.0f;

struct Vec3 {
  float x, y, z;
};

inline Vec3 Sub(const Vec3& a, const Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline float Dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline float LenSq(const Vec3& a) {
  return a.x * a.x + a.y * a.y + a.z * a.z;
}

inline float LenSqXY(const Vec3& a) {
  return a.x * a.x + a.y * a.y;
}

inline Vec3 Normalize(const Vec3& a) {
  float len = std::sqrt(LenSq(a));
  if (len < 1e-10f) return {0, 0, 0};
  float inv = 1.0f / len;
  return {a.x * inv, a.y * inv, a.z * inv};
}

inline float PlaneDist(const Vec3& normal, float d, const Vec3& point) {
  return Dot(normal, point) + d;
}

inline Vec3 Lerp(const Vec3& a, const Vec3& b, float t) {
  return {a.x + (b.x - a.x) * t,
          a.y + (b.y - a.y) * t,
          a.z + (b.z - a.z) * t};
}

}

std::int32_t SimplifyMovePath(const C3Vector& start,
                              std::vector<C3Vector>& waypoints,
                              const float* ) {
  if (waypoints.empty()) {
    return 0;
  }

  Vec3 s = {start.x, start.y, start.z};

  auto num = static_cast<std::int32_t>(waypoints.size());

  if (num == 1) {
    Vec3 wp = {waypoints[0].x, waypoints[0].y, waypoints[0].z};
    Vec3 diff = Sub(wp, s);

    if (LenSq(diff) < kMinDistSq) {
      waypoints.clear();
      return 0;
    }

    waypoints.insert(waypoints.begin(), start);
    return 2;
  }

  Vec3 v_start = s;
  std::int32_t last_idx = num - 1;

  std::int32_t same_side_positive = 0;
  std::int32_t same_side_negative = 0;

  for (std::int32_t i = 0; i < last_idx; ++i) {
    Vec3 p0 = {waypoints[static_cast<std::size_t>(i)].x,
               waypoints[static_cast<std::size_t>(i)].y,
               waypoints[static_cast<std::size_t>(i)].z};
    Vec3 p1 = {waypoints[static_cast<std::size_t>(i + 1)].x,
               waypoints[static_cast<std::size_t>(i + 1)].y,
               waypoints[static_cast<std::size_t>(i + 1)].z};

    Vec3 dir = Sub(p1, p0);

    const float d = -Dot(dir, v_start);

    float dist0 = PlaneDist(dir, d, p0);
    float dist1 = PlaneDist(dir, d, p1);

    if (dist0 >= 0.0f && dist1 >= 0.0f) {
      ++same_side_positive;
    } else if (dist0 <= 0.0f && dist1 <= 0.0f) {
      ++same_side_negative;
    } else {

      break;
    }
  }

  if (same_side_positive == last_idx) {
    Vec3 first = {waypoints[0].x, waypoints[0].y, waypoints[0].z};
    if (LenSqXY(Sub(first, v_start)) >= kMinStartDistSq) {

      waypoints.insert(waypoints.begin(), start);
      return static_cast<std::int32_t>(waypoints.size());
    }
    return num;
  }

  if (same_side_negative == last_idx) {
    Vec3 last_wp = {waypoints[static_cast<std::size_t>(num - 1)].x,
                    waypoints[static_cast<std::size_t>(num - 1)].y,
                    waypoints[static_cast<std::size_t>(num - 1)].z};
    Vec3 diff = Sub(last_wp, v_start);
    if (LenSq(diff) < kMinDistSq) {
      waypoints.clear();
      return 0;
    }

    C3Vector last_c = waypoints[static_cast<std::size_t>(num - 1)];
    waypoints.clear();
    waypoints.push_back(start);
    waypoints.push_back(last_c);
    return 2;
  }

  for (std::int32_t i = 0; i < last_idx; ++i) {
    Vec3 p0 = {waypoints[static_cast<std::size_t>(i)].x,
               waypoints[static_cast<std::size_t>(i)].y,
               waypoints[static_cast<std::size_t>(i)].z};
    Vec3 p1 = {waypoints[static_cast<std::size_t>(i + 1)].x,
               waypoints[static_cast<std::size_t>(i + 1)].y,
               waypoints[static_cast<std::size_t>(i + 1)].z};

    Vec3 seg = Sub(p1, p0);
    Vec3 norm = Normalize(seg);
    float d = -(Dot(norm, v_start));

    float d0 = PlaneDist(norm, d, p0);
    float d1 = PlaneDist(norm, d, p1);

    if ((d0 >= 0.0f && d1 >= 0.0f) || (d0 <= 0.0f && d1 <= 0.0f)) {
      continue;
    }

    if (d0 > 0.0f && d1 < 0.0f) {

      float t = d0 / (d0 - d1);
      Vec3 cross_pt = Lerp(p0, p1, t);

      Vec3 diff = Sub(p1, cross_pt);
      if (LenSq(diff) < kMinDistSq) {

        Vec3 diff2 = Sub(p0, v_start);
        if (LenSq(diff2) < kMinDistSq) {

          if (i + 1 >= last_idx) {
            waypoints.clear();
            return 0;
          }
          continue;
        }

        waypoints[static_cast<std::size_t>(i)] = start;

        waypoints.erase(waypoints.begin(),
                        waypoints.begin() + i);
        return static_cast<std::int32_t>(waypoints.size());
      }

      waypoints[static_cast<std::size_t>(i)] = {cross_pt.x, cross_pt.y,
                                                  cross_pt.z};
      waypoints.erase(waypoints.begin(),
                      waypoints.begin() + i);
      return static_cast<std::int32_t>(waypoints.size());
    }

    waypoints.erase(waypoints.begin(),
                    waypoints.begin() + i);
    waypoints.insert(waypoints.begin(), start);
    return static_cast<std::int32_t>(waypoints.size());
  }

  return num;
}

}
