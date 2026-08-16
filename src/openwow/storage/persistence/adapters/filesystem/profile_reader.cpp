#include "openwow/storage/persistence/profile_reader.h"

#include "openwow/platform/filesystem/filesystem.h"

#include <string>

namespace openwow::storage::persistence {

std::optional<ProfileValue> ReadFirstProfileValueFromFile(
    const ProfileFilePath& path, const ProfileValueQuery& query) {
  const auto bytes =
      openwow::platform::filesystem::ReadFileBytes(path.CStr());
  if (!bytes) {
    return std::nullopt;
  }

  const ProfileDocument document = ProfileDocument::Parse(*bytes);
  const ProfileValue* value =
      document.FindFirstValue(query.section.Text(), query.key.Text());
  return value != nullptr
             ? std::optional<ProfileValue>(ProfileValue(
                   std::string(value->Text())))
             : std::nullopt;
}

}
