#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace openwow::vfs {

struct LooseFileManifestCacheEntry {
  std::string resolved_path;
  int source_kind = 0;
};

const char *FindStormPathBasename(const char *path);
bool IsFilesystemQualifiedPath(const char *path);
int SFileResolveFilesystemPath(const char *source, char *resolved_path,
                               int resolved_path_capacity, std::uint8_t open_flags,
                               std::int32_t *out_type);
int SFileOpenFile_ResolveLoosePath(const char *source, char *resolved_path,
                                   int resolved_path_capacity, std::uint8_t open_flags,
                                   std::int32_t *out_type);
std::optional<LooseFileManifestCacheEntry> ResolveLooseFileManifestCaseInsensitive(
    const char *candidate);
void InvalidateSFileLooseManifestCache();
void InvalidateStreamingManifestLookupPathCache();
std::optional<std::string> CanonicalizeStreamingManifestLookupPath(const char *path);
void ResetSFileLooseManifestCacheForTests();
void ResetLegacyManifestLookupPathCacheForTests();
bool ProbeArchiveFallback(const char *source, std::uint8_t open_flags);
void SetSFileOpenFileArchiveProbeForTests(
    std::function<bool(const char *, std::uint8_t)> probe);

}
