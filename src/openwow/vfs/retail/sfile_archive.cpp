#include "openwow/vfs/retail/sfile_archive.h"

#include "openwow/core/storm_error.h"
#include "openwow/core/storm_path.h"
#include "openwow/core/streaming_storage.h"
#include "openwow/data/archive_system.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/data/streaming_init.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/adapters/filesystem/native_filesystem.h"
#include "openwow/vfs/adapters/mpq/mpq_archive.h"
#include "openwow/vfs/retail/archive_registry.h"
#include "openwow/vfs/retail/retail_path_resolver.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string_view>

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
#include <StormLib.h>
#endif

namespace openwow::vfs {
namespace {

const char *FindPathExtensionSegment(const char *path) {
  if (!path) return "";
  const char *extension = nullptr;
  for (const char *current = path;; ++current) {
    if (*current == '\0') return extension ? extension : current;
    if (*current == '.') extension = current + 1;
    else if (*current == '/' || *current == '\\') extension = nullptr;
  }
}

bool HasMpqExtension(const char *path) {
  return openwow::text::EqualsIgnoreCaseAscii(FindPathExtensionSegment(path), "mpq");
}

std::optional<std::string> ResolveDirectoryArchiveMemberNativePath(
    const std::string &archive_path, const char *filename) {
  if (archive_path.empty() || !filename || !*filename) return std::nullopt;
  std::array<char, 260> candidate{};
  openwow::core::CopyStormPath(candidate.data(), archive_path.c_str(), candidate.size());
  openwow::core::AppendStormPath(candidate.data(), filename, candidate.size());
  const auto resolved = ResolveExistingPathCaseInsensitive(candidate.data(),
                                                            ExistingPathRequirement::kFileOnly);
  return resolved ? std::optional<std::string>(resolved->string()) : std::nullopt;
}

std::string ResolveArchiveOpenCandidatePath(const char *path) {
  if (!path) return {};
  if (IsFilesystemQualifiedPath(path) || ResolveExistingPathInfoCaseInsensitive(path)) return path;
  std::array<char, 260> resolved{};
  if (!openwow::core::StreamingStorage::Instance().ResolveAbsolutePathFromBase(
          path, resolved.data(), resolved.size()) || resolved.front() == '\0') return path;
  return resolved.data();
}

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
bool ReadArchiveHandleBytes(HANDLE archive, const char *path,
                            std::vector<std::uint8_t> *out_bytes) {
  HANDLE file = nullptr;
  if (!archive || !path || !out_bytes ||
      !SFileOpenFileEx(archive, path, SFILE_OPEN_FROM_MPQ, &file) || !file) return false;
  DWORD high = 0;
  const DWORD low = SFileGetFileSize(file, &high);
  if (low == SFILE_INVALID_SIZE || high != 0) {
    SFileCloseFile(file);
    return false;
  }
  std::vector<std::uint8_t> bytes(low);
  DWORD read = 0;
  const bool success = low == 0 || SFileReadFile(file, bytes.data(), low, &read, nullptr) != 0;
  SFileCloseFile(file);
  if (!success) return false;
  bytes.resize(read);
  *out_bytes = std::move(bytes);
  return true;
}
#endif

bool ReadLooseFileBytes(const char *path, std::vector<std::uint8_t> *out_bytes) {
  if (!path || !out_bytes) return false;
  char resolved[260]{};
  std::int32_t type = 0;
  if (!SFileResolveFilesystemPath(path, resolved, sizeof(resolved), 0, &type) || !resolved[0])
    return false;
  std::ifstream input(ToNativePath(resolved), std::ios::binary);
  if (!input) return false;
  const std::string bytes((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());
  out_bytes->assign(bytes.begin(), bytes.end());
  return true;
}

bool ReadRegisteredArchiveFileBytes(const char *path, std::vector<std::uint8_t> *out_bytes) {
  return RetailArchiveRegistry().ReadFileBytes(path, ResolveDirectoryArchiveMemberNativePath,
                                               out_bytes);
}

bool ReadArchiveFileBytes(const char *path, std::vector<std::uint8_t> *out_bytes) {
  if (!path || !out_bytes) return false;
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (ReadRegisteredArchiveFileBytes(path, out_bytes)) return true;
  const auto *archives = openwow::data::GetArchiveSlots();
  if (!archives) return false;
  std::array<std::string, 2> candidates{std::string(path), std::string(path)};
  std::replace(candidates[1].begin(), candidates[1].end(), '/', '\\');
  for (std::size_t i = 0; i < openwow::data::GetArchiveHandleCount(); ++i) {
    if (!archives[i]) continue;
    HANDLE file = nullptr;
    bool opened = false;
    for (const auto &candidate : candidates) {
      if (SFileOpenFileEx(static_cast<HANDLE>(archives[i]), candidate.c_str(),
                          SFILE_OPEN_FROM_MPQ, &file)) {
        opened = true;
        break;
      }
    }
    if (!opened || !file) continue;
    DWORD high = 0;
    const DWORD low = SFileGetFileSize(file, &high);
    if (low == SFILE_INVALID_SIZE || high != 0) {
      SFileCloseFile(file);
      continue;
    }
    std::vector<std::uint8_t> bytes(low);
    DWORD read = 0;
    const bool success = low == 0 || SFileReadFile(file, bytes.data(), low, &read, nullptr) != 0;
    SFileCloseFile(file);
    if (!success) continue;
    bytes.resize(read);
    *out_bytes = std::move(bytes);
    return true;
  }
#else
  (void)path;
  (void)out_bytes;
#endif
  return false;
}

}

bool SFileCloseArchiveWrapped(void *archive) {
  std::unique_ptr<SArchiveHandle> handle(static_cast<SArchiveHandle *>(archive));
  const bool closed = handle && RetailArchiveRegistry().Close(handle->archive_token);
  return closed;
}

SFileArchiveLookupResult LookupRegisteredArchiveFile(const void *archive, const char *filename,
                                                      SFileArchiveLookupInfo *out_info) {
  if (archive && static_cast<const SArchiveHandle *>(archive)->type != 0) {
    if (out_info) *out_info = {};
    return SFileArchiveLookupResult::kMiss;
  }
  const auto token = archive ? std::optional<std::uint32_t>(
                                   static_cast<const SArchiveHandle *>(archive)->archive_token)
                             : std::nullopt;
  return RetailArchiveRegistry().Lookup(token, filename, ResolveDirectoryArchiveMemberNativePath,
                                         out_info);
}

bool QueryWrappedArchiveFileMetadata(const char *filename, std::string *archive_path,
                                     std::uint64_t *block_offset,
                                     std::uint32_t *compressed_size,
                                     std::uint32_t *file_flags) {
  return RetailArchiveRegistry().QueryFileMetadata(filename, ResolveDirectoryArchiveMemberNativePath,
                                                    archive_path, block_offset, compressed_size,
                                                    file_flags);
}

int ClearDefaultArchiveLookupKey() { return 0; }

bool TryOpenRawArchiveHandle(const char *path, std::int32_t priority, std::uint32_t flags,
                             void **out_handle) {
  const auto resolved = ResolveArchiveOpenCandidatePath(path);
  return RetailArchiveRegistry().OpenRawArchive(resolved.empty() ? path : resolved.c_str(),
                                                 priority, flags, out_handle);
}

bool SFileOpenArchiveWrapped(const char *path, std::int32_t priority, std::uint32_t flags,
                             void **out_handle) {
  openwow::data::ResetCurrentStreamingStatusChain();
  const auto resolved = ResolveArchiveOpenCandidatePath(path);
  const char *candidate = resolved.empty() ? path : resolved.c_str();
  auto handle = std::make_unique<SArchiveHandle>();
  *out_handle = handle.get();
  if (HasMpqExtension(candidate)) {
    void *raw = nullptr;
    if (!TryOpenRawArchiveHandle(candidate, priority, flags, &raw)) {
      *out_handle = nullptr;
      openwow::core::SErrSetLastError(openwow::core::GetStormLastError());
      return false;
    }
    handle->archive_token = RetailArchiveRegistry().RegisterMpq(raw, candidate, flags, priority);
    handle.release();
    return true;
  }
  const auto info = ResolveExistingPathInfoCaseInsensitive(candidate);
  if (!info || !info->is_directory) {
    *out_handle = nullptr;
    openwow::core::SErrSetLastError(openwow::core::GetStormLastError());
    return false;
  }
  handle->archive_token = RetailArchiveRegistry().RegisterDirectory(
      NormalizeDirectoryArchiveDisplayPath(candidate), flags, priority);
  handle.release();
  return true;
}

bool CopyWrappedArchivePathBounded(const void *archive, char *output, int capacity) {
  if (!output) return false;
  output[0] = '\0';
  const auto *handle = static_cast<const SArchiveHandle *>(archive);
  return handle && handle->type == 0 &&
         RetailArchiveRegistry().CopyPath(handle->archive_token, output, capacity);
}

bool SFileCloseArchiveRaw(void *archive) { return RetailArchiveRegistry().CloseRawArchive(archive); }

bool SFileOpenArchiveRaw_SetLastError(const char *path, std::int32_t priority,
                                      std::uint32_t flags, void **out_handle) {
  void *raw = nullptr;
  if (!TryOpenRawArchiveHandle(path, priority, flags, &raw)) {
    openwow::core::SErrSetLastError(openwow::core::GetStormLastError());
    return false;
  }
  *out_handle = raw;
  return true;
}

bool SFileOpenPatchArchiveWrapped(void *base_archive, const char *path, std::int32_t priority,
                                  int, void **out_handle) {
  *out_handle = nullptr;
  const auto *base = static_cast<const SArchiveHandle *>(base_archive);
  if (!base || base->type != 0) return false;
  std::unique_ptr<SArchiveHandle> handle;
  const auto patch = RetailArchiveRegistry().OpenPatch(
      base->archive_token, path, priority,
      [&] {
        handle = std::make_unique<SArchiveHandle>();
        *out_handle = handle.get();
      },
      [&](const std::string &directory, std::uint32_t flags) {
        return SFileOpenArchiveWrapped(directory.c_str(), priority, flags, out_handle);
      });
  if (patch.base_kind == ArchiveRegistry::PatchBaseKind::kInvalid) {
    *out_handle = nullptr;
    return false;
  }
  if (patch.base_kind == ArchiveRegistry::PatchBaseKind::kDirectory)
    return patch.directory_open_result;
  handle->archive_token = patch.archive_token;
  handle.release();
  return true;
}

bool SFileAuthenticateArchiveEx(void *archive, std::int32_t *out_result, void *modulus,
                                int modulus_size, void *exponent, int exponent_size,
                                const char *suffix) {
  const auto *handle = static_cast<const SArchiveHandle *>(archive);
  return RetailArchiveRegistry().Authenticate(handle && handle->type == 0 ? handle->archive_token : 0,
                                               out_result, modulus, modulus_size, exponent,
                                               exponent_size, suffix);
}

bool SFileAuthenticateArchive(void *archive, std::int32_t *out_result, void *modulus,
                              int modulus_size, void *exponent, int exponent_size) {
  std::int32_t local_result = 0;
  const bool ok = SFileAuthenticateArchiveEx(archive, out_result ? out_result : &local_result,
                                              modulus, modulus_size, exponent, exponent_size,
                                              "ARCHIVE");
  if (!ok) openwow::core::SErrSetLastError(openwow::core::GetStormLastError());
  return ok;
}

int SFileEnumListfile(void *archive, std::function<bool(const char *, int)> callback,
                      int user_data) {
  if (!archive || !callback) return 0;
  const auto *handle = static_cast<const SArchiveHandle *>(archive);
  if (handle->type >= 2) return 0;
  const auto listfile = RetailArchiveRegistry().ReadFile(
      handle->archive_token, "(listfile)", ResolveDirectoryArchiveMemberNativePath);
  if (!listfile) return 0;
  std::array<char, 512> line{};
  std::size_t length = 0;
  for (char ch : *listfile) {
    if (ch == '\r' || ch == '\n') {
      line[length] = '\0';
      if (length != 0) {
        if (!openwow::text::EqualsIgnoreCaseAscii(FindPathExtensionSegment(line.data()), "md5") &&
            !callback(line.data(), user_data)) break;
        length = 0;
      }
    } else if (length < 511) {
      line[length++] = ch;
    }
  }
  return 1;
}

bool SFileArchiveHasFile_SetLastErrorOnHit(const char *filename) {
  if (!ProbeArchiveFallback(filename, 0)) return false;
  openwow::core::SErrSetLastError(2);
  return true;
}

bool ReadRetailVfsFileBytes(const char *path, std::vector<std::uint8_t> *out_bytes) {
  return ReadLooseFileBytes(path, out_bytes) || ReadArchiveFileBytes(path, out_bytes);
}

bool ReadRetailArchiveListFile(const char *archive_name, std::string *out_contents) {
  if (!archive_name || !out_contents) return false;
  const auto &state = openwow::data::GetStartupFileSystemState();
  std::filesystem::path archive_path;
  if (!state.executable_base_path.empty()) archive_path = ToNativePath(state.executable_base_path.c_str());
  archive_path /= state.archive_data_path.empty() ? std::filesystem::path("Data")
                                                  : ToNativePath(state.archive_data_path.c_str());
  archive_path /= archive_name;
  archive_path.make_preferred();
  std::optional<std::string> listfile;
  if (RetailArchiveRegistry().ReadFileBySourcePath(archive_path.string().c_str(), "(listfile)",
                                                    ResolveDirectoryArchiveMemberNativePath,
                                                    &listfile)) {
    if (!listfile) return false;
    *out_contents = std::move(*listfile);
    return true;
  }
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  void *archive = nullptr;
  if (!mpq::OpenStormArchive(archive_path.string().c_str(), 0, 0, &archive) || !archive)
    return false;
  std::vector<std::uint8_t> bytes;
  const bool success = ReadArchiveHandleBytes(archive, "(listfile)", &bytes);
  SFileCloseArchive(archive);
  if (!success) return false;
  out_contents->assign(bytes.begin(), bytes.end());
  return true;
#else
  return false;
#endif
}

}
