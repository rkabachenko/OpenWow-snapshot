#pragma once

#include "openwow/storage/persistence/profile_document.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace openwow::storage::persistence {

class ProfileFilePath final {
 public:
  explicit ProfileFilePath(std::string_view text);

  [[nodiscard]] std::string_view Text() const;
 [[nodiscard]] const char* CStr() const;

 private:
  static constexpr std::size_t kCapacity = 260;

  std::array<char, kCapacity> text_{};
  std::size_t size_ = 0;
};

struct ProfileValueQuery final {
  ProfileSectionName section;
  ProfileKeyName key;
};

[[nodiscard]] std::optional<ProfileValue> ReadFirstProfileValue(
    const ProfileFilePath& path, const ProfileValueQuery& query);

[[nodiscard]] std::optional<ProfileValue> ReadFirstProfileValueFromFile(
    const ProfileFilePath& path, const ProfileValueQuery& query);

}
