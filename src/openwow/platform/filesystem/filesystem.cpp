#include "openwow/platform/filesystem/filesystem.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace openwow::platform::filesystem {

namespace {

std::filesystem::path MakeAtomicWriteTemporaryPath(
    const std::filesystem::path& destination) {
  static std::atomic<std::uint64_t> sequence{0};
  const auto clock_tick = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto unique_id = sequence.fetch_add(1, std::memory_order_relaxed);
  return destination.parent_path() /
         (destination.filename().string() + ".tmp." + std::to_string(clock_tick) + "." +
          std::to_string(unique_id));
}

bool ReplaceFileAtomically(const std::filesystem::path& source,
                           const std::filesystem::path& destination) {
#if defined(_WIN32)
  if (::GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES &&
      ::ReplaceFileW(destination.c_str(), source.c_str(), nullptr,
                     REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE) {
    return true;
  }
  return ::MoveFileExW(source.c_str(), destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else

  std::error_code permission_ec;
  const auto destination_status = std::filesystem::status(destination, permission_ec);
  if (!permission_ec && std::filesystem::exists(destination_status)) {
    std::filesystem::permissions(source, destination_status.permissions(),
                                 std::filesystem::perm_options::replace,
                                 permission_ec);
    if (permission_ec) {
      return false;
    }
  }

  std::error_code ec;
  std::filesystem::rename(source, destination, ec);
  return !ec;
#endif
}

bool SyncFileToStableStorage(const std::filesystem::path& path) {
#if defined(_WIN32)

  return true;
#else
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    return false;
  }

  int result = -1;
#if defined(F_FULLFSYNC)
  do {
    result = ::fcntl(fd, F_FULLFSYNC);
  } while (result != 0 && errno == EINTR);
#endif
  if (result != 0) {
    do {
      result = ::fsync(fd);
    } while (result != 0 && errno == EINTR);
  }
  const int close_result = ::close(fd);
  return result == 0 && close_result == 0;
#endif
}

bool SyncParentDirectoryToStableStorage(const std::filesystem::path& path) {
#if defined(_WIN32)
  return true;
#else
  const std::filesystem::path parent =
      path.parent_path().empty() ? std::filesystem::path(".")
                                 : path.parent_path();
  int flags = O_RDONLY;
#if defined(O_DIRECTORY)
  flags |= O_DIRECTORY;
#endif
  const int fd = ::open(parent.c_str(), flags);
  if (fd < 0) {
    return false;
  }
  int result;
  do {
    result = ::fsync(fd);
  } while (result != 0 && errno == EINTR);
  const int sync_error = result == 0 ? 0 : errno;
  const int close_result = ::close(fd);

  return (result == 0 && close_result == 0) ||
         (result != 0 &&
          (sync_error == EINVAL || sync_error == ENOTSUP));
#endif
}

unsigned char FoldAscii(const unsigned char ch) {
  return ch >= 'A' && ch <= 'Z'
             ? static_cast<unsigned char>(ch + ('a' - 'A'))
             : ch;
}

bool EqualsIgnoreCaseAscii(const std::string_view lhs,
                           const std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (FoldAscii(static_cast<unsigned char>(lhs[i])) !=
        FoldAscii(static_cast<unsigned char>(rhs[i]))) {
      return false;
    }
  }
  return true;
}

}

bool AtomicWriteFile(const std::filesystem::path& path, const std::string& content) {
  const auto tmp = MakeAtomicWriteTemporaryPath(path);
  std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    return false;
  }

  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  out.flush();
  out.close();
  const bool wrote_complete_file = out.good();

  if (!wrote_complete_file || !SyncFileToStableStorage(tmp) ||
      !ReplaceFileAtomically(tmp, path)) {
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return false;
  }

  return SyncParentDirectoryToStableStorage(path);
}

std::optional<std::vector<std::byte>> ReadFileBytes(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input.is_open()) {
    return std::nullopt;
  }

  const std::streampos end = input.tellg();
  if (end < 0) {
    return std::nullopt;
  }

  std::vector<std::byte> bytes(static_cast<std::size_t>(end));
  input.seekg(0, std::ios::beg);
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
      return std::nullopt;
    }
  }
  return bytes;
}

bool IsSafePathComponent(const std::string_view component) {
  if (component.empty() || component == "." || component == "..") {
    return false;
  }
  for (const unsigned char ch : component) {
    if (ch == 0 || ch == '/' || ch == '\\' || ch == ':') {
      return false;
    }
  }
  return true;
}

std::optional<std::string> ResolveExistingPathComponentCaseInsensitive(
    const std::filesystem::path& parent, const std::string_view requested) {
  if (!IsSafePathComponent(requested)) {
    return std::nullopt;
  }

  std::error_code ec;
  if (!std::filesystem::exists(parent, ec)) {
    return ec ? std::nullopt
              : std::optional<std::string>(std::string(requested));
  }
  if (ec || !std::filesystem::is_directory(parent, ec) || ec) {
    return std::nullopt;
  }

  std::optional<std::string> folded_match;
  for (std::filesystem::directory_iterator it(parent, ec), end;
       !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    if (name == requested) {
      return name;
    }
    if (EqualsIgnoreCaseAscii(name, requested) &&
        (!folded_match.has_value() || name < *folded_match)) {
      folded_match = name;
    }
  }
  if (ec) {
    return std::nullopt;
  }
  return folded_match.has_value()
             ? folded_match
             : std::optional<std::string>(std::string(requested));
}

bool CopyFilePath(const std::filesystem::path& source,
                  const std::filesystem::path& destination,
                  bool overwrite) {
  std::error_code ec;
  const auto options = overwrite ? std::filesystem::copy_options::overwrite_existing
                                 : std::filesystem::copy_options::none;
  return std::filesystem::copy_file(source, destination, options, ec) && !ec;
}

bool PathIsRegularFile(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec) && !ec;
}

bool MovePathNoReplace(const std::filesystem::path& source,
                       const std::filesystem::path& destination) {
  std::error_code ec;
  if (std::filesystem::exists(destination, ec) || ec) {
    return false;
  }

  std::filesystem::rename(source, destination, ec);
  return !ec;
}

bool RecursiveCopyDirectory(const std::filesystem::path& source,
                            const std::filesystem::path& destination,
                            bool overwrite) {
  std::error_code ec;
  if (!std::filesystem::exists(source, ec) || !std::filesystem::is_directory(source, ec)) {
    return false;
  }
  std::filesystem::create_directories(destination, ec);
  if (ec) {
    return false;
  }

  for (const auto& entry : std::filesystem::recursive_directory_iterator(source, ec)) {
    if (ec) {
      return false;
    }
    const auto relative = std::filesystem::relative(entry.path(), source, ec);
    if (ec) {
      return false;
    }
    const auto target = destination / relative;

    if (entry.is_directory()) {
      std::filesystem::create_directories(target, ec);
      if (ec) {
        return false;
      }
      continue;
    }

    if (entry.is_regular_file()) {
      const auto options = overwrite ? std::filesystem::copy_options::overwrite_existing
                                     : std::filesystem::copy_options::skip_existing;
      std::filesystem::copy_file(entry.path(), target, options, ec);
      if (ec) {
        return false;
      }
    }
  }
  return true;
}

bool IsSafeChildPath(const std::filesystem::path& root, const std::filesystem::path& candidate) {
  std::error_code ec;
  const auto canonical_root = std::filesystem::weakly_canonical(root, ec);
  if (ec) {
    return false;
  }
  const auto canonical_candidate = std::filesystem::weakly_canonical(candidate, ec);
  if (ec) {
    return false;
  }
  const auto rel = std::filesystem::relative(canonical_candidate, canonical_root, ec);
  if (ec) {
    return false;
  }
  const std::string relative = rel.generic_string();
  return !relative.empty() && relative != "." && relative != ".." &&
         relative.rfind("../", 0) != 0;
}

}
