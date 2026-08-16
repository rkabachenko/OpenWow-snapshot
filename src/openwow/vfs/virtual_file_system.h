#pragma once

#include <cstddef>
#include <filesystem>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::vfs::mpq {
class MpqArchive;
}

namespace openwow::vfs {

enum class MountKind {
  kFilesystem,
  kMpqArchive,
  kEnhancedOverride,
};

struct MountPoint {
  std::string id;
  MountKind kind{MountKind::kFilesystem};
  std::filesystem::path source_root;

  std::vector<std::filesystem::path> mpq_patches;
  int priority{0};
  bool enabled{true};
};

class VirtualFileSystem {
 public:
  VirtualFileSystem();
  ~VirtualFileSystem();

  VirtualFileSystem(const VirtualFileSystem&) = default;
  VirtualFileSystem& operator=(const VirtualFileSystem&) = default;
  VirtualFileSystem(VirtualFileSystem&&) noexcept = default;
  VirtualFileSystem& operator=(VirtualFileSystem&&) noexcept = default;

  void Mount(const MountPoint& mount);
  std::vector<MountPoint> mounts() const;

  void InvalidateLookupCaches();

  [[nodiscard]] std::uint64_t lookup_revision() const noexcept {
    return mount_view_revision_;
  }

  void PrewarmMpqArchives() const;

  void PrewarmFileEnumeration(const std::string& virtual_path_root,
                              bool recursive) const;

  bool CanOpenMpqMount(std::string_view mount_id) const;

  bool Exists(const std::string& virtual_path) const;
  std::optional<std::filesystem::path> Resolve(const std::string& virtual_path) const;
  std::optional<std::vector<std::uint8_t>> ReadFileBytes(const std::string& virtual_path) const;

  std::optional<std::vector<std::uint8_t>> ReadFilePrefix(
      const std::string& virtual_path, std::size_t max_bytes) const;
  std::optional<std::string> ReadTextFile(const std::string& virtual_path) const;

  std::vector<std::filesystem::path> EnumerateFiles(const std::string& virtual_path_root,
                                                    bool recursive) const;

  std::vector<std::filesystem::path> EnumerateFilesByMountKind(
      const std::string& virtual_path_root,
      bool recursive,
      MountKind mount_kind) const;

 private:
  struct CacheState;
  struct ResolvedPath;

  static std::string NormalizeVirtualPath(const std::string& virtual_path);
  std::shared_ptr<mpq::MpqArchive> AcquireMpqArchive(
      const MountPoint& mount) const;

  [[nodiscard]] std::optional<ResolvedPath> FindCachedPath(
      const std::string& normalized_virtual_path) const;
  [[nodiscard]] ResolvedPath ResolvePath(
      const std::string& normalized_virtual_path) const;
  [[nodiscard]] ResolvedPath ResolvePathUncached(
      const std::string& normalized_virtual_path) const;
  std::optional<std::vector<std::uint8_t>> ReadFileBytesImpl(
      const std::string& virtual_path,
      std::optional<std::size_t> max_bytes) const;
  void CachePath(const std::string& normalized_virtual_path,
                 const ResolvedPath& resolved) const;
  void EraseCachedPath(const std::string& normalized_virtual_path) const;
  [[nodiscard]] std::vector<std::filesystem::path> EnumerateFilesImpl(
      const std::string& virtual_path_root,
      bool recursive,
      std::optional<MountKind> mount_kind) const;
  [[nodiscard]] std::vector<std::filesystem::path> EnumerateFilesUncached(
      const std::string& normalized_root,
      bool recursive,
      std::optional<MountKind> mount_kind) const;

  std::optional<std::filesystem::path> ResolveFilesystemPathCaseInsensitive(
      const MountPoint& mount,
      const std::string& normalized_virtual_path) const;

  std::vector<MountPoint> mounts_;
  std::shared_ptr<CacheState> cache_state_;

  std::uint64_t mount_view_revision_{0};
};

}
