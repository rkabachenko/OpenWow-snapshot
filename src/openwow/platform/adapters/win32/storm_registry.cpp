
#include "openwow/platform/adapters/win32/storm_registry.h"

#include "openwow/core/storm_error.h"
#include "openwow/core/storm_string.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <filesystem>
#  include <fstream>
#  include <mutex>
#  include <set>
#  include <system_error>
#  include <unordered_map>
#endif

namespace openwow::platform {
namespace {

constexpr int kErrorInvalidParameter = 87;

enum class RegistryValueType : std::uint32_t {
  kString = 1,
  kBinary = 3,
  kDword = 4,
};

struct RegistryValue {
  RegistryValueType type = RegistryValueType::kBinary;
  std::vector<std::uint8_t> bytes;
};

[[nodiscard]] bool ValidName(const char* value) {
  return value != nullptr && *value != '\0';
}

#if defined(_WIN32)
[[nodiscard]] std::string BuildWindowsSubKey(const std::uint8_t flags,
                                             const char* subkey) {
  std::string result;
  if ((flags & 0x10u) == 0) {
    result = (flags & 0x02u) != 0
                 ? "Software\\Battle.net\\"
                 : "Software\\Blizzard Entertainment\\";
  }
  result += subkey;
  return result;
}
#endif

[[nodiscard]] std::array<std::uint8_t, 4> EncodeDword(
    const std::uint32_t value) {
  return {
      static_cast<std::uint8_t>(value),
      static_cast<std::uint8_t>(value >> 8u),
      static_cast<std::uint8_t>(value >> 16u),
      static_cast<std::uint8_t>(value >> 24u),
  };
}

[[nodiscard]] std::uint32_t DecodeDword(
    const std::vector<std::uint8_t>& bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8u) |
         (static_cast<std::uint32_t>(bytes[2]) << 16u) |
         (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

#if defined(_WIN32)

[[nodiscard]] bool QueryRegistryValue(const char* subkey,
                                      const char* value_name,
                                      const std::uint8_t flags,
                                      RegistryValue* output) {
  const std::string full_key = BuildWindowsSubKey(flags, subkey);
  LSTATUS final_status = ERROR_FILE_NOT_FOUND;

  const auto query_root = [&](const HKEY root) {
    HKEY key = nullptr;
    LSTATUS status = ::RegOpenKeyExA(root, full_key.c_str(), 0,
                                     KEY_QUERY_VALUE, &key);
    if (status != ERROR_SUCCESS) {
      final_status = status;
      return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    status = ::RegQueryValueExA(key, value_name, nullptr, &type, nullptr,
                                &size);
    if (status != ERROR_SUCCESS) {
      ::RegCloseKey(key);
      final_status = status;
      return false;
    }

    std::vector<std::uint8_t> bytes(size);
    status = ::RegQueryValueExA(key, value_name, nullptr, &type,
                                bytes.empty() ? nullptr : bytes.data(), &size);
    ::RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
      final_status = status;
      return false;
    }
    bytes.resize(size);
    output->type = static_cast<RegistryValueType>(type);
    output->bytes = std::move(bytes);
    return true;
  };

  if ((flags & 0x04u) == 0 && query_root(HKEY_CURRENT_USER)) {
    return true;
  }
  if ((flags & 0x01u) == 0 && query_root(HKEY_LOCAL_MACHINE)) {
    return true;
  }

  core::SErrSetLastError(static_cast<int>(final_status));
  return false;
}

[[nodiscard]] bool WriteRegistryValue(const char* subkey,
                                      const char* value_name,
                                      const std::uint8_t flags,
                                      const RegistryValue& value) {
  const std::string full_key = BuildWindowsSubKey(flags, subkey);
  const HKEY root = (flags & 0x04u) != 0 ? HKEY_LOCAL_MACHINE
                                         : HKEY_CURRENT_USER;
  HKEY key = nullptr;
  DWORD disposition = 0;
  LSTATUS status = ::RegCreateKeyExA(root, full_key.c_str(), 0, nullptr, 0,
                                     KEY_SET_VALUE, nullptr, &key,
                                     &disposition);
  if (status != ERROR_SUCCESS) {
    core::SErrSetLastError(static_cast<int>(status));
    return false;
  }

  status = ::RegSetValueExA(
      key, value_name, 0, static_cast<DWORD>(value.type),
      value.bytes.empty() ? nullptr : value.bytes.data(),
      static_cast<DWORD>(value.bytes.size()));
  if (status == ERROR_SUCCESS && (flags & 0x08u) != 0) {
    status = ::RegFlushKey(key);
  }
  const LSTATUS close_status = ::RegCloseKey(key);
  if (status == ERROR_SUCCESS) {
    status = close_status;
  }
  if (status != ERROR_SUCCESS) {
    core::SErrSetLastError(static_cast<int>(status));
    return false;
  }
  return true;
}

#else

namespace fs = std::filesystem;

enum class RegistryScope {
  kCurrentUser,
  kLocalMachine,
  kBattleNet,
};

constexpr std::size_t kMaxPreferenceKeyBytes = 0x3ffu;
constexpr std::size_t kMaxPreferenceValueBytes = 64u * 1024u * 1024u;

struct PortableRegistryEntry {
  std::string logical_key;
  fs::path preference_file;
  RegistryValue value;
};

#if !defined(__APPLE__)

[[nodiscard]] std::string FoldCase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const char ch) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch)));
  });
  return value;
}
#endif

[[nodiscard]] RegistryScope ScopeForFlags(const std::uint8_t flags) {
  if ((flags & 0x02u) != 0) {
    return RegistryScope::kBattleNet;
  }
  if ((flags & 0x04u) != 0) {
    return RegistryScope::kLocalMachine;
  }
  return RegistryScope::kCurrentUser;
}

[[nodiscard]] const char* ScopeName(const RegistryScope scope) {
  switch (scope) {
    case RegistryScope::kCurrentUser:
      return "Current User";
    case RegistryScope::kLocalMachine:
      return "Local Machine";
    case RegistryScope::kBattleNet:
      return "Battle.net";
  }
  return "Current User";
}

#if !defined(__APPLE__)
[[nodiscard]] const char* PortableScopeDirectory(const RegistryScope scope) {
  switch (scope) {
    case RegistryScope::kCurrentUser:
      return "current-user";
    case RegistryScope::kLocalMachine:
      return "local-machine";
    case RegistryScope::kBattleNet:
      return "battle-net";
  }
  return "current-user";
}
#endif

[[nodiscard]] std::string ProductName(const char* subkey) {
  std::string product(subkey);
  if (const std::size_t separator = product.find('\\');
      separator != std::string::npos) {
    product.resize(separator);
  }
  for (char& ch : product) {
    const auto byte = static_cast<unsigned char>(ch);
    if (ch == '/' || ch == '\\' || byte < 0x20u) {
      ch = '_';
    }
  }
#if !defined(__APPLE__)

  return FoldCase(std::move(product));
#else
  return product;
#endif
}

[[nodiscard]] std::string BuildLogicalKey(const char* subkey,
                                          const char* value_name,
                                          const RegistryScope scope) {
  std::string result(ScopeName(scope));
  result.push_back('\\');
  result += subkey;
  result.push_back('\\');
  result += value_name;
  return result;
}

struct RegistryKeyHash {
  [[nodiscard]] std::size_t operator()(const std::string& key) const noexcept {

    return core::SStrHashCI(key.c_str());
  }
};

struct RegistryKeyEqual {
  [[nodiscard]] bool operator()(const std::string& left,
                                const std::string& right) const noexcept {

    return core::SStrCmpNoCase(
               left.c_str(), right.c_str(),
               static_cast<std::size_t>(
                   std::numeric_limits<std::int32_t>::max())) == 0;
  }
};

[[nodiscard]] int HexValue(const char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return ch - 'a' + 10;
  }
  if (ch >= 'A' && ch <= 'F') {
    return ch - 'A' + 10;
  }
  return 0;
}

[[nodiscard]] std::vector<std::uint8_t> DecodePreferenceBytes(
    const std::string& line) {
  std::vector<std::uint8_t> output;
  output.reserve(line.size());
  bool hex_run_open = false;
  bool run_has_digit = false;
  bool expect_high_nibble = true;
  for (const char character : line) {
    if (character == '%') {

      if (hex_run_open && !run_has_digit) {
        output.push_back(static_cast<std::uint8_t>('%'));
      }
      hex_run_open = !hex_run_open;
      run_has_digit = false;
      expect_high_nibble = true;
      continue;
    }
    if (!hex_run_open) {
      output.push_back(static_cast<std::uint8_t>(character));
      continue;
    }

    const auto nibble = static_cast<std::uint8_t>(HexValue(character));
    if (expect_high_nibble) {
      output.push_back(static_cast<std::uint8_t>(nibble << 4u));
    } else {
      output.back() = static_cast<std::uint8_t>(output.back() | nibble);
    }
    run_has_digit = true;
    expect_high_nibble = !expect_high_nibble;
  }
  return output;
}

[[nodiscard]] std::string EncodePreferenceBytes(
    const std::uint8_t* bytes, const std::size_t size) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string output;
  if (size <= (std::numeric_limits<std::size_t>::max() - 2u) / 2u) {
    output.reserve(size * 2u + 2u);
  }
  bool hex_run_open = false;
  for (std::size_t index = 0; index < size; ++index) {
    const std::uint8_t byte = bytes[index];
    const bool printable = byte >= 0x20u && byte < 0x80u && byte != '%';

    if (hex_run_open) {
      const bool next_printable =
          index + 1u == size ||
          (bytes[index + 1u] >= 0x20u && bytes[index + 1u] < 0x80u &&
           bytes[index + 1u] != '%');
      if (printable && next_printable) {
        output.push_back('%');
        hex_run_open = false;
        output.push_back(static_cast<char>(byte));
      } else {
        output.push_back(kHex[byte >> 4u]);
        output.push_back(kHex[byte & 0x0fu]);
      }
      continue;
    }

    if (byte == '%') {
      output += "%%";
    } else if (printable) {
      output.push_back(static_cast<char>(byte));
    } else {
      output.push_back('%');
      hex_run_open = true;
      output.push_back(kHex[byte >> 4u]);
      output.push_back(kHex[byte & 0x0fu]);
    }
  }
  if (hex_run_open) {
    output.push_back('%');
  }
  return output;
}

enum class PreferenceLineResult {
  kComplete,
  kEndOfFile,
  kTooLong,
};

[[nodiscard]] PreferenceLineResult ReadPreferenceLine(
    std::istream& input, std::string& output, const std::size_t maximum_size,
    const bool accept_final_line) {
  output.clear();
  bool too_long = false;
  for (;;) {
    const int next = input.get();
    if (next == std::char_traits<char>::eof()) {
      if (accept_final_line) {
        return too_long ? PreferenceLineResult::kTooLong
                        : PreferenceLineResult::kComplete;
      }
      return PreferenceLineResult::kEndOfFile;
    }
    if (next == '\n') {
      return too_long ? PreferenceLineResult::kTooLong
                      : PreferenceLineResult::kComplete;
    }
    if (output.size() < maximum_size) {
      output.push_back(static_cast<char>(next));
    } else {
      too_long = true;
    }
  }
}

class PortableRegistryStore {
 public:
  ~PortableRegistryStore() {
    std::lock_guard lock(mutex_);
    (void)FlushAllLocked();
  }

  [[nodiscard]] bool Query(const char* subkey, const char* value_name,
                           const std::uint8_t flags,
                           RegistryValue* output) {
    std::lock_guard lock(mutex_);
    const RegistryScope scope = ScopeForFlags(flags);
    const fs::path file = PreferenceFile(subkey, scope);
    LoadFileLocked(file);
    const auto found = entries_.find(
        BuildLogicalKey(subkey, value_name, scope));
    if (found == entries_.end()) {
      return false;
    }
    *output = found->second.value;
    return true;
  }

  [[nodiscard]] bool Write(const char* subkey, const char* value_name,
                           const std::uint8_t flags,
                           RegistryValue value) {
    std::lock_guard lock(mutex_);
    const RegistryScope scope = ScopeForFlags(flags);
    const fs::path file = PreferenceFile(subkey, scope);
    LoadFileLocked(file);

    PortableRegistryEntry entry;
    entry.logical_key = BuildLogicalKey(subkey, value_name, scope);
    entry.preference_file = file;
    entry.value = std::move(value);
    const std::string lookup_key = entry.logical_key;
    entries_.insert_or_assign(lookup_key, std::move(entry));
    dirty_files_.insert(file);

    return true;
  }

  [[nodiscard]] bool FlushAll() {
    std::lock_guard lock(mutex_);
    return FlushAllLocked();
  }

  void SetRootForTests(const char* root) {
    std::lock_guard lock(mutex_);
    (void)FlushAllLocked();
    entries_.clear();
    loaded_files_.clear();
    dirty_files_.clear();
    root_override_ = root ? fs::path(root) : fs::path();
  }

  void ResetCacheForTests() {
    std::lock_guard lock(mutex_);
    (void)FlushAllLocked();
    entries_.clear();
    loaded_files_.clear();
    dirty_files_.clear();
  }

  [[nodiscard]] std::string PreferencePathForTests(
      const char* subkey, const std::uint8_t flags) {
    std::lock_guard lock(mutex_);
    return PreferenceFile(subkey, ScopeForFlags(flags)).string();
  }

 private:
  [[nodiscard]] fs::path PortableStorageRoot() const {
    if (!root_override_.empty()) {
      return root_override_;
    }
    if (const char* explicit_root = std::getenv("OPENWOW_REGISTRY_DIR")) {
      if (*explicit_root != '\0') {
        return explicit_root;
      }
    }
#if defined(__APPLE__)

#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
      if (*xdg != '\0') {
        return fs::path(xdg) / "openwow" / "registry";
      }
    }
    if (const char* home = std::getenv("HOME")) {
      if (*home != '\0') {
        return fs::path(home) / ".config" / "openwow" / "registry";
      }
    }
#endif
    std::error_code error;
    fs::path root = fs::temp_directory_path(error);
    if (error) {
      root = fs::current_path(error);
    }
    return root / "openwow-registry";
  }

  [[nodiscard]] fs::path PreferenceFile(const char* subkey,
                                        const RegistryScope scope) const {
    const fs::path filename =
        "com.blizzard." + ProductName(subkey) + ".prefs";
#if defined(__APPLE__)
    if (!root_override_.empty()) {
      switch (scope) {
        case RegistryScope::kCurrentUser:
          return root_override_ / filename;
        case RegistryScope::kLocalMachine:
          return root_override_ / "Blizzard" / filename;
        case RegistryScope::kBattleNet:
          return root_override_ / "Battle.net" / filename;
      }
    }
    if (scope == RegistryScope::kCurrentUser) {
      if (const char* home = std::getenv("HOME"); home && *home != '\0') {
        return fs::path(home) / "Library" / "Preferences" / filename;
      }
      return PortableStorageRoot() / filename;
    }
    const fs::path shared_root = "/Users/Shared";
    return shared_root /
           (scope == RegistryScope::kBattleNet ? "Battle.net" : "Blizzard") /
           filename;
#else
    return PortableStorageRoot() / PortableScopeDirectory(scope) / filename;
#endif
  }

  void LoadFileLocked(const fs::path& file) {
    if (!loaded_files_.insert(file).second) {
      return;
    }
    std::ifstream input(file, std::ios::binary);
    if (!input) {
      return;
    }

    for (;;) {
      const int marker_value = input.get();
      if (marker_value == std::char_traits<char>::eof()) {
        break;
      }
      const char marker = static_cast<char>(marker_value);
      std::string logical_key;
      const PreferenceLineResult key_result = ReadPreferenceLine(
          input, logical_key, kMaxPreferenceKeyBytes, false);
      if (key_result == PreferenceLineResult::kEndOfFile) {
        break;
      }
      if (marker != '$' && marker != '@' && marker != '#') {
        continue;
      }

      std::string value_line;
      const PreferenceLineResult value_result = ReadPreferenceLine(
          input, value_line, kMaxPreferenceValueBytes, true);
      if (value_result == PreferenceLineResult::kEndOfFile) {
        break;
      }

      if (key_result == PreferenceLineResult::kTooLong ||
          value_result == PreferenceLineResult::kTooLong) {
        continue;
      }

      PortableRegistryEntry entry;
      entry.logical_key = std::move(logical_key);
      entry.preference_file = file;
      if (marker == '#') {
        char* end = nullptr;
        errno = 0;
        const long parsed = std::strtol(value_line.c_str(), &end, 10);
        if (end == value_line.c_str()) {
          continue;
        }
        const auto bytes = EncodeDword(static_cast<std::uint32_t>(parsed));
        entry.value.type = RegistryValueType::kDword;
        entry.value.bytes.assign(bytes.begin(), bytes.end());
      } else {
        entry.value.type = marker == '$' ? RegistryValueType::kString
                                         : RegistryValueType::kBinary;
        entry.value.bytes = DecodePreferenceBytes(value_line);
        if (marker == '$') {
          entry.value.bytes.push_back(0);
        }
      }
      const std::string lookup_key = entry.logical_key;
      entries_.insert_or_assign(lookup_key, std::move(entry));
    }
  }

  [[nodiscard]] bool FlushFileLocked(const fs::path& file) {
    if (!dirty_files_.contains(file)) {
      return true;
    }

    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    if (error) {
      core::SErrSetLastError(error.value());
      return false;
    }

    fs::path temporary = file;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      core::SErrSetLastError(errno != 0 ? errno : EIO);
      return false;
    }

    std::vector<const PortableRegistryEntry*> ordered_entries;
    ordered_entries.reserve(entries_.size());
    for (const auto& [lookup_key, entry] : entries_) {
      (void)lookup_key;
      if (entry.preference_file == file) {
        ordered_entries.push_back(&entry);
      }
    }
    std::sort(ordered_entries.begin(), ordered_entries.end(),
              [](const PortableRegistryEntry* left,
                 const PortableRegistryEntry* right) {
                return left->logical_key < right->logical_key;
              });
    for (const PortableRegistryEntry* entry_ptr : ordered_entries) {
      const PortableRegistryEntry& entry = *entry_ptr;
      switch (entry.value.type) {
        case RegistryValueType::kString: {
          output << '$' << entry.logical_key << '\n';
          std::size_t size = entry.value.bytes.size();
          if (size != 0 && entry.value.bytes.back() == 0) {
            --size;
          }
          output << EncodePreferenceBytes(entry.value.bytes.data(), size)
                 << '\n';
          break;
        }
        case RegistryValueType::kBinary:
          output << '@' << entry.logical_key << '\n'
                 << EncodePreferenceBytes(entry.value.bytes.data(),
                                          entry.value.bytes.size())
                 << '\n';
          break;
        case RegistryValueType::kDword:
          if (entry.value.bytes.size() < 4) {
            continue;
          }
          output << '#' << entry.logical_key << '\n'
                 << static_cast<std::int32_t>(DecodeDword(entry.value.bytes))
                 << '\n';
          break;
      }
    }
    output.flush();
    if (!output) {
      output.close();
      fs::remove(temporary, error);
      core::SErrSetLastError(EIO);
      return false;
    }
    output.close();

    fs::rename(temporary, file, error);
    if (error) {
      const int error_value = error.value();
      std::error_code cleanup_error;
      fs::remove(temporary, cleanup_error);
      core::SErrSetLastError(error_value);
      return false;
    }
    dirty_files_.erase(file);
    return true;
  }

  [[nodiscard]] bool FlushAllLocked() {
    bool success = true;
    const std::vector<fs::path> dirty(dirty_files_.begin(),
                                      dirty_files_.end());
    for (const fs::path& file : dirty) {
      success = FlushFileLocked(file) && success;
    }
    return success;
  }

  std::mutex mutex_;
  std::unordered_map<std::string, PortableRegistryEntry,
                     RegistryKeyHash, RegistryKeyEqual> entries_;
  std::set<fs::path> loaded_files_;
  std::set<fs::path> dirty_files_;
  fs::path root_override_;
};

PortableRegistryStore& RegistryStore() {
  static PortableRegistryStore store;
  return store;
}

[[nodiscard]] bool QueryRegistryValue(const char* subkey,
                                      const char* value_name,
                                      const std::uint8_t flags,
                                      RegistryValue* output) {
  return RegistryStore().Query(subkey, value_name, flags, output);
}

[[nodiscard]] bool WriteRegistryValue(const char* subkey,
                                      const char* value_name,
                                      const std::uint8_t flags,
                                      RegistryValue value) {
  return RegistryStore().Write(subkey, value_name, flags, std::move(value));
}

#endif

}

int WriteRegistryStringValue(const char* subkey, const char* value_name,
                             const std::uint8_t flags, const char* data) {
  if (!ValidName(subkey) || !ValidName(value_name) || data == nullptr) {
    core::SErrSetLastError(kErrorInvalidParameter);
    return 0;
  }

  RegistryValue value;
  value.type = RegistryValueType::kString;
  const std::size_t size = std::strlen(data) + 1u;
  value.bytes.assign(reinterpret_cast<const std::uint8_t*>(data),
                     reinterpret_cast<const std::uint8_t*>(data) + size);
  return WriteRegistryValue(subkey, value_name, flags, std::move(value)) ? 1
                                                                         : 0;
}

int WriteRegistryValue(const char* subkey, const char* value_name,
                       const std::uint8_t flags, const std::uint32_t data) {
  if (!ValidName(subkey) || !ValidName(value_name)) {
    core::SErrSetLastError(kErrorInvalidParameter);
    return 0;
  }

  RegistryValue value;
  value.type = RegistryValueType::kDword;
  const auto bytes = EncodeDword(data);
  value.bytes.assign(bytes.begin(), bytes.end());
  return WriteRegistryValue(subkey, value_name, flags, std::move(value)) ? 1
                                                                         : 0;
}

int WriteRegistryBinaryValue(const char* subkey, const char* value_name,
                             const std::uint8_t flags, const void* data,
                             const std::uint32_t size) {
  if (!ValidName(subkey) || !ValidName(value_name) ||
      (data == nullptr && size != 0)) {
    core::SErrSetLastError(kErrorInvalidParameter);
    return 0;
  }

  RegistryValue value;
  value.type = RegistryValueType::kBinary;
  if (size != 0) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    value.bytes.assign(bytes, bytes + size);
  }
  return WriteRegistryValue(subkey, value_name, flags, std::move(value)) ? 1
                                                                         : 0;
}

int ReadRegistryStringValue(const char* subkey, const char* value_name,
                            const std::uint8_t flags, char* buffer,
                            const std::uint32_t buffer_size) {
  if (!ValidName(subkey) || !ValidName(value_name) || buffer == nullptr ||
      buffer_size == 0) {
    core::SErrSetLastError(kErrorInvalidParameter);
    return 0;
  }

  RegistryValue value;
  if (!QueryRegistryValue(subkey, value_name, flags, &value) ||
      value.type != RegistryValueType::kString) {
    return 0;
  }

  const std::size_t source_size =
      std::find(value.bytes.begin(), value.bytes.end(), 0) -
      value.bytes.begin();
  const std::size_t copy_size =
      std::min<std::size_t>(source_size, buffer_size - 1u);
  if (copy_size != 0) {
    std::memcpy(buffer, value.bytes.data(), copy_size);
  }
  buffer[copy_size] = '\0';
  return 1;
}

int ReadRegistryValue(const char* subkey, const char* value_name,
                      const std::uint8_t flags, std::uint32_t* output) {
  if (!ValidName(subkey) || !ValidName(value_name) || output == nullptr) {
    core::SErrSetLastError(kErrorInvalidParameter);
    return 0;
  }

  RegistryValue value;
  if (!QueryRegistryValue(subkey, value_name, flags, &value) ||
      value.type != RegistryValueType::kDword || value.bytes.size() < 4) {
    return 0;
  }
  *output = DecodeDword(value.bytes);
  return 1;
}

int FlushRegistryValues() {
#if defined(_WIN32)
  return 1;
#else
  return RegistryStore().FlushAll() ? 1 : 0;
#endif
}

namespace detail {

void SetRegistryStorageRootForTests(const char* root) {
#if !defined(_WIN32)
  RegistryStore().SetRootForTests(root);
#else
  (void)root;
#endif
}

void ResetRegistryCacheForTests() {
#if !defined(_WIN32)
  RegistryStore().ResetCacheForTests();
#endif
}

std::string RegistryPreferencePathForTests(const char* subkey,
                                           const std::uint8_t flags) {
#if !defined(_WIN32)
  return RegistryStore().PreferencePathForTests(subkey, flags);
#else
  (void)subkey;
  (void)flags;
  return {};
#endif
}

}
}
