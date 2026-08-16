#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::data {

inline constexpr std::size_t kTextureCacheRowPathCapacity = 0x80u;

[[nodiscard]] std::uint32_t HashTextureCachePath(std::string_view path);

[[nodiscard]] bool TextureCachePathsAlias(std::string_view lhs,
                                          std::string_view rhs) noexcept;

[[nodiscard]] std::string CopyTextureCacheRowPath(std::string_view path);

[[nodiscard]] std::string MakeRetailTextureCacheBlpPath(
    std::string_view stored_path);
[[nodiscard]] std::string MakeRetailTextureCacheTgaPath(
    std::string_view stored_path);

struct TextureCacheRowIdentity {
  std::uint32_t hash{0};
  std::string path;

  std::uint64_t generation{0};
};

struct TextureCacheRowSource {
  TextureCacheRowIdentity identity;
  std::shared_ptr<const std::vector<std::uint8_t>> bytes;

  [[nodiscard]] explicit operator bool() const noexcept {
    return bytes != nullptr && !bytes->empty();
  }
};

class TextureCacheRowStore {
 public:
  using SourceLoader =
      std::function<std::vector<std::uint8_t>(const std::string&)>;

  TextureCacheRowStore();
  ~TextureCacheRowStore();

  TextureCacheRowStore(const TextureCacheRowStore&) = delete;
  TextureCacheRowStore& operator=(const TextureCacheRowStore&) = delete;

  [[nodiscard]] TextureCacheRowIdentity Resolve(std::string_view path);

  [[nodiscard]] TextureCacheRowSource Load(
      const TextureCacheRowIdentity& identity, const SourceLoader& loader);

  [[nodiscard]] bool IsTerminalFailure(std::uint32_t row_hash) const;
  [[nodiscard]] bool IsCurrent(
      const TextureCacheRowIdentity& identity) const;
  void MarkTerminalFailure(const TextureCacheRowIdentity& identity);

  void ClearTerminalFailure(const TextureCacheRowIdentity& identity);
  void ResetTerminalFailures();

  void InvalidateSources(
      std::span<const std::uint32_t> retained_row_hashes = {});
  void Clear();

  [[nodiscard]] std::size_t RowCount() const;
  [[nodiscard]] std::size_t TerminalFailureCount() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

inline constexpr std::uint32_t kTextureCacheRowInitialPackedStatus = 0x10000u;

struct TextureCacheRowHeaderState {
  bool is_blp2_version_1 = false;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  std::uint8_t alpha_depth = 0;
  std::uint8_t mip_count = 0;
  std::uint32_t packed_status = kTextureCacheRowInitialPackedStatus;
};

[[nodiscard]] constexpr TextureCacheRowHeaderState
MakeInitialTextureCacheRowHeaderState() {
  return {};
}

void CTextureCacheRow_FreeAsyncReadBuffer(std::uint32_t async_read_object_token);

[[nodiscard]] std::optional<TextureCacheRowHeaderState>
ParseTextureCacheRowHeaderState(
    const std::uint8_t* data, std::size_t size,
    std::uint32_t existing_status = kTextureCacheRowInitialPackedStatus);

}
