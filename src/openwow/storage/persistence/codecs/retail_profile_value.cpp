#include "openwow/storage/persistence/profile_value.h"

#include <algorithm>

namespace openwow::storage::persistence {

ProfileInteger::ProfileInteger(const std::uint32_t value) : value_(value) {}

std::uint32_t ProfileInteger::RawValue() const {
  return value_;
}

ProfileValueView::ProfileValueView(const std::string_view text) : text_(text) {}

std::string_view ProfileValueView::Text() const {
  return text_;
}

ProfileInteger ProfileValueView::AsInteger() const {
  if (text_.starts_with('\'')) {
    std::uint32_t result = 0;
    const std::size_t end = text_.find('\'', 1);
    const std::size_t character_count =
        std::min<std::size_t>(4, end == std::string_view::npos
                                    ? text_.size() - 1
                                    : end - 1);
    for (std::size_t index = 0; index < character_count; ++index) {
      result = (result << 8) |
               static_cast<unsigned char>(text_[index + 1]);
    }
    return ProfileInteger(result);
  }

  std::size_t cursor = 0;
  const bool negative = text_.starts_with('-');
  if (negative) {
    ++cursor;
  }
  if (cursor >= text_.size()) {
    return ProfileInteger(0);
  }

  std::uint32_t result =
      static_cast<unsigned char>(text_[cursor]) -
      static_cast<unsigned char>('0');
  if (result >= 10) {
    return ProfileInteger(0);
  }

  for (++cursor; cursor < text_.size(); ++cursor) {
    const std::uint32_t digit =
        static_cast<unsigned char>(text_[cursor]) -
        static_cast<unsigned char>('0');
    if (digit >= 10) {
      break;
    }
    result = digit + 10 * result;
  }

  return ProfileInteger(negative ? 0u - result : result);
}

}
