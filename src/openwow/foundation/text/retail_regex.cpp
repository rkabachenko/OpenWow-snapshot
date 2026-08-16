#include "openwow/foundation/text/retail_regex.h"

#include <pcre.h>

#include <array>
#include <limits>
#include <utility>

namespace openwow::foundation::text {

namespace {

constexpr int kRetailCompileOptions =
    PCRE_CASELESS | PCRE_UTF8 | PCRE_NO_UTF8_CHECK;
constexpr int kRetailExecuteOptions = PCRE_NO_UTF8_CHECK;

}

struct RetailRegex::Impl {
  explicit Impl(pcre* compiled_code) : code(compiled_code) {}
  ~Impl() { pcre_free(code); }

  pcre* code;
};

RetailRegex::RetailRegex() = default;
RetailRegex::RetailRegex(std::string pattern, std::unique_ptr<Impl> impl)
    : pattern_(std::move(pattern)), impl_(std::move(impl)) {}
RetailRegex::RetailRegex(RetailRegex&&) noexcept = default;
RetailRegex& RetailRegex::operator=(RetailRegex&&) noexcept = default;
RetailRegex::~RetailRegex() = default;

std::optional<RetailRegex> RetailRegex::Compile(std::string pattern) {

  const char* error_text = nullptr;
  int error_offset = 0;
  pcre* compiled = pcre_compile(pattern.c_str(), kRetailCompileOptions,
                                &error_text, &error_offset, nullptr);
  if (compiled == nullptr) {
    return std::nullopt;
  }

  return RetailRegex(std::move(pattern), std::make_unique<Impl>(compiled));
}

bool RetailRegex::Matches(const std::string_view subject) const {
  return Search(subject).has_value();
}

std::optional<RegexMatchSpan> RetailRegex::Search(
    const std::string_view subject, const std::size_t start_offset) const {
  if (impl_ == nullptr || start_offset > subject.size()) {
    return std::nullopt;
  }
  if (subject.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }

  std::array<int, 3> ovector{};
  const int rc =
      pcre_exec(impl_->code, nullptr, subject.data(),
                static_cast<int>(subject.size()), static_cast<int>(start_offset),
                kRetailExecuteOptions, ovector.data(),
                static_cast<int>(ovector.size()));
  if (rc < 0 || ovector[0] < 0 || ovector[1] < ovector[0]) {
    return std::nullopt;
  }

  return RegexMatchSpan{
      .start = static_cast<std::size_t>(ovector[0]),
      .end = static_cast<std::size_t>(ovector[1]),
  };
}

}
