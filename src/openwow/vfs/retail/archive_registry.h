#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openwow::vfs {

enum class SFileArchiveLookupResult : std::int32_t;
struct SFileArchiveLookupInfo;

class ArchiveRegistry {
public:
  using DirectoryMemberResolver =
      std::function<std::optional<std::string>(const std::string &, const char *)>;
  using RawArchiveVisitor = std::function<void(void *, const std::string &)>;
  using RawOpenHook =
      std::function<bool(const char *, std::int32_t, std::uint32_t, void **)>;
  using RawPatchHook = std::function<bool(void *, const char *, const char *)>;
  using RawCloseHook = std::function<bool(void *)>;

  enum class PatchBaseKind : std::uint8_t {
    kInvalid,
    kDirectory,
    kMpq,
  };

  struct PatchOpenResult {
    PatchBaseKind base_kind = PatchBaseKind::kInvalid;
    std::uint32_t archive_token = 0;
    bool directory_open_result = false;
  };

  ArchiveRegistry();
  ~ArchiveRegistry();
  ArchiveRegistry(const ArchiveRegistry &) = delete;
  ArchiveRegistry &operator=(const ArchiveRegistry &) = delete;

  bool OpenRawArchive(const char *path, std::int32_t priority, std::uint32_t flags,
                      void **out_handle);
  bool CloseRawArchive(void *archive);
  bool OpenRawArchiveFile(void *archive, const char *filename, void **out_file,
                          std::uint32_t *out_size, std::string *out_archive_path,
                          std::shared_ptr<void> *out_retained_archive);
  std::uint32_t RegisterMpq(void *raw_archive, const char *path, std::uint32_t flags,
                            std::int32_t priority);
  std::uint32_t RegisterDirectory(std::string path, std::uint32_t flags,
                                  std::int32_t priority);
  bool Close(std::uint32_t token);
  bool Contains(std::uint32_t token) const;
  bool CopyPath(std::uint32_t token, char *output, int output_capacity) const;

  PatchOpenResult OpenPatch(std::uint32_t base_token, const char *path,
                            std::int32_t priority,
                            const std::function<void()> &before_mpq_open,
                            const std::function<bool(const std::string &, std::uint32_t)> &
                                open_directory);
  bool Authenticate(std::uint32_t token, std::int32_t *out_result, const void *modulus,
                    int modulus_size, const void *exponent, int exponent_size,
                    const char *suffix);

  SFileArchiveLookupResult Lookup(std::optional<std::uint32_t> token, const char *filename,
                                  const DirectoryMemberResolver &resolve_directory_member,
                                  SFileArchiveLookupInfo *out_info);
  bool QueryFileMetadata(const char *filename,
                         const DirectoryMemberResolver &resolve_directory_member,
                         std::string *archive_path, std::uint64_t *block_offset,
                         std::uint32_t *compressed_size, std::uint32_t *file_flags);
  bool ReadFileBytes(const char *filename,
                     const DirectoryMemberResolver &resolve_directory_member,
                     std::vector<std::uint8_t> *out_bytes);
  std::optional<std::string> ReadFile(std::uint32_t token, const char *filename,
                                     const DirectoryMemberResolver &resolve_directory_member);
  bool ReadFileBySourcePath(
      const char *archive_path, const char *filename,
      const DirectoryMemberResolver &resolve_directory_member,
      std::optional<std::string> *out_contents);
  std::optional<void *> FindRawArchiveByPath(const std::string &archive_path) const;
  bool VisitRawArchive(std::uint32_t token, bool lock_operation,
                       const RawArchiveVisitor &visitor) const;
  bool VisitArchive(std::uint32_t token, bool lock_operation,
                    const RawArchiveVisitor &visitor) const;

  void ResetForTests();
  std::size_t SizeForTests() const;
  std::vector<std::uint32_t> TokensInStockOrderForTests() const;
  void SetRawOpenHookForTests(RawOpenHook hook);
  void SetRawPatchHookForTests(RawPatchHook hook);
  void SetRawCloseHookForTests(RawCloseHook hook);
  void ResetRawHooksForTests();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

ArchiveRegistry &RetailArchiveRegistry();

}
