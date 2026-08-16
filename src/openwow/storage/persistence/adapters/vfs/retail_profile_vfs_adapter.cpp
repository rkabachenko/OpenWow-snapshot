#include "openwow/storage/persistence/profile_reader.h"

#include "openwow/core/storm_error.h"
#include "openwow/core/storm_string.h"
#include "openwow/storage/persistence/adapters/storm/retail_profile.h"
#include "openwow/storage/persistence/profile_document.h"
#include "openwow/storage/persistence/adapters/storm/retail_profile_runtime.h"
#include "openwow/vfs/sfile_core.h"

#include <cctype>
#include <cstddef>
#include <span>

#if defined(_MSC_VER)
#include <mbctype.h>
#endif

namespace openwow::storage::persistence {
namespace {

constexpr int kErrorInvalidParameter = 87;
constexpr std::size_t kProfileLoadPathChars = 260;

bool IsProfilePathWhitespace(const char character) {
#if defined(_MSC_VER)
  return _ismbcspace(static_cast<int>(character)) != 0;
#else
  const auto byte = static_cast<unsigned char>(character);
  return byte <= 0x7F && std::isspace(byte) != 0;
#endif
}

void TrimProfilePathTrailingWhitespace(char* path) {
  std::size_t length = openwow::core::SStrLen(path);
  while (length > 0 && IsProfilePathWhitespace(path[length - 1])) {
    --length;
  }
  path[length] = '\0';
}

class LoadedProfileFile final {
 public:
  LoadedProfileFile() = default;
  LoadedProfileFile(const LoadedProfileFile&) = delete;
  LoadedProfileFile& operator=(const LoadedProfileFile&) = delete;

  ~LoadedProfileFile() {
    if (data_) {
      openwow::vfs::SFileFreeLoadedData(data_);
    }
  }

  [[nodiscard]] std::span<const std::byte> Bytes() const {
    return {static_cast<const std::byte*>(data_), size_};
  }

  [[nodiscard]] bool Load(const char* path) {
    return openwow::vfs::SFileReadFileToBuffer_Wrapper(
               path, &data_, &size_, 1, 0) != 0;
  }

 private:
  void* data_ = nullptr;
  std::size_t size_ = 0;
};

bool LoadProfileFile(const char* path, LoadedProfileFile* loaded_file) {
  if (!path || !loaded_file) {
    return false;
  }

  char bounded_path[kProfileLoadPathChars]{};
  openwow::core::SStrCopy(bounded_path, path, sizeof(bounded_path));
  TrimProfilePathTrailingWhitespace(bounded_path);

  return loaded_file->Load(bounded_path);
}

}

int CProfile_LoadPath(CProfile* profile, const char* path) {
  if (!path) {
    openwow::core::SErrSetLastError(kErrorInvalidParameter);
    return 0;
  }
  return CProfile_LoadFile(path, profile);
}

int CProfile_LoadFile(const char* path, CProfile* profile) {
  LoadedProfileFile loaded_file;
  if (!LoadProfileFile(path, &loaded_file)) {
    return 0;
  }
  const ProfileDocument document = ProfileDocument::Parse(loaded_file.Bytes());
  return detail::LoadRetailProfileDocument(profile, document);
}

std::optional<ProfileValue> ReadFirstProfileValue(
    const ProfileFilePath& path, const ProfileValueQuery& query) {
  LoadedProfileFile loaded_file;
  if (!LoadProfileFile(path.CStr(), &loaded_file)) {
    return std::nullopt;
  }
  const ProfileDocument document = ProfileDocument::Parse(loaded_file.Bytes());
  const ProfileValue* value =
      document.FindFirstValue(query.section.Text(), query.key.Text());
  return value != nullptr
             ? std::optional<ProfileValue>(ProfileValue(
                   std::string(value->Text())))
             : std::nullopt;
}

}
