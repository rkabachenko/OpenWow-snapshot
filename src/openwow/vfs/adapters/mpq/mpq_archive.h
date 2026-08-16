#pragma once

#include <cstddef>
#include <filesystem>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::vfs::mpq {

inline constexpr std::uint32_t kArchiveOpenFlagStrictHeaderOffset = 0x8u;
inline constexpr std::uint32_t kArchiveOpenFlagSearchLastValidHeader = 0x20u;
inline constexpr std::uint32_t kStormErrorNotArchive = 108u;

enum class ArchiveOpenProbeStatus {
  kSuccess,
  kNotArchive,
  kUnavailable,
};

struct ArchiveOpenProbeInfo {
  std::uint64_t file_size = 0;
  std::uint64_t header_offset = 0;
  std::uint64_t hash_table_offset = 0;
  std::uint64_t block_table_offset = 0;
  std::uint64_t extended_block_table_offset = 0;
  std::uint64_t table_region_end = 0;
  std::uint32_t header_size = 0;
  std::uint32_t archive_size = 0;
  std::uint32_t hash_table_entry_count = 0;
  std::uint32_t block_table_entry_count = 0;
  std::uint16_t format_version = 0;
  std::uint16_t sector_size_shift = 0;
  std::uint32_t sector_size = 0;
  bool has_user_data = false;
  std::uint64_t user_data_payload_offset = 0;
  std::uint32_t user_data_payload_size = 0;
  std::uint32_t user_data_header_span = 0;
};

ArchiveOpenProbeStatus ProbeArchiveOpenHeader(
    const std::filesystem::path& path,
    std::uint32_t flags,
    ArchiveOpenProbeInfo* out_info = nullptr);

bool OpenStormArchive(const char* path,
                      std::uint32_t priority,
                      std::uint32_t flags,
                      void** out_archive);

class MpqArchive {
 public:
  ~MpqArchive();
  bool Open(const std::filesystem::path& path);

  bool ApplyPatch(const std::filesystem::path& patch_path);
  void Close();
  bool IsOpen() const;
  bool has_patches() const;
  std::filesystem::path archive_path() const;

  std::uint32_t last_error() const;
  bool Exists(const std::string& virtual_path) const;

  [[nodiscard]] bool HasDeleteMarker(const std::string& virtual_path) const;
  std::optional<std::vector<std::uint8_t>> ReadFile(const std::string& virtual_path) const;
  std::optional<std::vector<std::uint8_t>> ReadFilePrefix(
      const std::string& virtual_path, std::size_t max_bytes) const;
  std::vector<std::string> EnumerateFiles(const std::string& virtual_root,
                                          bool recursive) const;

  std::vector<std::string> EnumerateDeleteMarkedFiles(
      const std::string& virtual_root, bool recursive) const;

 private:
  static std::string NormalizeVirtualPath(const std::string& virtual_path);
  static std::string ToLowerAscii(std::string value);
  void CloseUnlocked();
  void BuildFileIndexUnlocked() const;
  void CollectFileIndexForRootUnlocked(
      const std::string& normalized_root,
      bool recursive,
      std::unordered_map<std::string, std::string>& out) const;
  std::optional<std::vector<std::uint8_t>> ReadFileUnlocked(
      const std::string& virtual_path,
      std::optional<std::size_t> max_bytes) const;
  bool HasDeleteMarkerUnlocked(const std::string& normalized_path) const;

  mutable std::mutex mutex_;
  std::filesystem::path archive_path_;
  void* archive_handle_{nullptr};
  bool is_open_{false};
  bool has_patches_{false};
  mutable std::uint32_t last_error_{0};

  mutable bool index_ready_{false};
  mutable std::unordered_map<std::string, std::string> canonical_names_by_lower_;
};

}
