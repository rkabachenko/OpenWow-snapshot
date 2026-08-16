#pragma once

#include "openwow/storage/persistence/profile_value.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::storage::persistence {

class ProfileSectionName final {
 public:
  explicit ProfileSectionName(std::string text);

  [[nodiscard]] std::string_view Text() const;
  [[nodiscard]] const char* CStr() const;

 private:
  std::string text_;
};

class ProfileKeyName final {
 public:
  explicit ProfileKeyName(std::string text);

  [[nodiscard]] std::string_view Text() const;
  [[nodiscard]] const char* CStr() const;

 private:
  std::string text_;
};

class ProfileValue final {
 public:
  explicit ProfileValue(std::string text);

  [[nodiscard]] std::string_view Text() const;
  [[nodiscard]] ProfileValueView View() const;

 private:
  std::string text_;
};

struct ProfileAssignment final {
  ProfileSectionName section;
  ProfileKeyName key;
  std::vector<ProfileValue> values;
};

class ProfileDocument final {
 public:
  [[nodiscard]] static ProfileDocument Parse(
      std::span<const std::byte> bytes);

  [[nodiscard]] const std::vector<ProfileAssignment>& Assignments() const;
  [[nodiscard]] const ProfileValue* FindFirstValue(
      std::string_view section, std::string_view key) const;

 private:
  std::vector<ProfileAssignment> assignments_;
};

}
