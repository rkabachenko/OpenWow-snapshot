#include "openwow/storage/persistence/profile_paths.h"

#include "openwow/storage/persistence/profile_reader.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#if defined(_MSC_VER)

#include <mbstring.h>
#endif

namespace openwow::storage::persistence {
namespace {

bool IsRetailPathWhitespace(const char character) {
#if defined(_MSC_VER)
  return _ismbcspace(static_cast<int>(character)) != 0;
#else
  const auto byte = static_cast<unsigned char>(character);
  return byte <= 0x7F && std::isspace(byte) != 0;
#endif
}

}

ProfileFilePath::ProfileFilePath(const std::string_view text) {
  size_ = std::min(text.size(), text_.size() - 1);
  std::memcpy(text_.data(), text.data(), size_);
  while (size_ > 0 && IsRetailPathWhitespace(text_[size_ - 1])) {
    --size_;
  }
  text_[size_] = '\0';
}

std::string_view ProfileFilePath::Text() const {
  return {text_.data(), size_};
}

const char* ProfileFilePath::CStr() const {
  return text_.data();
}

std::filesystem::path GetDefaultProfileRoot() {
  return std::filesystem::path("config") / "profile";
}

std::filesystem::path GetConfigPath() {
  return std::filesystem::path("config") / "openwow.json";
}

}
