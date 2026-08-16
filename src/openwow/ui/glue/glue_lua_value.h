#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace openwow::ui::glue {

struct GlueLuaValue {
  enum class Kind : std::uint8_t {
    kNil = 0,
    kString = 1,
    kNumber = 2,
    kBoolean = 3,
  };
  Kind kind{Kind::kNil};
  std::string string_value;
  double number_value{0.0};
  bool bool_value{false};
};

inline GlueLuaValue MakeLuaString(std::string value) {
  GlueLuaValue v;
  v.kind = GlueLuaValue::Kind::kString;
  v.string_value = std::move(value);
  return v;
}

inline GlueLuaValue MakeLuaNumber(double value) {
  GlueLuaValue v;
  v.kind = GlueLuaValue::Kind::kNumber;
  v.number_value = value;
  return v;
}

inline GlueLuaValue MakeLuaBool(bool value) {
  GlueLuaValue v;
  v.kind = GlueLuaValue::Kind::kBoolean;
  v.bool_value = value;
  return v;
}

inline GlueLuaValue MakeLuaNil() {
  return GlueLuaValue{};
}

inline bool operator==(const GlueLuaValue& lhs, const char* rhs) {
  return lhs.kind == GlueLuaValue::Kind::kString && lhs.string_value == rhs;
}
inline bool operator==(const char* lhs, const GlueLuaValue& rhs) {
  return rhs == lhs;
}
inline bool operator==(const GlueLuaValue& lhs, const std::string& rhs) {
  return lhs.kind == GlueLuaValue::Kind::kString && lhs.string_value == rhs;
}
inline bool operator==(const std::string& lhs, const GlueLuaValue& rhs) {
  return rhs == lhs;
}

inline bool operator==(const GlueLuaValue& lhs, double rhs) {
  return lhs.kind == GlueLuaValue::Kind::kNumber && lhs.number_value == rhs;
}
inline bool operator==(double lhs, const GlueLuaValue& rhs) {
  return rhs == lhs;
}

}
