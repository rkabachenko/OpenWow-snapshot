#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace openwow::render {

struct C3Vector {
  float x{0.0f}, y{0.0f}, z{0.0f};

  C3Vector operator+(const C3Vector& o) const { return {x + o.x, y + o.y, z + o.z}; }
  C3Vector operator-(const C3Vector& o) const { return {x - o.x, y - o.y, z - o.z}; }
  C3Vector operator*(float s) const { return {x * s, y * s, z * s}; }
  C3Vector& operator+=(const C3Vector& o) { x += o.x; y += o.y; z += o.z; return *this; }
  float LengthSq() const { return x * x + y * y + z * z; }
  float Length() const { return std::sqrt(LengthSq()); }
};

struct SplineFrame {
  C3Vector forward{};
  C3Vector right{};
  C3Vector up{};
  C3Vector position{};
};

class CSpline {
 public:
  enum class CurveType : std::uint8_t {
    kLinear = 0,
    kCatmullRom = 1,
    kBezier = 2,
  };

  static constexpr int kUniformParameterMode = 0;
  static constexpr int kArcLengthParameterMode = 1;

  CSpline() = default;
  explicit CSpline(CurveType type) noexcept : curve_type_(type) {}
  ~CSpline() = default;

  void SetCurveType(CurveType type) noexcept { curve_type_ = type; }
  [[nodiscard]] CurveType GetCurveType() const noexcept { return curve_type_; }

  void SetControlPoints(const C3Vector* points, std::uint32_t count) {
    if (points == nullptr || count == 0) {
      point_storage_.clear();
      segment_length_storage_.clear();
      num_points_ = 0;
      return;
    }

    point_storage_.assign(points, points + count);
    num_points_ = count;
    segment_length_storage_.resize(GetSegmentCount());

    if (GetSegmentCount() > 0) {
      RecalculateSegmentLengths();
      total_length_ = SumSegmentLengths();
    }
  }

  void SetControlPoints(const std::vector<C3Vector>& points) {
    SetControlPoints(points.data(), static_cast<std::uint32_t>(points.size()));
  }

  [[nodiscard]] bool EvaluateInto(float t, C3Vector& out, int mode = 0) const {
    if (num_points_ == 0) {
      return false;
    }

    if (t <= 0.0f) {
      out = point_storage_[0];
      return true;
    }

    if (t >= 1.0f) {
      out = point_storage_[num_points_ - 1];
      return true;
    }

    if (!HasInteriorSegments()) {
      return false;
    }

    switch (mode) {
      case kUniformParameterMode:
        out = EvaluateWithUniformParameter(t);
        return true;
      case kArcLengthParameterMode:
        out = EvaluateWithArcLength(t);
        return true;
      default:
        return false;
    }
  }

  C3Vector Evaluate(float t, int mode = 0) const {
    C3Vector out{};
    (void)EvaluateInto(t, out, mode);
    return out;
  }

  [[nodiscard]] bool EvaluateTangentInto(float t, C3Vector& out, int mode = 0) const {
    if (!HasInteriorSegments()) {
      return false;
    }

    const float clamped_t = std::clamp(t, 0.0f, 1.0f);
    switch (mode) {
      case kUniformParameterMode:
        out = EvaluateTangentWithUniformParameter(clamped_t);
        return true;
      case kArcLengthParameterMode:
        out = EvaluateTangentWithArcLength(clamped_t);
        return true;
      default:
        return false;
    }
  }

  C3Vector EvaluateTangent(float t, int mode = 0) const {
    C3Vector out{};
    (void)EvaluateTangentInto(t, out, mode);
    return out;
  }

  [[nodiscard]] bool EvaluateFrameInto(float t,
                                       SplineFrame& out,
                                       int mode = kArcLengthParameterMode) const {
    if (mode != kArcLengthParameterMode) {
      return false;
    }

    const float clamped_t = std::clamp(t, 0.0f, 1.0f);

    SplineFrame frame{};
    if (curve_type_ == CurveType::kBezier) {
      const auto bezier_frame = EvaluateBezierArcLengthFrame(clamped_t);
      if (!bezier_frame.has_value()) {
        return false;
      }
      out = *bezier_frame;
      return true;
    }

    if (!HasInteriorSegments()) {
      return false;
    }

    std::uint32_t segment_index = 0;
    float local_t = clamped_t;
    SelectArcLengthSegment(clamped_t, segment_index, local_t);
    frame.position = EvaluateSegment(segment_index, local_t);
    frame.forward = EvaluateArcLengthFrameForward(segment_index, local_t);
    frame.right = {-frame.forward.y, frame.forward.x, 0.0f};

    constexpr float kBasisNormalizeMinLengthSq = 0.00000023841858f;
    const float right_length_sq = frame.right.x * frame.right.x +
                                  frame.right.y * frame.right.y;
    if (right_length_sq > kBasisNormalizeMinLengthSq) {
      const float inverse_right_length = 1.0f / std::sqrt(right_length_sq);
      frame.right.x *= inverse_right_length;
      frame.right.y *= inverse_right_length;
    }

    frame.up = Cross(frame.forward, frame.right);
    out = frame;
    return true;
  }

  [[nodiscard]] std::optional<SplineFrame> EvaluateFrame(
      float t,
      int mode = kArcLengthParameterMode) const {
    SplineFrame out{};
    if (!EvaluateFrameInto(t, out, mode)) {
      return std::nullopt;
    }
    return out;
  }

  [[nodiscard]] float GetTotalLength() const { return total_length_; }

  [[nodiscard]] float GetAccumulatedLengthToPoint(std::uint32_t pointIndex) const {
    if (pointIndex <= 1) return 0.0f;
    const auto seg_count = GetSegmentCount();
    const auto limit = std::min(pointIndex - 1, seg_count);
    double accum = 0.0;
    for (std::uint32_t i = 0; i < limit; ++i) {
      accum += static_cast<double>(segment_length_storage_[i]);
    }
    return static_cast<float>(accum);
  }

  [[nodiscard]] std::uint32_t GetNumPoints() const { return num_points_; }

  [[nodiscard]] const C3Vector& GetPoint(std::uint32_t idx) const {
    return point_storage_[idx];
  }
  [[nodiscard]] std::uint32_t CopyControlPoints(C3Vector* destination,
                                                std::uint32_t max_count) const {
    if (destination == nullptr) return 0;
    const auto count = std::min<std::size_t>(point_storage_.size(), max_count);
    std::copy_n(point_storage_.begin(), count, destination);
    return static_cast<std::uint32_t>(count);
  }

  [[nodiscard]] std::uint32_t GetRemainingPoints(
      float t,
      C3Vector* output,
      std::uint32_t max_count) const {
    if (curve_type_ != CurveType::kCatmullRom || !HasInteriorSegments()) {
      return 0;
    }

    std::uint32_t segment_index = 0;
    float local_t = 0.0f;
    SelectArcLengthSegment(t, segment_index, local_t);

    const std::uint32_t segment_count = GetSegmentCount();
    std::uint32_t remaining =
        segment_count > segment_index ? segment_count - segment_index : 0;

    if (output == nullptr) {
      return remaining;
    }

    if (remaining > max_count) {
      remaining = max_count;
    }

    for (std::uint32_t i = 0; i < remaining; ++i) {
      output[i] = point_storage_[segment_index + 2 + i];
    }

    return remaining;
  }

  [[nodiscard]] std::optional<SplineFrame> EvaluateBezierArcLengthFrame(float t) const {
    if (curve_type_ != CurveType::kBezier) {
      return std::nullopt;
    }

    const auto segment_count = GetSegmentCount();
    if (segment_count == 0) {
      return std::nullopt;
    }

    std::uint32_t segment_index = 0;
    float local_t = t;
    if (segment_count > 1) {
      if (total_length_ <= 0.0f) {
        return std::nullopt;
      }
      SelectArcLengthSegment(t, segment_index, local_t);
    }

    SplineFrame frame;
    frame.position = EvaluateBezierSegment(segment_index, local_t);

    const C3Vector tangent = EvaluateBezierSegmentTangent(segment_index, local_t);
    constexpr float kMinTangentLength = 0.01f;
    const float tangent_length = tangent.Length();
    if (tangent_length < kMinTangentLength) {
      return std::nullopt;
    }

    frame.forward = tangent * (1.0f / tangent_length);

    const float horizontal_length_sq =
        frame.forward.x * frame.forward.x + frame.forward.y * frame.forward.y;
    if (horizontal_length_sq <= 0.0f) {
      return std::nullopt;
    }

    const float inverse_horizontal_length = 1.0f / std::sqrt(horizontal_length_sq);
    frame.right = {
        -frame.forward.y * inverse_horizontal_length,
        frame.forward.x * inverse_horizontal_length,
        0.0f,
    };
    frame.up = Cross(frame.forward, frame.right);
    return frame;
  }

 private:
  static C3Vector Lerp(const C3Vector& from, const C3Vector& to, float t) {
    return from * (1.0f - t) + to * t;
  }

  static C3Vector Cross(const C3Vector& lhs, const C3Vector& rhs) {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
  }

  static float Dot(const C3Vector& lhs, const C3Vector& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
  }

  static float DistanceBetween(const C3Vector& from, const C3Vector& to) {
    const double dx = static_cast<double>(to.x) - static_cast<double>(from.x);
    const double dy = static_cast<double>(to.y) - static_cast<double>(from.y);
    const double dz = static_cast<double>(to.z) - static_cast<double>(from.z);
    return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
  }

  [[nodiscard]] std::uint32_t GetSegmentCount() const noexcept {
    if (curve_type_ == CurveType::kBezier) {
      return num_points_ / 3u;
    }
    return num_points_ > 3 ? num_points_ - 3u : 0u;
  }

  [[nodiscard]] bool HasInteriorSegments() const noexcept {
    return GetSegmentCount() != 0;
  }

  C3Vector EvaluateWithUniformParameter(float t) const {
    int segment_index = 0;
    float local_t = t;
    if (curve_type_ == CurveType::kBezier) {
      SelectUniformBezierSegment(t, segment_index, local_t);
    } else {
      SelectUniformSegment(t, segment_index, local_t);
    }
    return EvaluateSegment(static_cast<std::uint32_t>(segment_index), local_t);
  }

  C3Vector EvaluateWithArcLength(float t) const {
    std::uint32_t segment_index = 0;
    float local_t = t;
    SelectArcLengthSegment(t, segment_index, local_t);
    return EvaluateSegment(segment_index, local_t);
  }

  C3Vector EvaluateTangentWithUniformParameter(float t) const {
    int segment_index = 0;
    float local_t = t;
    if (curve_type_ == CurveType::kBezier) {
      SelectUniformBezierSegment(t, segment_index, local_t);
    } else {
      SelectUniformSegment(t, segment_index, local_t);
    }
    return EvaluateSegmentTangent(static_cast<std::uint32_t>(segment_index), local_t);
  }

  C3Vector EvaluateTangentWithArcLength(float t) const {
    std::uint32_t segment_index = 0;
    float local_t = t;
    SelectArcLengthSegment(t, segment_index, local_t);
    return EvaluateSegmentTangent(segment_index, local_t);
  }

  C3Vector EvaluateArcLengthFrameForward(std::uint32_t segment_index, float local_t) const {
    constexpr float kDirectionNormalizeMinLengthSq = 0.00000023841858f;
    C3Vector forward = point_storage_[segment_index + 2] - point_storage_[segment_index + 1];
    const float chord_length_sq = forward.LengthSq();
    if (chord_length_sq > kDirectionNormalizeMinLengthSq) {
      const float inverse_chord_length = 1.0f / std::sqrt(chord_length_sq);
      forward = forward * inverse_chord_length;
    }

    if (curve_type_ != CurveType::kCatmullRom) {
      return forward;
    }

    C3Vector tangent = EvaluateCatmullRomSegmentTangent(segment_index, local_t);
    constexpr float kDerivativeNormalizeMinLengthSq = 0.000099999997f;
    const float tangent_length_sq = tangent.LengthSq();
    if (tangent_length_sq <= kDerivativeNormalizeMinLengthSq) {
      return forward;
    }

    const float inverse_tangent_length = 1.0f / std::sqrt(tangent_length_sq);
    tangent = tangent * inverse_tangent_length;
    if (Dot(tangent, forward) >= 0.5f) {
      return tangent;
    }
    return forward;
  }

  void SelectUniformSegment(float t, int& segment_index, float& local_t) const {
    const float segment_count = static_cast<float>(GetSegmentCount());
    const float scaled = segment_count * t;

    segment_index = static_cast<int>(std::nearbyint(scaled - 0.5f));
    local_t = (t - (static_cast<float>(segment_index) / segment_count)) * segment_count;
  }

  void SelectUniformBezierSegment(float t, int& segment_index, float& local_t) const {
    const auto raw_segment_count = GetSegmentCount();
    if (raw_segment_count == 0) {
      segment_index = 0;
      local_t = t;
      return;
    }

    const float segment_count = static_cast<float>(raw_segment_count);
    const float scaled = segment_count * t;
    segment_index = static_cast<int>(scaled - 0.5f);
    local_t = (t - (static_cast<float>(segment_index) / segment_count)) * segment_count;
  }

  void SelectArcLengthSegment(float t,
                              std::uint32_t& segment_index,
                              float& local_t) const {
    const auto segment_count = GetSegmentCount();
    if (segment_count <= 1) {
      segment_index = 0;
      local_t = t;
      return;
    }

    const double target_length =
        static_cast<double>(total_length_) * static_cast<double>(t);
    double accumulated_length = 0.0;
    segment_index = 0;
    for (; segment_index + 1 < segment_count; ++segment_index) {
      const double segment_length =
          static_cast<double>(segment_length_storage_[segment_index]);
      if (accumulated_length + segment_length > target_length) {
        break;
      }
      accumulated_length += segment_length;
    }

    const double segment_length =
        static_cast<double>(segment_length_storage_[segment_index]);
    local_t = static_cast<float>((target_length - accumulated_length) / segment_length);
  }

  C3Vector EvaluateSegment(std::uint32_t segment_index, float local_t) const {
    if (curve_type_ == CurveType::kCatmullRom) {
      return EvaluateCatmullRomSegment(segment_index, local_t);
    }
    if (curve_type_ == CurveType::kBezier) {
      return EvaluateBezierSegment(segment_index, local_t);
    }
    return Lerp(point_storage_[segment_index + 1], point_storage_[segment_index + 2], local_t);
  }

  C3Vector EvaluateSegmentTangent(std::uint32_t segment_index, float local_t) const {
    if (curve_type_ == CurveType::kCatmullRom) {
      return EvaluateCatmullRomSegmentTangent(segment_index, local_t);
    }
    if (curve_type_ == CurveType::kBezier) {
      return EvaluateBezierSegmentTangent(segment_index, local_t);
    }
    return EvaluateLinearSegmentTangent(segment_index);
  }

  C3Vector EvaluateLinearSegmentTangent(std::uint32_t segment_index) const {
    return point_storage_[segment_index + 2] - point_storage_[segment_index + 1];
  }

  C3Vector EvaluateCatmullRomSegment(std::uint32_t segment_index, float local_t) const {
    const C3Vector& p0 = point_storage_[segment_index];
    const C3Vector& p1 = point_storage_[segment_index + 1];
    const C3Vector& p2 = point_storage_[segment_index + 2];
    const C3Vector& p3 = point_storage_[segment_index + 3];

    const float t2 = local_t * local_t;
    const float t3 = t2 * local_t;
    return p0 * (-0.5f * t3 + t2 - 0.5f * local_t) +
           p1 * (1.5f * t3 - 2.5f * t2 + 1.0f) +
           p2 * (-1.5f * t3 + 2.0f * t2 + 0.5f * local_t) +
           p3 * (0.5f * t3 - 0.5f * t2);
  }

  C3Vector EvaluateCatmullRomSegmentTangent(std::uint32_t segment_index,
                                            float local_t) const {
    const C3Vector& p0 = point_storage_[segment_index];
    const C3Vector& p1 = point_storage_[segment_index + 1];
    const C3Vector& p2 = point_storage_[segment_index + 2];
    const C3Vector& p3 = point_storage_[segment_index + 3];

    const float t2 = local_t * local_t;
    return p0 * (-1.5f * t2 + 2.0f * local_t - 0.5f) +
           p1 * (4.5f * t2 - 5.0f * local_t) +
           p2 * (-4.5f * t2 + 4.0f * local_t + 0.5f) +
           p3 * (1.5f * t2 - local_t);
  }

  C3Vector EvaluateBezierSegment(std::uint32_t segment_index, float local_t) const {
    const std::uint32_t start = 3u * segment_index;
    if (start + 3u >= num_points_) {
      return {};
    }

    const C3Vector& p0 = point_storage_[start];
    const C3Vector& p1 = point_storage_[start + 1];
    const C3Vector& p2 = point_storage_[start + 2];
    const C3Vector& p3 = point_storage_[start + 3];

    const float one_minus_t = 1.0f - local_t;
    const float one_minus_t2 = one_minus_t * one_minus_t;
    const float t2 = local_t * local_t;
    return p0 * (one_minus_t * one_minus_t2) +
           p1 * (3.0f * local_t * one_minus_t2) +
           p2 * (3.0f * t2 * one_minus_t) +
           p3 * (t2 * local_t);
  }

  C3Vector EvaluateBezierSegmentTangent(std::uint32_t segment_index, float local_t) const {
    const std::uint32_t start = 3u * segment_index;
    if (start + 3u >= num_points_) {
      return {};
    }

    const C3Vector& p0 = point_storage_[start];
    const C3Vector& p1 = point_storage_[start + 1];
    const C3Vector& p2 = point_storage_[start + 2];
    const C3Vector& p3 = point_storage_[start + 3];

    const float t2 = local_t * local_t;
    return p0 * (-3.0f * t2 + 6.0f * local_t - 3.0f) +
           p1 * (9.0f * t2 - 12.0f * local_t + 3.0f) +
           p2 * (-9.0f * t2 + 6.0f * local_t) +
           p3 * (3.0f * t2);
  }

  float ComputeSegmentLength(std::uint32_t segment_index) const {
    if (curve_type_ == CurveType::kBezier) {
      constexpr float kStep = 0.05f;
      C3Vector previous = EvaluateBezierSegment(segment_index, 0.0f);
      double total = 0.0;
      float sample_t = kStep;
      for (int sample = 0; sample < 20; ++sample, sample_t += kStep) {
        const C3Vector current = EvaluateBezierSegment(segment_index, sample_t);
        total += static_cast<double>(DistanceBetween(previous, current));
        previous = current;
      }
      return static_cast<float>(total);
    }

    if (curve_type_ != CurveType::kCatmullRom) {
      return DistanceBetween(point_storage_[segment_index + 1],
                             point_storage_[segment_index + 2]);
    }

    constexpr float kStep = 0.05f;
    C3Vector previous = EvaluateCatmullRomSegment(segment_index, 0.0f);
    double total = 0.0;
    float sample_t = kStep;
    for (int sample = 0; sample < 20; ++sample, sample_t += kStep) {
      const C3Vector current = EvaluateCatmullRomSegment(segment_index, sample_t);
      total += static_cast<double>(DistanceBetween(previous, current));
      previous = current;
    }
    return static_cast<float>(total);
  }

  void RecalculateSegmentLengths() {
    const auto segment_count = GetSegmentCount();
    for (std::uint32_t segment_index = 0; segment_index < segment_count; ++segment_index) {
      segment_length_storage_[segment_index] = ComputeSegmentLength(segment_index);
    }
  }

  float SumSegmentLengths() const {
    double total = 0.0;
    for (std::uint32_t segment_index = 0; segment_index < GetSegmentCount(); ++segment_index) {
      total += static_cast<double>(segment_length_storage_[segment_index]);
    }
    return static_cast<float>(total);
  }

  std::vector<C3Vector> point_storage_;
  std::vector<float> segment_length_storage_;
  std::uint32_t num_points_{0};
  float total_length_{0.0f};
  CurveType curve_type_{CurveType::kLinear};
};

}
