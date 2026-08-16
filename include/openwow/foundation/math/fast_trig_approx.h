
#pragma once

namespace openwow::math {

struct FastTrigSample {
  float sine = 0.0f;
  float cosine = 0.0f;
};

namespace detail {

struct SplitFloatIntFracParts {
  int integral_part = 0;
  float fractional_part = 0.0f;
};

inline SplitFloatIntFracParts SplitFloatIntFrac(const float value) {
  const auto truncated = static_cast<long long>(value);
  SplitFloatIntFracParts parts;
  parts.integral_part =
      value <= 0.0f ? static_cast<int>(truncated - 1) : static_cast<int>(truncated);
  parts.fractional_part = value - static_cast<float>(parts.integral_part);
  return parts;
}

inline float EvaluateFastTrigPolynomial(const float reduced_turns) {
  const SplitFloatIntFracParts parts = SplitFloatIntFrac(reduced_turns);
  double result = 1.0 - static_cast<double>(parts.fractional_part) *
                            ((6.0 - 4.0 * static_cast<double>(parts.fractional_part)) *
                             static_cast<double>(parts.fractional_part));
  if ((parts.integral_part & 1) != 0) {
    result = -result;
  }
  return static_cast<float>(result);
}

}

inline constexpr float kFastTrigReciprocalPi = 0.31830987f;

inline float FastSinApprox(const float radians) {
  return detail::EvaluateFastTrigPolynomial(radians * kFastTrigReciprocalPi - 0.5f);
}

inline float FastCosApprox(const float radians) {
  return detail::EvaluateFastTrigPolynomial(radians * kFastTrigReciprocalPi);
}

inline FastTrigSample FastSinCosApprox(const float radians) {
  const float half_turns = radians * kFastTrigReciprocalPi;
  return {
      .sine = detail::EvaluateFastTrigPolynomial(half_turns - 0.5f),
      .cosine = detail::EvaluateFastTrigPolynomial(half_turns),
  };
}

}
