#pragma once

#include <cstdint>
#include <string_view>

namespace openwow::storage::persistence {

class ProfileValueView;

class ProfileInteger final {
 public:
  [[nodiscard]] std::uint32_t RawValue() const;

 private:
  explicit ProfileInteger(std::uint32_t value);

  std::uint32_t value_ = 0;

  friend class ProfileValueView;
};

class ProfileValueView final {
 public:
  explicit ProfileValueView(std::string_view text);

  [[nodiscard]] std::string_view Text() const;
  [[nodiscard]] ProfileInteger AsInteger() const;

 private:
  std::string_view text_;
};

}
