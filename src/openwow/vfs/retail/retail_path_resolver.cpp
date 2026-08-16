#include "openwow/vfs/retail/retail_path_resolver.h"

#include "openwow/core/storm_error.h"
#include "openwow/core/storm_path.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/adapters/filesystem/native_filesystem.h"
#include "openwow/vfs/retail/sfile_archive.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace openwow::vfs {
namespace {

enum class SearchCandidate : std::uint8_t {
  kBasePathFile,
  kBasePathTailFile,
  kBaseDataFile,
  kBaseDataBz,
  kLooseDataFile,
  kLooseDataBz,
  kBaseDataMpq,
  kLocaleDataFile,
};
enum class ProbeResult : std::uint8_t { kMiss, kMatch, kTerminalReject };
enum class SearchResult : std::uint8_t { kMiss, kMatch, kTerminalReject };

struct ManifestRoot {
  std::string display_root;
  std::string skipped_relative_subtree;
};
struct LooseManifestCache {
  std::mutex mutex;
  std::unordered_map<std::string, LooseFileManifestCacheEntry> entries;
  bool initialized = false;
};
struct CanonicalPathCache {
  std::mutex mutex;
  std::unordered_map<std::string, std::string> entries;
};

LooseManifestCache &LooseCache() { static LooseManifestCache cache; return cache; }
CanonicalPathCache &CanonicalCache() { static CanonicalPathCache cache; return cache; }
std::function<bool(const char *, std::uint8_t)> &ArchiveProbeForTests() {
  static std::function<bool(const char *, std::uint8_t)> probe;
  return probe;
}

std::string FoldManifestKey(std::string_view value) {
  std::string key(value);
  std::replace(key.begin(), key.end(), '/', '\\');
  return openwow::text::ToLowerAscii(std::move(key));
}

std::string EnsureTrailingSeparator(std::string value) {
  if (!value.empty() && value.back() != '\\' && value.back() != '/') value.push_back('\\');
  return value;
}
std::string JoinStormPaths(std::string_view left, std::string_view right) {
  std::array<char, 260> result{};
  const std::string left_string(left), right_string(right);
  openwow::core::JoinStormPathBounded(result.data(), result.size(), left_string.c_str(),
                                      right_string.c_str());
  return result.data();
}
std::optional<std::string> RelativeStormPath(const std::filesystem::path &root,
                                              const std::filesystem::path &entry,
                                              bool trailing = false) {
  std::error_code ec;
  const auto relative = std::filesystem::relative(entry, root, ec);
  if (ec) return std::nullopt;
  std::string result = ToStormPathString(relative);
  return trailing ? EnsureTrailingSeparator(std::move(result)) : result;
}
std::vector<ManifestRoot> BuildManifestRoots() {
  const auto &state = openwow::data::GetStartupFileSystemState();
  std::string base = ".";
  if (!state.executable_base_path.empty()) {
    if (const auto resolved = ResolveExistingPathCaseInsensitive(
            state.executable_base_path.c_str(), ExistingPathRequirement::kDirectoryOnly))
      base = ToStormPathString(*resolved);
  }
  std::vector<ManifestRoot> roots{{base, EnsureTrailingSeparator(state.archive_data_path)}};
  if (!state.archive_data_path.empty()) roots.push_back({JoinStormPaths(base, state.archive_data_path), {}});
  if (!state.locale_data_path.empty()) roots.push_back({state.locale_data_path, {}});
  return roots;
}
void IndexManifestRoot(const ManifestRoot &root, LooseManifestCache &cache) {
  const auto actual = ResolveExistingPathCaseInsensitive(root.display_root.c_str(),
                                                          ExistingPathRequirement::kDirectoryOnly);
  if (!actual) return;
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(*actual, ec), end; !ec && it != end;
       it.increment(ec)) {
    const std::string name = it->path().filename().string();
    std::error_code type_ec;
    if (it->is_directory(type_ec)) {
      if (openwow::text::EqualsIgnoreCaseAscii(name.c_str(), ".svn")) {
        it.disable_recursion_pending();
      } else if (!root.skipped_relative_subtree.empty()) {
        const auto relative = RelativeStormPath(*actual, it->path(), true);
        if (relative && *relative == root.skipped_relative_subtree) it.disable_recursion_pending();
      }
      continue;
    }
    if (type_ec || !it->is_regular_file(type_ec) || type_ec) continue;
    const auto relative = RelativeStormPath(*actual, it->path());
    if (!relative) continue;
    std::string key = *relative;
    int kind = 0;
    const std::size_t dot = key.find_last_of('.');
    if (dot != std::string::npos) {
      const char *extension = key.c_str() + dot + 1;
      if (openwow::text::EqualsIgnoreCaseAscii(extension, "bz")) { key.resize(dot); kind = 1; }
      else if (openwow::text::EqualsIgnoreCaseAscii(extension, "mpq")) { key.resize(dot); kind = 2; }
    }
    if (!key.empty()) cache.entries.try_emplace(
        FoldManifestKey(key),
        LooseFileManifestCacheEntry{JoinStormPaths(root.display_root, *relative), kind});
  }
}

std::string NormalizeCanonicalPath(std::string path) {
  std::replace(path.begin(), path.end(), '/', '\\');
  return openwow::text::ToLowerAscii(std::move(path));
}
std::optional<std::string> ResolveCanonicalPathUncached(const char *path) {
  if (!path || !*path) return std::nullopt;
  std::array<char, 1024> resolved{};
  if (!FileSystem_MakeAbsolutePath(path, resolved.data(), resolved.size())) return std::nullopt;
  return NormalizeCanonicalPath(resolved.data());
}

const char *FindLastBoundedCharOccurrence(const char *text, char needle, std::size_t max_length) {
  if (!text || needle == '\0') return nullptr;
  const char *last = nullptr;
  for (std::size_t i = 0; i < max_length && text[i]; ++i)
    if (text[i] == needle) last = text + i;
  return last;
}

template <typename Probe>
SearchResult ProbeSearchOrder(const char *source, char *candidate, int capacity, Probe &&probe) {
  const auto &state = openwow::data::GetStartupFileSystemState();
  const auto run = [&](SearchCandidate kind) {
    switch (probe(kind, candidate)) {
    case ProbeResult::kMatch: return SearchResult::kMatch;
    case ProbeResult::kTerminalReject: return SearchResult::kTerminalReject;
    case ProbeResult::kMiss: return SearchResult::kMiss;
    }
    return SearchResult::kMiss;
  };
  openwow::core::JoinStormPathBounded(candidate, capacity, state.executable_base_path.c_str(), source);
  if (auto result = run(SearchCandidate::kBasePathFile); result != SearchResult::kMiss) return result;
  if (const char *slash = FindLastBoundedCharOccurrence(source, '\\', 0x7FFFFFFFu);
      slash && slash[1]) {
    openwow::core::JoinStormPathBounded(candidate, capacity, state.executable_base_path.c_str(), slash + 1);
    if (auto result = run(SearchCandidate::kBasePathTailFile); result != SearchResult::kMiss) return result;
  }
  openwow::core::JoinStormPathBounded(candidate, capacity, state.executable_base_path.c_str(),
                                      state.archive_data_path.c_str());
  openwow::core::JoinStormPathBounded(candidate, capacity, candidate, source);
  if (auto result = run(SearchCandidate::kBaseDataFile); result != SearchResult::kMiss) return result;
  openwow::core::AppendStormPath(candidate, ".bz", capacity);
  if (auto result = run(SearchCandidate::kBaseDataBz); result != SearchResult::kMiss) return result;
  if (!state.executable_base_path.empty()) {
    openwow::core::JoinStormPathBounded(candidate, capacity, state.archive_data_path.c_str(), source);
    if (auto result = run(SearchCandidate::kLooseDataFile); result != SearchResult::kMiss) return result;
    openwow::core::AppendStormPath(candidate, ".bz", capacity);
    if (auto result = run(SearchCandidate::kLooseDataBz); result != SearchResult::kMiss) return result;
  }
  openwow::core::JoinStormPathBounded(candidate, capacity, state.executable_base_path.c_str(),
                                      state.archive_data_path.c_str());
  openwow::core::JoinStormPathBounded(candidate, capacity, candidate, source);
  openwow::core::AppendStormPath(candidate, ".MPQ", capacity);
  if (auto result = run(SearchCandidate::kBaseDataMpq); result != SearchResult::kMiss) return result;
  if (!state.locale_data_path.empty()) {
    openwow::core::JoinStormPathBounded(candidate, capacity, state.executable_base_path.c_str(),
                                        state.locale_data_path.c_str());
    openwow::core::JoinStormPathBounded(candidate, capacity, candidate, source);
    if (auto result = run(SearchCandidate::kLocaleDataFile); result != SearchResult::kMiss) return result;
  }
  return SearchResult::kMiss;
}

bool TryResolveManifestEntry(const char *key, char *resolved, int capacity, std::int32_t *type) {
  const auto entry = ResolveLooseFileManifestCaseInsensitive(key);
  if (!entry) return false;
  openwow::core::CopyStormPath(resolved, entry->resolved_path.c_str(), capacity);
  *type = entry->source_kind;
  return true;
}

std::string NormalizeRetailLookupPath(const char *source) {
  std::string normalized(source ? source : "");
  if (std::none_of(normalized.begin(), normalized.end(),
                   [](unsigned char value) { return value >= 0x80u; })) return normalized;
#if defined(__APPLE__)
  struct Releaser {
    void operator()(const void *value) const noexcept { if (value) CFRelease(value); }
  };
  using String = std::unique_ptr<std::remove_pointer_t<CFStringRef>, Releaser>;
  using MutableString = std::unique_ptr<std::remove_pointer_t<CFMutableStringRef>, Releaser>;
  if (normalized.size() > static_cast<std::size_t>(std::numeric_limits<CFIndex>::max()))
    return normalized;
  String source_string(CFStringCreateWithBytes(
      kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(normalized.data()),
      static_cast<CFIndex>(normalized.size()), kCFStringEncodingUTF8, false));
  if (!source_string) return normalized;
  MutableString decomposed(CFStringCreateMutableCopy(kCFAllocatorDefault, 0, source_string.get()));
  if (!decomposed) return normalized;
  CFStringNormalize(decomposed.get(), kCFStringNormalizationFormKD);
  std::array<char, 1025> output{};
  CFIndex bytes = 0;
  const CFIndex converted = CFStringGetBytes(
      decomposed.get(), CFRangeMake(0, CFStringGetLength(decomposed.get())),
      kCFStringEncodingUTF8, 0, false, reinterpret_cast<UInt8 *>(output.data()), 1024, &bytes);
  if (converted != 0 && bytes >= 0) return std::string(output.data(), static_cast<std::size_t>(bytes));
#endif
  return normalized;
}

}

const char *FindStormPathBasename(const char *path) {
  if (!path) return "";
  const char *basename = path;
  for (const char *cursor = path; *cursor; ++cursor)
    if (*cursor == '\\' || *cursor == '/') basename = cursor + 1;
  return basename;
}

bool IsFilesystemQualifiedPath(const char *path) {
  if (!path || !*path) return false;
  return ToNativePath(path).is_absolute() || (path[0] == '\\' && path[1] == '\\') || path[1] == ':';
}

int ResolveExistingPathAbsolute(const char *source, char *resolved, int capacity,
                                ExistingPathRequirement requirement) {
  if (!source || !resolved || capacity <= 0) return 0;
  const auto matches = [requirement](const ResolvedExistingPathInfo &info) {
    switch (requirement) {
    case ExistingPathRequirement::kFileOnly: return info.is_regular_file;
    case ExistingPathRequirement::kDirectoryOnly: return info.is_directory;
    case ExistingPathRequirement::kFileOrDirectory: return true;
    }
    return false;
  };
  const auto probe = [&](SearchCandidate, const char *candidate) {
    const auto info = ResolveExistingPathInfoCaseInsensitive(candidate);
    if (!info || !matches(*info)) return ProbeResult::kMiss;
    openwow::core::CopyStormPath(resolved, ToStormPathString(info->actual_path).c_str(), capacity);
    return ProbeResult::kMatch;
  };
  if (ProbeSearchOrder(source, resolved, capacity, probe) == SearchResult::kMatch) return 1;
  if (const auto path = ResolveExistingPathCaseInsensitive(source, requirement); path) {
    openwow::core::CopyStormPath(resolved, ToStormPathString(*path).c_str(), capacity);
    return 1;
  }
  return 0;
}

int SFileResolveFilesystemPath(const char *source, char *resolved, int capacity,
                               std::uint8_t open_flags, std::int32_t *out_type) {
  if (!source || !resolved || capacity <= 0 || !out_type) return 0;
  const auto probe = [&](SearchCandidate kind, const char *candidate) {
    const auto info = ResolveExistingPathInfoCaseInsensitive(candidate);
    if (!info) return ProbeResult::kMiss;
    if (!info->is_regular_file) {
      if (kind == SearchCandidate::kBaseDataFile || kind == SearchCandidate::kBaseDataBz ||
          kind == SearchCandidate::kBaseDataMpq || kind == SearchCandidate::kLocaleDataFile)
        return ProbeResult::kTerminalReject;
      return ProbeResult::kMiss;
    }
    openwow::core::CopyStormPath(resolved, ToStormPathString(info->actual_path).c_str(), capacity);
    *out_type = kind == SearchCandidate::kBaseDataBz || kind == SearchCandidate::kLooseDataBz
                    ? 1
                    : kind == SearchCandidate::kBaseDataMpq ? 2 : 0;
    return ProbeResult::kMatch;
  };
  const auto result = ProbeSearchOrder(source, resolved, capacity, probe);
  if (result == SearchResult::kMatch) return 1;
  if (result == SearchResult::kTerminalReject) return 0;
  if (IsFilesystemQualifiedPath(source)) {
    if (const auto loose = ResolveLooseFileCaseInsensitive(source); loose) {
      openwow::core::CopyStormPath(resolved, loose->c_str(), capacity);
      *out_type = 3;
      return 1;
    }
  }
  if (!openwow::data::GetStartupFileSystemState().base_path_init_flag) open_flags &= 0xFCu;
  if (ProbeArchiveFallback(source, open_flags)) {
    openwow::core::CopyStormPath(resolved, source, capacity);
    *out_type = 3;
    return 1;
  }
  openwow::core::SErrSetLastError(2);
  return 0;
}

int SFileOpenFile_ResolveLoosePath(const char *source, char *resolved, int capacity,
                                   std::uint8_t open_flags, std::int32_t *out_type) {
  if (!source || !resolved || capacity <= 0 || !out_type) return 0;
  const std::string normalized = NormalizeRetailLookupPath(source);
  source = normalized.c_str();
  const auto &state = openwow::data::GetStartupFileSystemState();
  open_flags = static_cast<std::uint8_t>(state.storm_open_flags | open_flags);
  if (state.force_filesystem_path_resolution || IsFilesystemQualifiedPath(source))
    return SFileResolveFilesystemPath(source, resolved, capacity, open_flags, out_type);
  if ((open_flags & 2u) && TryResolveManifestEntry(FindStormPathBasename(source), resolved, capacity, out_type))
    return 1;
  if ((open_flags & 1u) && TryResolveManifestEntry(source, resolved, capacity, out_type)) return 1;
  if (!state.base_path_init_flag) open_flags &= 0xFCu;
  if (ProbeArchiveFallback(source, open_flags)) {
    openwow::core::CopyStormPath(resolved, source, capacity);
    *out_type = 3;
    return 1;
  }
  return 0;
}

std::optional<LooseFileManifestCacheEntry> ResolveLooseFileManifestCaseInsensitive(
    const char *candidate) {
  if (!candidate || !*candidate || ToNativePath(candidate).is_absolute()) return std::nullopt;
  auto &cache = LooseCache();
  std::lock_guard lock(cache.mutex);
  if (!cache.initialized) {
    cache.entries.clear();
    for (const auto &root : BuildManifestRoots()) IndexManifestRoot(root, cache);
    cache.initialized = true;
  }
  const auto it = cache.entries.find(FoldManifestKey(candidate));
  return it == cache.entries.end() ? std::nullopt
                                   : std::optional<LooseFileManifestCacheEntry>(it->second);
}

void InvalidateSFileLooseManifestCache() {
  auto &cache = LooseCache();
  std::lock_guard lock(cache.mutex);
  cache.entries.clear();
  cache.initialized = false;
}
void ResetSFileLooseManifestCacheForTests() { InvalidateSFileLooseManifestCache(); }

std::optional<std::string> CanonicalizeStreamingManifestLookupPath(const char *path) {
  if (!path || !*path) return std::nullopt;
  const std::string raw(path);
  auto &cache = CanonicalCache();
  {
    std::lock_guard lock(cache.mutex);
    auto [it, inserted] = cache.entries.try_emplace(raw);
    if (!it->second.empty()) return it->second;
    (void)inserted;
  }
  const auto resolved = ResolveCanonicalPathUncached(raw.c_str());
  if (!resolved) return std::nullopt;
  std::lock_guard lock(cache.mutex);
  auto [it, inserted] = cache.entries.try_emplace(raw, *resolved);
  if (!inserted && it->second.empty()) it->second = *resolved;
  return it->second;
}
void InvalidateStreamingManifestLookupPathCache() {
  auto &cache = CanonicalCache();
  std::lock_guard lock(cache.mutex);
  cache.entries.clear();
}
void ResetLegacyManifestLookupPathCacheForTests() {
  InvalidateStreamingManifestLookupPathCache();
}

bool ProbeArchiveFallback(const char *source, std::uint8_t open_flags) {
  if (auto &probe = ArchiveProbeForTests(); probe) return probe(source, open_flags);
  if ((open_flags & 8u) != 0) return false;
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  const auto result = LookupRegisteredArchiveFile(nullptr, source, nullptr);
  return result == SFileArchiveLookupResult::kArchive ||
         result == SFileArchiveLookupResult::kDirectoryArchive;
#else
  (void)source;
  return false;
#endif
}

void SetSFileOpenFileArchiveProbeForTests(
    std::function<bool(const char *, std::uint8_t)> probe) {
  ArchiveProbeForTests() = std::move(probe);
}

}
