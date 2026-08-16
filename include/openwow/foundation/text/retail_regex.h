#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::foundation::text {

struct RegexMatchSpan {
  std::size_t start{0};
  std::size_t end{0};

  [[nodiscard]] std::size_t length() const noexcept {
    return end - start;
  }
};

class RetailRegex {
 public:
  RetailRegex();
  RetailRegex(const RetailRegex&) = delete;
  RetailRegex& operator=(const RetailRegex&) = delete;
  RetailRegex(RetailRegex&&) noexcept;
  RetailRegex& operator=(RetailRegex&&) noexcept;
  ~RetailRegex();

  [[nodiscard]] static std::optional<RetailRegex> Compile(
      std::string pattern);

  [[nodiscard]] const std::string &pattern() const {
    return pattern_;
  }
  [[nodiscard]] bool Matches(std::string_view subject) const;
  [[nodiscard]] std::optional<RegexMatchSpan> Search(
      std::string_view subject, std::size_t start_offset = 0) const;

 private:
  struct Impl;
  RetailRegex(std::string pattern, std::unique_ptr<Impl> impl);

  std::string pattern_;
  std::unique_ptr<Impl> impl_;
};

}
