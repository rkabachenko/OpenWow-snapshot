
#include "openwow/game/warden_client.h"
#include "openwow/core/md5.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/game/warden_module.h"
#include "openwow/foundation/hashing/retail_sha1.h"
#include "openwow/ui/frame_script_events.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/vfs/sfile_core.h"
#include "openwow/vfs/retail/io_unit/io_unit_compat.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <system_error>
#include <string_view>
#include <utility>

namespace openwow::game {

namespace {

constexpr std::array<uint8_t, 16> kLegacyTokenSeedSignature = {
    0x55, 0x8D, 0x6C, 0x24, 0x90, 0x81, 0xEC, 0xDC,
    0x01, 0x00, 0x00, 0x83, 0x65, 0x48, 0x00, 0x83,
};

constexpr std::array<uint8_t, 20> kEmbeddedModuleExpectedHash = {
    0xDD, 0x8D, 0x57, 0xCE, 0x70, 0x36, 0x27, 0xAB,
    0xAB, 0x21, 0xA2, 0x51, 0xCE, 0x78, 0x24, 0x28,
    0xFF, 0x90, 0xE3, 0xEE,
};

using Sha1Digest = WardenSha1Digest;

constexpr std::size_t kMaxWardenPacketSize =
    std::numeric_limits<std::uint16_t>::max();
constexpr std::size_t kMaxNormalizedPayloadSize = 512;
constexpr std::uint8_t kModuleMissingOpcode = 0;
constexpr std::uint8_t kModuleOkOpcode = 1;
constexpr std::uint8_t kCheckResultOpcode = 2;
constexpr std::uint8_t kHashResultOpcode = 4;
constexpr std::uint8_t kModuleFailedOpcode = 5;
constexpr std::uint8_t kSignatureAbsent = 0xE9;
constexpr std::uint8_t kSignatureMatch = 0x4A;

class ByteReader {
 public:
  explicit ByteReader(const std::span<const std::uint8_t> bytes)
      : bytes_(bytes) {}

  [[nodiscard]] std::size_t remaining() const {
    return bytes_.size() - position_;
  }

  [[nodiscard]] bool empty() const { return remaining() == 0; }

  bool ReadU8(std::uint8_t& value) {
    if (remaining() < 1) {
      return false;
    }
    value = bytes_[position_++];
    return true;
  }

  bool ReadU16(std::uint16_t& value) {
    if (remaining() < 2) {
      return false;
    }
    value = static_cast<std::uint16_t>(bytes_[position_]) |
            (static_cast<std::uint16_t>(bytes_[position_ + 1]) << 8);
    position_ += 2;
    return true;
  }

  bool ReadU32(std::uint32_t& value) {
    if (remaining() < 4) {
      return false;
    }
    value = static_cast<std::uint32_t>(bytes_[position_]) |
            (static_cast<std::uint32_t>(bytes_[position_ + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes_[position_ + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes_[position_ + 3]) << 24);
    position_ += 4;
    return true;
  }

  bool ReadBytes(const std::size_t size,
                 std::span<const std::uint8_t>& bytes) {
    if (size > remaining()) {
      return false;
    }
    bytes = bytes_.subspan(position_, size);
    position_ += size;
    return true;
  }

  template <std::size_t Size>
  bool ReadArray(std::array<std::uint8_t, Size>& value) {
    std::span<const std::uint8_t> bytes;
    if (!ReadBytes(Size, bytes)) {
      return false;
    }
    std::copy(bytes.begin(), bytes.end(), value.begin());
    return true;
  }

 private:
  std::span<const std::uint8_t> bytes_;
  std::size_t position_ = 0;
};

class ByteWriter {
 public:
  explicit ByteWriter(const std::size_t maximum_size)
      : maximum_size_(maximum_size) {
    bytes_.reserve(maximum_size);
  }

  bool WriteU8(const std::uint8_t value) {
    if (!CanWrite(1)) {
      return false;
    }
    bytes_.push_back(value);
    return true;
  }

  bool WriteU16(const std::uint16_t value) {
    if (!CanWrite(2)) {
      return false;
    }
    bytes_.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    bytes_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    return true;
  }

  bool WriteU32(const std::uint32_t value) {
    if (!CanWrite(4)) {
      return false;
    }
    bytes_.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    bytes_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    bytes_.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    bytes_.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
    return true;
  }

  bool WriteBytes(const std::span<const std::uint8_t> bytes) {
    if (!CanWrite(bytes.size())) {
      return false;
    }
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    return true;
  }

  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const {
    return bytes_;
  }

  [[nodiscard]] std::vector<std::uint8_t> Take() { return std::move(bytes_); }

 private:
  [[nodiscard]] bool CanWrite(const std::size_t size) const {
    return size <= maximum_size_ - bytes_.size();
  }

  std::size_t maximum_size_;
  std::vector<std::uint8_t> bytes_;
};

[[nodiscard]] Sha1Digest Sha1Bytes(const std::uint8_t* data,
                                   const std::size_t size) {
  foundation::hashing::RetailSha1State context{};
  foundation::hashing::InitializeRetailSha1(context);
  std::size_t remaining = size;
  const std::uint8_t* cursor = data;
  while (remaining != 0) {
    const std::size_t chunk_size = std::min<std::size_t>(
        remaining, std::numeric_limits<std::uint32_t>::max());
    foundation::hashing::UpdateRetailSha1(context, cursor, static_cast<std::uint32_t>(chunk_size));
    cursor += chunk_size;
    remaining -= chunk_size;
  }

  Sha1Digest digest{};
  foundation::hashing::FinalizeRetailSha1(context, digest.data());
  return digest;
}

[[nodiscard]] Sha1Digest Sha1Bytes(
    const std::span<const std::uint8_t> bytes) {
  return Sha1Bytes(bytes.data(), bytes.size());
}

[[nodiscard]] std::uint32_t XorSha1Checksum(
    const std::span<const std::uint8_t> bytes) {
  const Sha1Digest digest = Sha1Bytes(bytes);
  ByteReader reader(digest);
  std::uint32_t checksum = 0;
  for (std::size_t word_index = 0; word_index < 5; ++word_index) {
    std::uint32_t word = 0;
    if (!reader.ReadU32(word)) {
      return 0;
    }
    checksum ^= word;
  }
  return checksum;
}

[[nodiscard]] Sha1Digest AdvanceWardenKeyGenerator(
    const Sha1Digest& first_half_hash,
    const Sha1Digest& current,
    const Sha1Digest& second_half_hash) {
  foundation::hashing::RetailSha1State context{};
  foundation::hashing::InitializeRetailSha1(context);
  foundation::hashing::UpdateRetailSha1(context, first_half_hash.data(),
                               static_cast<std::uint32_t>(first_half_hash.size()));
  foundation::hashing::UpdateRetailSha1(context, current.data(),
                               static_cast<std::uint32_t>(current.size()));
  foundation::hashing::UpdateRetailSha1(context, second_half_hash.data(),
                               static_cast<std::uint32_t>(second_half_hash.size()));

  Sha1Digest digest{};
  foundation::hashing::FinalizeRetailSha1(context, digest.data());
  return digest;
}

struct WardenModuleProfile {
  std::array<std::uint8_t, 16> module_id;
  std::array<std::uint8_t, 16> module_key;
  std::uint32_t compressed_size;
  Sha1Digest compressed_sha1;
  std::array<std::uint8_t, 16> hash_request_seed;
  WardenSessionKeys post_hash_keys;
};

constexpr WardenModuleProfile kStandardWin335Module = {
    .module_id = {
        0x79, 0xC0, 0x76, 0x8D, 0x65, 0x79, 0x77, 0xD6,
        0x97, 0xE1, 0x0B, 0xAD, 0x95, 0x6C, 0xCE, 0xD1,
    },
    .module_key = {
        0xAE, 0x25, 0xBC, 0x51, 0x06, 0x3B, 0x77, 0xBD,
        0x36, 0x3C, 0x3E, 0xFE, 0x0F, 0xC1, 0x73, 0xF9,
    },
    .compressed_size = 18756,
    .compressed_sha1 = {
        0x12, 0xD5, 0x5B, 0x72, 0xF0, 0x34, 0x2A, 0xCE, 0x7B, 0x40,
        0xDE, 0x3A, 0x6C, 0xDC, 0x61, 0x9A, 0x89, 0xAC, 0x7C, 0xB3,
    },
    .hash_request_seed = {
        0x4D, 0x80, 0x8D, 0x2C, 0x77, 0xD9, 0x05, 0xC4,
        0x1A, 0x63, 0x80, 0xEC, 0x08, 0x58, 0x6A, 0xFE,
    },
    .post_hash_keys = {
        .client_to_server = {
            0x7F, 0x96, 0xEE, 0xFD, 0xA5, 0xB6, 0x3D, 0x20,
            0xA4, 0xDF, 0x8E, 0x00, 0xCB, 0xF4, 0x83, 0x04,
        },
        .server_to_client = {
            0xC2, 0xB7, 0xAD, 0xED, 0xFC, 0xCC, 0xA9, 0xC2,
            0xBF, 0xB3, 0xF8, 0x56, 0x02, 0xBA, 0x80, 0x9B,
        },
    },
};

[[nodiscard]] const WardenModuleProfile* FindWardenModuleProfile(
    const std::span<const std::uint8_t> module_id) {
  return module_id.size() == kStandardWin335Module.module_id.size() &&
                 std::equal(kStandardWin335Module.module_id.begin(),
                            kStandardWin335Module.module_id.end(),
                            module_id.begin())
      ? &kStandardWin335Module
      : nullptr;
}

[[nodiscard]] bool ValidateModulePayload(
    const WardenModuleProfile& profile,
    const std::span<const std::uint8_t> payload) {
  return payload.size() == profile.compressed_size &&
         openwow::core::MD5_Digest(payload.data(), payload.size()) ==
             profile.module_id &&
         Sha1Bytes(payload) == profile.compressed_sha1;
}

constexpr std::array<std::uint8_t, 20> kArchiveInitializePayload = {
    0x01, 0x00, 0x01, 0x00, 0x80, 0x4F, 0x02, 0x00, 0xC0, 0x18,
    0x02, 0x00, 0x30, 0x25, 0x02, 0x00, 0x10, 0x29, 0x02, 0x00,
};
constexpr std::array<std::uint8_t, 8> kLuaInitializePayload = {
    0x04, 0x00, 0x00, 0x10, 0x92, 0x41, 0x00, 0x01,
};
constexpr std::array<std::uint8_t, 8> kPerformanceInitializePayload = {
    0x01, 0x01, 0x00, 0x20, 0xAE, 0x46, 0x00, 0x01,
};

[[nodiscard]] std::span<const std::uint8_t> ExpectedInitializePayload(
    const std::size_t record_index) {
  switch (record_index) {
    case 0:
      return kArchiveInitializePayload;
    case 1:
      return kLuaInitializePayload;
    case 2:
      return kPerformanceInitializePayload;
    default:
      return {};
  }
}

struct CheckDescriptor {
  WardenCheckType type = WardenCheckType::kTimingCheck;
  std::uint8_t first_index = 0;
  std::uint8_t second_index = 0;
  std::uint32_t address = 0;
  std::uint8_t length = 0;
  std::array<std::uint8_t, 4> seed{};
  Sha1Digest digest{};
};

constexpr std::size_t kMaxReferenceExecutableSize = 128u * 1024u * 1024u;
constexpr std::uint32_t kMaxReferenceImageSize = 256u * 1024u * 1024u;
constexpr std::uint32_t kMaxArchiveCheckSize = 128u * 1024u * 1024u;
constexpr std::size_t kArchiveHashChunkSize = 64u * 1024u;
constexpr std::size_t kMaxArchiveHashCacheEntries = 256;

[[nodiscard]] bool ReadLe16(const std::span<const std::uint8_t> bytes,
                            const std::size_t offset,
                            std::uint16_t& value) {
  if (offset > bytes.size() || bytes.size() - offset < 2) {
    return false;
  }
  value = static_cast<std::uint16_t>(bytes[offset]) |
          (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
  return true;
}

[[nodiscard]] bool ReadLe32(const std::span<const std::uint8_t> bytes,
                            const std::size_t offset,
                            std::uint32_t& value) {
  if (offset > bytes.size() || bytes.size() - offset < 4) {
    return false;
  }
  value = static_cast<std::uint32_t>(bytes[offset]) |
          (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
          (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
          (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
  return true;
}

[[nodiscard]] bool LoadReferencePeImage(
    const std::filesystem::path& executable,
    std::vector<std::uint8_t>& mapped_image,
    std::uint32_t& image_base) {
  std::ifstream stream(executable, std::ios::binary | std::ios::ate);
  if (!stream) {
    return false;
  }

  const std::streamoff end = stream.tellg();
  if (end <= 0 ||
      static_cast<std::uintmax_t>(end) > kMaxReferenceExecutableSize) {
    return false;
  }
  const auto file_size = static_cast<std::size_t>(end);
  std::vector<std::uint8_t> file(file_size);
  stream.seekg(0, std::ios::beg);
  stream.read(reinterpret_cast<char*>(file.data()),
              static_cast<std::streamsize>(file.size()));
  if (!stream) {
    return false;
  }

  const std::span<const std::uint8_t> bytes(file);
  std::uint32_t pe_offset = 0;
  if (bytes.size() < 0x40 || bytes[0] != 'M' || bytes[1] != 'Z' ||
      !ReadLe32(bytes, 0x3C, pe_offset) ||
      pe_offset > bytes.size() || bytes.size() - pe_offset < 24 ||
      bytes[pe_offset] != 'P' || bytes[pe_offset + 1] != 'E' ||
      bytes[pe_offset + 2] != 0 || bytes[pe_offset + 3] != 0) {
    return false;
  }

  std::uint16_t section_count = 0;
  std::uint16_t optional_header_size = 0;
  if (!ReadLe16(bytes, pe_offset + 6, section_count) ||
      !ReadLe16(bytes, pe_offset + 20, optional_header_size) ||
      section_count == 0 || section_count > 96 ||
      optional_header_size < 64) {
    return false;
  }

  const std::size_t optional_header = pe_offset + 24;
  const std::size_t section_table = optional_header + optional_header_size;
  if (optional_header > bytes.size() ||
      optional_header_size > bytes.size() - optional_header ||
      section_table > bytes.size() ||
      section_count > (bytes.size() - section_table) / 40) {
    return false;
  }

  std::uint16_t optional_magic = 0;
  std::uint32_t size_of_image = 0;
  std::uint32_t size_of_headers = 0;
  if (!ReadLe16(bytes, optional_header, optional_magic) ||
      optional_magic != 0x10Bu ||
      !ReadLe32(bytes, optional_header + 28, image_base) ||
      !ReadLe32(bytes, optional_header + 56, size_of_image) ||
      !ReadLe32(bytes, optional_header + 60, size_of_headers) ||
      image_base == 0 || size_of_headers == 0 ||
      size_of_image < size_of_headers ||
      size_of_headers > file.size() ||
      size_of_image > kMaxReferenceImageSize ||
      static_cast<std::uint64_t>(image_base) + size_of_image >
          static_cast<std::uint64_t>(
              std::numeric_limits<std::uint32_t>::max()) + 1u) {
    return false;
  }

  std::vector<std::uint8_t> image(size_of_image, 0);
  const std::size_t header_bytes = std::min<std::size_t>(
      {size_of_headers, file.size(), image.size()});
  std::copy_n(file.begin(), header_bytes, image.begin());

  for (std::size_t index = 0; index < section_count; ++index) {
    const std::size_t section = section_table + index * 40;
    std::uint32_t virtual_address = 0;
    std::uint32_t raw_size = 0;
    std::uint32_t raw_offset = 0;
    if (!ReadLe32(bytes, section + 12, virtual_address) ||
        !ReadLe32(bytes, section + 16, raw_size) ||
        !ReadLe32(bytes, section + 20, raw_offset)) {
      return false;
    }
    if (raw_size == 0) {
      continue;
    }
    if (raw_offset > file.size() || raw_size > file.size() - raw_offset ||
        virtual_address >= image.size() ||
        raw_size > image.size() - virtual_address) {
      return false;
    }
    std::copy_n(file.begin() + raw_offset, raw_size,
                image.begin() + virtual_address);
  }

  mapped_image = std::move(image);
  return true;
}

[[nodiscard]] std::filesystem::path StartupPathToNative(std::string path) {
#if !defined(_WIN32)
  std::replace(path.begin(), path.end(), '\\',
               std::filesystem::path::preferred_separator);
#endif
  return std::filesystem::path(std::move(path));
}

[[nodiscard]] bool EqualsIgnoreCaseAscii(const std::string_view lhs,
                                         const std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    const auto lower = [](const unsigned char value) {
      return value >= 'A' && value <= 'Z'
                 ? static_cast<unsigned char>(value - 'A' + 'a')
                 : value;
    };
    if (lower(static_cast<unsigned char>(lhs[index])) !=
        lower(static_cast<unsigned char>(rhs[index]))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::string_view PathLeaf(const std::string_view path) {
  const std::size_t separator = path.find_last_of("/\\");
  return separator == std::string_view::npos ? path : path.substr(separator + 1);
}

[[nodiscard]] std::filesystem::path ResolveReferenceExecutable(
    const std::filesystem::path& configured) {
  if (!configured.empty()) {
    return configured;
  }

  std::filesystem::path root;
  const auto& startup = openwow::data::GetStartupFileSystemState();
  if (!startup.executable_base_path.empty()) {
    root = StartupPathToNative(startup.executable_base_path);
  } else {
    std::error_code error;
    root = std::filesystem::current_path(error);
    if (error) {
      return "Wow.exe";
    }
  }

  std::error_code error;
  const std::filesystem::path uppercase = root / "Wow.exe";
  if (std::filesystem::is_regular_file(uppercase, error)) {
    return uppercase;
  }
  return root / "wow.exe";
}

[[nodiscard]] bool IsSafeArchiveCheckPath(const std::string_view filename) {
  if (filename.empty() || filename.size() > 260 || filename.front() == '/' ||
      filename.front() == '\\' ||
      (filename.size() > 1 && filename[1] == ':')) {
    return false;
  }

  std::size_t component_begin = 0;
  for (std::size_t index = 0; index <= filename.size(); ++index) {
    const bool at_end = index == filename.size();
    const unsigned char value = at_end
        ? 0
        : static_cast<unsigned char>(filename[index]);
    if (!at_end && (value == 0 || value < 0x20)) {
      return false;
    }
    if (!at_end && value != '/' && value != '\\') {
      continue;
    }
    const std::string_view component =
        filename.substr(component_begin, index - component_begin);
    if (component == "." || component == "..") {
      return false;
    }
    component_begin = index + 1;
  }
  return true;
}

[[nodiscard]] std::string NormalizeArchiveCacheKey(
    const std::string_view filename) {
  std::string key(filename);
  for (char& value : key) {
    if (value == '/') {
      value = '\\';
    } else if (value >= 'A' && value <= 'Z') {
      value = static_cast<char>(value - 'A' + 'a');
    }
  }
  return key;
}

struct LuaProbeResult {
  bool available = false;
  std::optional<std::string> value;
};

[[nodiscard]] LuaProbeResult EvaluateGameLua(
    const std::string_view expression) {
  lua_State* state =
      openwow::ui::frame_script_events::FrameScript_GetLuaStateTyped();
  if (state == nullptr) {
    return {};
  }

  LuaProbeResult result{.available = true};
  const int original_top = lua_gettop(state);
  if (luaL_loadbuffer(state, expression.data(), expression.size(), "=Warden") !=
          0 ||
      lua_pcall(state, 0, 1, 0) != 0) {
    lua_settop(state, original_top);
    return result;
  }

  std::size_t result_size = 0;
  const char* value = lua_tolstring(state, -1, &result_size);
  if (value != nullptr) {

    result_size = std::min<std::size_t>(result_size, 0xFFu);
    result.value.emplace(value, result_size);
  }
  lua_settop(state, original_top);
  return result;
}

constexpr std::array<uint8_t, 512> kEmbeddedModuleEncryptedBlob = {
    0x4F, 0xC9, 0xB9, 0xCC, 0xB9, 0x26, 0x07, 0xB1,
    0xF5, 0x41, 0x9E, 0xE5, 0x78, 0x9A, 0x41, 0xE0,
    0xF2, 0xF6, 0x98, 0xB4, 0x62, 0x2D, 0x51, 0x9C,
    0xCF, 0x20, 0x9B, 0x67, 0x40, 0x73, 0xD6, 0x9B,
    0x92, 0x8D, 0xFC, 0x60, 0x2D, 0xAF, 0x14, 0xB7,
    0x6E, 0x8A, 0xED, 0x41, 0x54, 0xEC, 0xED, 0xC8,
    0x54, 0x67, 0xF4, 0x81, 0x4F, 0xBA, 0x2E, 0x60,
    0x98, 0x87, 0xC9, 0x59, 0x1B, 0xBE, 0xE3, 0x5C,
    0x6A, 0x35, 0x5F, 0x14, 0x62, 0x2C, 0xD1, 0x8B,
    0xB5, 0x63, 0x99, 0x2A, 0x28, 0xB9, 0x48, 0x1E,
    0x5E, 0x2B, 0x1D, 0xF9, 0x98, 0xEA, 0xB9, 0x47,
    0x40, 0x5A, 0xF6, 0xDE, 0xA2, 0x26, 0xEA, 0x51,
    0x66, 0xEC, 0xF5, 0x42, 0x2F, 0x1A, 0xCC, 0xF7,
    0xB4, 0xC4, 0x85, 0xFE, 0x5D, 0x10, 0x4B, 0xEB,
    0xE6, 0xD0, 0xAB, 0x85, 0xA2, 0x3F, 0x1B, 0xBC,
    0xAE, 0x5C, 0x7A, 0x15, 0xE5, 0x40, 0x34, 0x7E,
    0xA6, 0xFA, 0x46, 0xF7, 0x04, 0x2D, 0x6E, 0xC0,
    0xDA, 0xFB, 0x3A, 0x91, 0xF8, 0x71, 0x7A, 0xBF,
    0xB7, 0x13, 0x4A, 0x24, 0x0A, 0x27, 0xA9, 0x71,
    0x75, 0x24, 0xF0, 0x2B, 0xBC, 0x7F, 0x8F, 0xCE,
    0xBB, 0x10, 0x97, 0x76, 0x18, 0x39, 0xC3, 0x56,
    0x62, 0x2F, 0x24, 0x51, 0xEF, 0x97, 0x16, 0x55,
    0xD4, 0x09, 0x69, 0x4B, 0x48, 0x24, 0x47, 0xE2,
    0x0B, 0xFE, 0xEE, 0x9F, 0x7E, 0xB7, 0x6D, 0x43,
    0x4C, 0x52, 0x32, 0x78, 0x28, 0x17, 0x94, 0xED,
    0xC0, 0x65, 0xBA, 0x7C, 0xF9, 0x2E, 0x25, 0xFA,
    0xF6, 0x7C, 0x54, 0x54, 0x2E, 0xB2, 0x1F, 0xB0,
    0xE0, 0xD7, 0x3D, 0x0A, 0x3A, 0xA9, 0xB7, 0x02,
    0xA2, 0x2A, 0x16, 0xB9, 0x23, 0x87, 0x96, 0xAC,
    0x7C, 0xF0, 0xA5, 0x8D, 0xF6, 0xEB, 0xDA, 0x1E,
    0xE1, 0x01, 0x55, 0x5B, 0xCF, 0x2C, 0x34, 0xE2,
    0xBF, 0x18, 0x94, 0x0F, 0xAD, 0x1A, 0xB2, 0x52,
    0x91, 0xC7, 0xDC, 0xC5, 0xF0, 0xF5, 0x96, 0xBE,
    0xFF, 0xEE, 0x28, 0xDE, 0xD2, 0x4A, 0x2B, 0x02,
    0xED, 0xCB, 0x2F, 0xF7, 0x06, 0x2C, 0x2B, 0x07,
    0xFB, 0xDF, 0x4B, 0x5F, 0x64, 0x1C, 0x69, 0x90,
    0x62, 0xC2, 0x16, 0x76, 0xFE, 0x14, 0x2E, 0x01,
    0x35, 0xA0, 0xE5, 0x9C, 0xE4, 0xC8, 0xFE, 0x44,
    0x16, 0x8F, 0x84, 0x5B, 0x36, 0x47, 0x80, 0xBF,
    0x5E, 0x2E, 0x83, 0x47, 0x0C, 0xDF, 0xB6, 0xCD,
    0x10, 0xC8, 0x1E, 0xD1, 0xA1, 0x41, 0xAA, 0x42,
    0x0C, 0xBF, 0x41, 0x0B, 0x5D, 0xA2, 0x70, 0xEC,
    0x62, 0xEB, 0xF8, 0x28, 0x09, 0x33, 0x08, 0x26,
    0x38, 0x21, 0x2A, 0x66, 0x89, 0xB9, 0xAC, 0xD2,
    0xB7, 0x1E, 0xB7, 0xF0, 0x93, 0x1A, 0x49, 0x57,
    0xE8, 0xA3, 0x9F, 0x62, 0x4F, 0xB8, 0x91, 0x55,
    0x22, 0xA9, 0xBC, 0xC5, 0x30, 0x4D, 0xCA, 0xD0,
    0x95, 0x99, 0xA7, 0x4A, 0x4C, 0x7E, 0x88, 0x3F,
    0x68, 0xB1, 0x23, 0xE2, 0x5A, 0x43, 0xA9, 0xF6,
    0xCF, 0x48, 0xDF, 0xB0, 0xB1, 0x2D, 0xF4, 0xBF,
    0x01, 0xCE, 0x4F, 0xBF, 0x27, 0x6B, 0x3D, 0xEF,
    0x9F, 0xAA, 0xC4, 0x1E, 0x38, 0x01, 0xF2, 0x63,
    0x1A, 0x89, 0xC7, 0x1F, 0x15, 0xFB, 0x12, 0xAD,
    0xDB, 0x0C, 0x87, 0x49, 0xF9, 0xDF, 0x68, 0xB3,
    0x75, 0x8B, 0x32, 0x8D, 0x6A, 0x4F, 0x5B, 0xCC,
    0xA4, 0xC5, 0x1F, 0xEC, 0x9A, 0xEE, 0xD1, 0x75,
    0x3B, 0x6E, 0x66, 0x1E, 0x15, 0x39, 0xA6, 0xDF,
    0xED, 0xEB, 0x05, 0x22, 0x59, 0xF5, 0x81, 0xD6,
    0x39, 0xD4, 0x53, 0xBA, 0x62, 0x5E, 0x2F, 0xBC,
    0x11, 0xA4, 0x63, 0x23, 0x24, 0x74, 0xE0, 0x1D,
    0x4F, 0x67, 0x3C, 0x79, 0xB5, 0xE0, 0xB7, 0xB3,
    0x6B, 0xA9, 0x87, 0x3C, 0xF4, 0x38, 0xF6, 0xB8,
    0x61, 0x2F, 0xE8, 0xA2, 0x0F, 0x75, 0x86, 0x9F,
    0xD8, 0x65, 0x5F, 0x03, 0x5C, 0x17, 0x4A, 0x7B,
};

}

WardenSessionKeys DeriveWardenSessionKeys(
    const std::span<const std::uint8_t> session_key) {
  const std::size_t half = session_key.size() / 2;
  const Sha1Digest first_half_hash = Sha1Bytes(session_key.data(), half);
  const std::uint8_t* second_half =
      session_key.empty() ? nullptr : session_key.data() + half;
  const Sha1Digest second_half_hash =
      Sha1Bytes(second_half, session_key.size() - half);

  Sha1Digest stream_block{};
  stream_block = AdvanceWardenKeyGenerator(first_half_hash, stream_block,
                                           second_half_hash);

  std::array<std::uint8_t, 32> stream{};
  std::size_t stream_offset = 0;
  std::size_t block_offset = 0;
  while (stream_offset < stream.size()) {
    if (block_offset == stream_block.size()) {
      stream_block = AdvanceWardenKeyGenerator(
          first_half_hash, stream_block, second_half_hash);
      block_offset = 0;
    }
    stream[stream_offset++] = stream_block[block_offset++];
  }

  WardenSessionKeys keys;
  std::copy_n(stream.begin(), keys.client_to_server.size(),
              keys.client_to_server.begin());
  std::copy_n(stream.begin() + keys.client_to_server.size(),
              keys.server_to_client.size(), keys.server_to_client.begin());
  return keys;
}

void WardenClient::Init(const uint8_t* session_key, size_t key_len) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto session_key_bytes = session_key != nullptr
      ? std::span<const std::uint8_t>(session_key, key_len)
      : std::span<const std::uint8_t>{};
  const WardenSessionKeys keys = DeriveWardenSessionKeys(session_key_bytes);

  encrypt_state_.Init(keys.client_to_server.data(), keys.client_to_server.size());
  decrypt_state_.Init(keys.server_to_client.data(), keys.server_to_client.size());

  ResetModuleState();
  archive_hash_cache_.clear();
  if (reference_client_image_.empty()) {

    reference_client_load_attempted_ = false;
  }
  legacy_token_seed_verified_ = false;
  std::memset(legacy_token_seed_buffer_, 0, sizeof(legacy_token_seed_buffer_));
  embedded_module_activated_ = false;
  std::memset(embedded_module_data_, 0, sizeof(embedded_module_data_));
  embedded_module_callback_ = nullptr;

  pending_responses_.clear();
  initialized_ = true;

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "WardenClient: initialized");
}

void WardenClient::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  initialized_ = false;
  encrypt_state_.Reset();
  decrypt_state_.Reset();
  ResetModuleState();
  legacy_token_seed_verified_ = false;
  std::memset(legacy_token_seed_buffer_, 0, sizeof(legacy_token_seed_buffer_));
  embedded_module_activated_ = false;
  std::memset(embedded_module_data_, 0, sizeof(embedded_module_data_));
  embedded_module_callback_ = nullptr;
  pending_responses_.clear();
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "WardenClient: reset");
}

void WardenClient::ResetModuleState() {
  standard_profile_selected_ = false;
  awaiting_module_transfer_ = false;
  module_loaded_ = false;
  module_payload_validated_ = false;
  hash_response_pending_ = false;
  post_hash_active_ = false;
  module_initialization_failed_ = false;
  module_data_.clear();
  module_id_.fill(0);
  module_key_.fill(0);
  module_size_ = 0;
  module_received_ = 0;
  next_initialize_record_ = 0;
  portable_capabilities_ = {};
}

bool WardenClient::IsInitialized() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return initialized_;
}

bool WardenClient::HasPendingResponse() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !pending_responses_.empty();
}

void WardenClient::SetProbeCallbacks(WardenProbeCallbacks callbacks) {
  std::lock_guard<std::mutex> lock(mutex_);
  probe_callbacks_ = std::move(callbacks);
}

void WardenClient::SetReferenceClientExecutable(
    std::filesystem::path executable) {
  std::lock_guard<std::mutex> lock(mutex_);
  reference_client_executable_ = std::move(executable);
  reference_client_load_attempted_ = false;
  reference_client_image_base_ = 0;
  reference_client_image_.clear();
}

bool WardenClient::EnsureReferenceClientImageLoaded() {
  if (!reference_client_image_.empty()) {
    return true;
  }
  if (reference_client_load_attempted_) {
    return false;
  }
  reference_client_load_attempted_ = true;

  const std::filesystem::path executable =
      ResolveReferenceExecutable(reference_client_executable_);
  std::vector<std::uint8_t> mapped_image;
  std::uint32_t image_base = 0;
  if (!LoadReferencePeImage(executable, mapped_image, image_base)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kError,
        "WardenClient: cannot map pristine 3.3.5a client image: " +
            executable.string());
    return false;
  }

  reference_client_image_base_ = image_base;
  reference_client_image_ = std::move(mapped_image);
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "WardenClient: mapped pristine client image for portable MEM checks (" +
          std::to_string(reference_client_image_.size()) + " bytes)");
  return true;
}

std::optional<std::vector<std::uint8_t>>
WardenClient::ReadReferenceClientMemory(
    const std::string_view module_name,
    const std::uint32_t address,
    const std::uint8_t length) const {
  if (!reference_client_image_.empty() &&
      (module_name.empty() ||
       EqualsIgnoreCaseAscii(PathLeaf(module_name), "Wow.exe")) &&
      address >= reference_client_image_base_) {
    const std::uint64_t offset =
        static_cast<std::uint64_t>(address) - reference_client_image_base_;
    if (offset <= reference_client_image_.size() &&
        length <= reference_client_image_.size() - offset) {
      const auto begin = reference_client_image_.begin() +
          static_cast<std::ptrdiff_t>(offset);
      return std::vector<std::uint8_t>(begin, begin + length);
    }
  }
  return std::nullopt;
}

std::optional<WardenSha1Digest> WardenClient::HashArchiveFile(
    const std::string_view filename) {

  if (!IsSafeArchiveCheckPath(filename)) {
    return std::nullopt;
  }

  const std::string cache_key = NormalizeArchiveCacheKey(filename);
  if (const auto cached = archive_hash_cache_.find(cache_key);
      cached != archive_hash_cache_.end()) {
    return cached->second;
  }

  const std::string path(filename);
  int file_handle = 0;
  if (!openwow::vfs::SFileOpenFile_Wrapper(path.c_str(), &file_handle)) {
    return std::nullopt;
  }
  struct ScopedFileHandle {
    int handle;
    ~ScopedFileHandle() {
      (void)openwow::vfs::IOUnitContainer_CloseFileHandle(handle);
    }
  } scoped_file{file_handle};

  std::uint32_t size_high = 0;
  const int signed_size =
      openwow::vfs::SFile_GetFileSize(file_handle, &size_high);
  if (signed_size < 0 || size_high != 0 ||
      static_cast<std::uint32_t>(signed_size) > kMaxArchiveCheckSize) {
    return std::nullopt;
  }

  foundation::hashing::RetailSha1State context{};
  foundation::hashing::InitializeRetailSha1(context);
  std::array<std::uint8_t, kArchiveHashChunkSize> buffer{};
  std::uint32_t remaining = static_cast<std::uint32_t>(signed_size);
  while (remaining != 0) {
    const std::uint32_t chunk_size = static_cast<std::uint32_t>(
        std::min<std::size_t>(remaining, buffer.size()));
    std::uint32_t bytes_read = 0;
    if (!openwow::vfs::SFile_ReadFile(
            file_handle, buffer.data(), static_cast<int>(chunk_size),
            &bytes_read, 0, 0) ||
        bytes_read != chunk_size) {
      return std::nullopt;
    }
    foundation::hashing::UpdateRetailSha1(context, buffer.data(), chunk_size);
    remaining -= chunk_size;
  }

  WardenSha1Digest digest{};
  foundation::hashing::FinalizeRetailSha1(context, digest.data());
  if (archive_hash_cache_.size() < kMaxArchiveHashCacheEntries) {
    archive_hash_cache_.emplace(cache_key, digest);
  }
  return digest;
}

WardenPortableCapabilities WardenClient::GetPortableCapabilities() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return portable_capabilities_;
}

bool WardenClient::ProcessLegacyTokenSeed(const std::string& encoded_seed,
                                          const uint8_t key[16]) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (legacy_token_seed_verified_) {
    return true;
  }

  unsigned int decoded_size = 0;
  uint8_t current_byte = 0;
  bool have_high_nibble = false;
  for (const char ch : encoded_seed) {
    if (decoded_size >= sizeof(legacy_token_seed_buffer_)) {
      break;
    }
    const unsigned char value = static_cast<unsigned char>(ch - 'a');
    if (value > 0x0F) {
      break;
    }

    current_byte = static_cast<uint8_t>((current_byte << 4) + value);
    if (have_high_nibble) {
      legacy_token_seed_buffer_[decoded_size++] = current_byte;
    }
    have_high_nibble = !have_high_nibble;
  }

  RC4State state;
  state.Init(key, 16);
  state.Process(legacy_token_seed_buffer_, decoded_size);

  if (std::memcmp(legacy_token_seed_buffer_,
                  kLegacyTokenSeedSignature.data(),
                  kLegacyTokenSeedSignature.size()) == 0) {
    legacy_token_seed_verified_ = true;
  }
  return legacy_token_seed_verified_;
}

bool WardenClient::ConsumeLegacyTokenSeedVerification() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!legacy_token_seed_verified_) {
    return false;
  }

  legacy_token_seed_verified_ = false;
  return true;
}

bool WardenClient::IsLegacyTokenSeedVerified() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return legacy_token_seed_verified_;
}

bool WardenClient::DecryptAndActivateEmbeddedModule(
    const uint8_t* key, uint32_t key_len,
    WardenModuleActivationCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (embedded_module_activated_) {
    return true;
  }

  if (key == nullptr || key_len == 0) {
    return false;
  }

  RC4State rc4_state;
  rc4_state.Init(key, key_len);

  uint8_t decrypted[512];
  std::memcpy(decrypted, kEmbeddedModuleEncryptedBlob.data(), 512);
  rc4_state.Process(decrypted, 512);

  const Sha1Digest hash = Sha1Bytes(decrypted, sizeof(decrypted));

  if (std::memcmp(hash.data(), kEmbeddedModuleExpectedHash.data(),
                  kEmbeddedModuleExpectedHash.size()) != 0) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WardenClient: embedded module hash mismatch");
    return false;
  }

  std::memcpy(embedded_module_data_, decrypted, 512);
  embedded_module_activated_ = true;
  embedded_module_callback_ = callback;

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "WardenClient: embedded scan module activated");
  return true;
}

bool WardenClient::IsEmbeddedModuleActivated() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return embedded_module_activated_;
}

const uint8_t* WardenClient::GetEmbeddedModuleData() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!embedded_module_activated_) {
    return nullptr;
  }
  return embedded_module_data_;
}

void WardenClient::HandleWardenData(const uint8_t* data, size_t len) {
  if (len == 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WardenClient: received data before init");
    return;
  }
  if (data == nullptr || len > kMaxWardenPacketSize) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WardenClient: rejected invalid packet buffer");
    return;
  }

  std::vector<std::uint8_t> packet(data, data + len);
  decrypt_state_.Process(packet.data(), packet.size());

  const auto opcode = static_cast<WardenServerOpcode>(packet.front());
  const std::span<const std::uint8_t> payload(packet.data() + 1,
                                               packet.size() - 1);

  switch (opcode) {
    case WardenServerOpcode::kModuleUse:
      HandleModuleUse(payload);
      break;
    case WardenServerOpcode::kModuleCache:
      HandleModuleTransfer(payload);
      break;
    case WardenServerOpcode::kHashRequest:
      HandleHashRequest(payload);
      break;
    case WardenServerOpcode::kCheckRequest:
      HandleCheckData(payload);
      break;
    case WardenServerOpcode::kModuleInitialize:

      HandleModuleInitialize(packet);
      break;
    case WardenServerOpcode::kMemoryRequest:
      HandleMemoryRequest(payload);
      break;
    default:
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "WardenClient: unknown opcode " +
                         std::to_string(static_cast<unsigned>(packet.front())));
      break;
  }
}

std::vector<uint8_t> WardenClient::BuildResponse() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (pending_responses_.empty()) {
    return {};
  }

  PendingResponse response = std::move(pending_responses_.front());
  pending_responses_.pop_front();
  std::vector<std::uint8_t> result = std::move(response.bytes);
  encrypt_state_.Process(result.data(), result.size());
  if (response.rekey.has_value()) {
    encrypt_state_.Init(response.rekey->client_to_server.data(),
                        response.rekey->client_to_server.size());
    decrypt_state_.Init(response.rekey->server_to_client.data(),
                        response.rekey->server_to_client.size());
    hash_response_pending_ = false;
    post_hash_active_ = true;
  }
  return result;
}

bool WardenClient::QueueResponse(
    std::vector<std::uint8_t> bytes,
    std::optional<WardenSessionKeys> rekey) {
  constexpr std::size_t kMaxPendingResponseCount = 16;
  constexpr std::size_t kMaxResponseSize = kMaxNormalizedPayloadSize + 7;
  if (bytes.empty() || bytes.size() > kMaxResponseSize ||
      pending_responses_.size() >= kMaxPendingResponseCount) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WardenClient: response queue limit reached");
    return false;
  }
  pending_responses_.push_back(
      PendingResponse{.bytes = std::move(bytes), .rekey = std::move(rekey)});
  return true;
}

void WardenClient::QueueModuleFailure(const std::string_view reason) {
  awaiting_module_transfer_ = false;
  module_loaded_ = false;
  module_payload_validated_ = false;
  hash_response_pending_ = false;
  post_hash_active_ = false;
  module_initialization_failed_ = true;
  module_data_.clear();
  module_received_ = 0;
  next_initialize_record_ = 0;
  portable_capabilities_ = {};
  QueueResponse({kModuleFailedOpcode});
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                     "WardenClient: " + std::string(reason));
}

void WardenClient::HandleModuleUse(
    const std::span<const std::uint8_t> payload) {
  ResetModuleState();

  ByteReader reader(payload);
  if (!reader.ReadArray(module_id_) || !reader.ReadArray(module_key_) ||
      !reader.ReadU32(module_size_) || !reader.empty()) {
    QueueModuleFailure("malformed MODULE_USE");
    return;
  }

  const WardenModuleProfile* profile = FindWardenModuleProfile(module_id_);
  if (profile == nullptr || module_key_ != profile->module_key ||
      module_size_ != profile->compressed_size) {
    QueueModuleFailure("unsupported MODULE_USE metadata");
    return;
  }
  standard_profile_selected_ = true;
  module_initialization_failed_ = false;
  module_data_.reserve(profile->compressed_size);

  WardenModuleData cached_module{};
  if (WardenModuleCache_Load(module_id_.data(), cached_module)) {
    const bool cache_is_valid = cached_module.data != nullptr &&
        cached_module.size == profile->compressed_size &&
        ValidateModulePayload(
            *profile,
            std::span<const std::uint8_t>(cached_module.data,
                                          cached_module.size));
    if (cache_is_valid) {
      module_data_.assign(cached_module.data,
                          cached_module.data + cached_module.size);
      module_received_ = cached_module.size;
      module_loaded_ = true;
      module_payload_validated_ = true;
    }
    WardenClient_FreeModuleData(&cached_module);
    if (cache_is_valid) {
      QueueResponse({kModuleOkOpcode});
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                         "WardenClient: validated standard module cache hit");
      return;
    }
  }

  awaiting_module_transfer_ = true;
  QueueResponse({kModuleMissingOpcode});
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "WardenClient: standard module cache miss");
}

void WardenClient::HandleModuleTransfer(
    const std::span<const std::uint8_t> payload) {
  ByteReader reader(payload);
  std::uint16_t chunk_size = 0;
  std::span<const std::uint8_t> chunk;
  if (!reader.ReadU16(chunk_size) || chunk_size == 0 ||
      !reader.ReadBytes(chunk_size, chunk) || !reader.empty()) {
    QueueModuleFailure("malformed MODULE_CACHE chunk");
    return;
  }
  if (!standard_profile_selected_ || !awaiting_module_transfer_ ||
      module_data_.size() != module_received_ ||
      module_received_ > module_size_ ||
      chunk.size() > module_size_ - module_received_) {
    QueueModuleFailure("out-of-state or oversized MODULE_CACHE chunk");
    return;
  }

  module_data_.insert(module_data_.end(), chunk.begin(), chunk.end());
  module_received_ += static_cast<std::uint32_t>(chunk.size());
  if (module_received_ != module_size_) {
    return;
  }

  const WardenModuleProfile* profile = FindWardenModuleProfile(module_id_);
  if (profile == nullptr || !ValidateModulePayload(*profile, module_data_)) {
    QueueModuleFailure("standard module digest validation failed");
    return;
  }

  awaiting_module_transfer_ = false;
  module_loaded_ = true;
  module_payload_validated_ = true;
  WardenModuleCache_Store(module_id_.data(), module_data_.data(),
                          static_cast<std::uint32_t>(module_data_.size()));
  QueueResponse({kModuleOkOpcode});
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "WardenClient: validated standard module transfer");
}

void WardenClient::HandleHashRequest(
    const std::span<const std::uint8_t> payload) {
  const WardenModuleProfile* profile = FindWardenModuleProfile(module_id_);
  const bool seed_matches =
      profile != nullptr && payload.size() == profile->hash_request_seed.size() &&
      std::equal(profile->hash_request_seed.begin(),
                 profile->hash_request_seed.end(), payload.begin());

  if (!standard_profile_selected_ || !module_loaded_ ||
      !module_payload_validated_ || hash_response_pending_ ||
      post_hash_active_ || !seed_matches) {
    QueueModuleFailure("invalid module HASH_REQUEST");
    return;
  }

  const Sha1Digest key_hash = Sha1Bytes(
      profile->post_hash_keys.client_to_server.data(),
      profile->post_hash_keys.client_to_server.size());
  std::vector<std::uint8_t> response;
  response.reserve(1 + key_hash.size());
  response.push_back(kHashResultOpcode);
  response.insert(response.end(), key_hash.begin(), key_hash.end());
  hash_response_pending_ =
      QueueResponse(std::move(response), profile->post_hash_keys);
  if (!hash_response_pending_) {
    return;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "WardenClient: module hash accepted; rekey pending");
}

void WardenClient::HandleModuleInitialize(
    const std::span<const std::uint8_t> packet) {
  const auto fail = [this](const std::string_view reason) {
    module_initialization_failed_ = true;
    next_initialize_record_ = 0;
    portable_capabilities_ = {};
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WardenClient: " + std::string(reason));
  };

  if (!standard_profile_selected_ || !module_loaded_ ||
      !module_payload_validated_ || !post_hash_active_ ||
      module_initialization_failed_) {
    fail("out-of-state MODULE_INITIALIZE");
    return;
  }

  ByteReader reader(packet);
  std::size_t parsed_records = 0;
  while (!reader.empty()) {
    std::uint8_t opcode = 0;
    std::uint16_t payload_size = 0;
    std::uint32_t checksum = 0;
    std::span<const std::uint8_t> payload;
    if (!reader.ReadU8(opcode) ||
        opcode != static_cast<std::uint8_t>(
                      WardenServerOpcode::kModuleInitialize) ||
        !reader.ReadU16(payload_size) ||
        payload_size > kMaxNormalizedPayloadSize ||
        !reader.ReadU32(checksum) ||
        !reader.ReadBytes(payload_size, payload)) {
      fail("malformed MODULE_INITIALIZE frame");
      return;
    }

    const std::span<const std::uint8_t> expected =
        ExpectedInitializePayload(next_initialize_record_);
    if (expected.empty() || payload.size() != expected.size() ||
        !std::equal(expected.begin(), expected.end(), payload.begin()) ||
        XorSha1Checksum(payload) != checksum) {
      fail("unexpected MODULE_INITIALIZE record");
      return;
    }

    switch (next_initialize_record_) {
      case 0:
        portable_capabilities_.archive_callbacks = true;
        break;
      case 1:
        portable_capabilities_.lua_execute = true;
        break;
      case 2:
        portable_capabilities_.performance_counter = true;
        break;
      default:
        fail("too many MODULE_INITIALIZE records");
        return;
    }
    ++next_initialize_record_;
    ++parsed_records;
  }

  if (parsed_records == 0) {
    fail("empty MODULE_INITIALIZE packet");
    return;
  }
  if (portable_capabilities_.IsComplete()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "WardenClient: portable module capabilities ready");
  }
}

void WardenClient::HandleMemoryRequest(
    const std::span<const std::uint8_t> payload) {

  ByteReader reader(payload);
  std::uint8_t blob_size = 0;
  std::span<const std::uint8_t> blob;
  if (!reader.ReadU8(blob_size) || !reader.ReadBytes(blob_size, blob) ||
      !reader.empty()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WardenClient: malformed legacy memory request");
    return;
  }
  (void)blob;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                     "WardenClient: legacy memory request safely ignored");
}

void WardenClient::HandleCheckData(
    const std::span<const std::uint8_t> payload) {
  if (!standard_profile_selected_ || !module_loaded_ ||
      !module_payload_validated_ || !post_hash_active_ ||
      !portable_capabilities_.IsComplete()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WardenClient: out-of-state check request");
    return;
  }
  if (payload.empty() || payload.size() > kMaxNormalizedPayloadSize) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WardenClient: oversized check request");
    return;
  }

  ByteReader reader(payload);
  std::vector<std::string> strings;
  for (;;) {
    std::uint8_t string_size = 0;
    std::span<const std::uint8_t> string_bytes;
    if (!reader.ReadU8(string_size) ||
        (string_size != 0 && !reader.ReadBytes(string_size, string_bytes))) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "WardenClient: malformed check string table");
      return;
    }
    if (string_size == 0) {
      break;
    }
    strings.emplace_back(reinterpret_cast<const char*>(string_bytes.data()),
                         string_bytes.size());
  }

  const auto valid_string_index = [&strings](const std::uint8_t index) {
    return index != 0 && static_cast<std::size_t>(index) <= strings.size();
  };
  const auto valid_module_index = [&valid_string_index](
                                      const std::uint8_t index) {
    return index == 0 || valid_string_index(index);
  };

  std::vector<CheckDescriptor> descriptors;
  bool found_terminator = false;
  while (!reader.empty()) {
    std::uint8_t encoded_type = 0;
    if (!reader.ReadU8(encoded_type)) {
      return;
    }
    if (encoded_type == 0x7F) {
      found_terminator = true;
      break;
    }

    CheckDescriptor descriptor;
    descriptor.type = static_cast<WardenCheckType>(encoded_type ^ 0x7F);
    bool valid = true;
    switch (descriptor.type) {
      case WardenCheckType::kTimingCheck:
        break;
      case WardenCheckType::kMemCheck:
        valid = reader.ReadU8(descriptor.first_index) &&
                reader.ReadU32(descriptor.address) &&
                reader.ReadU8(descriptor.length) &&
                valid_module_index(descriptor.first_index);
        break;
      case WardenCheckType::kPageCheckA:
      case WardenCheckType::kPageCheckB:
        valid = reader.ReadArray(descriptor.seed) &&
                reader.ReadArray(descriptor.digest) &&
                reader.ReadU32(descriptor.address) &&
                reader.ReadU8(descriptor.length);
        break;
      case WardenCheckType::kMpqCheck:
      case WardenCheckType::kLuaStrCheck:
        valid = reader.ReadU8(descriptor.first_index) &&
                valid_string_index(descriptor.first_index);
        break;
      case WardenCheckType::kDriverCheck:
        valid = reader.ReadArray(descriptor.seed) &&
                reader.ReadArray(descriptor.digest) &&
                reader.ReadU8(descriptor.first_index) &&
                valid_string_index(descriptor.first_index);
        break;
      case WardenCheckType::kProcCheck:
        valid = reader.ReadArray(descriptor.seed) &&
                reader.ReadArray(descriptor.digest) &&
                reader.ReadU8(descriptor.first_index) &&
                reader.ReadU8(descriptor.second_index) &&
                reader.ReadU32(descriptor.address) &&
                reader.ReadU8(descriptor.length) &&
                valid_module_index(descriptor.first_index) &&
                valid_string_index(descriptor.second_index);
        break;
      case WardenCheckType::kModuleCheck:
        valid = reader.ReadArray(descriptor.seed) &&
                reader.ReadArray(descriptor.digest);
        break;
      default:
        valid = false;
        break;
    }
    if (!valid) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "WardenClient: malformed check descriptor");
      return;
    }
    descriptors.push_back(std::move(descriptor));
  }

  if (!found_terminator || !reader.empty()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WardenClient: unterminated check request");
    return;
  }

  const auto string_at = [&strings](const std::uint8_t index) -> std::string_view {
    return index == 0 ? std::string_view{} : strings[index - 1];
  };
  ByteWriter body(kMaxNormalizedPayloadSize);
  bool response_valid = true;
  for (const CheckDescriptor& descriptor : descriptors) {
    switch (descriptor.type) {
      case WardenCheckType::kTimingCheck: {
        std::uint32_t ticks = 0;
        if (probe_callbacks_.monotonic_ticks) {
          ticks = probe_callbacks_.monotonic_ticks();
        } else {
          const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now().time_since_epoch());
          ticks = static_cast<std::uint32_t>(elapsed.count());
        }
        response_valid = body.WriteU8(1) && body.WriteU32(ticks);
        break;
      }
      case WardenCheckType::kMemCheck: {
        std::optional<std::vector<std::uint8_t>> bytes;
        if (probe_callbacks_.read_memory) {
          bytes = probe_callbacks_.read_memory(
              string_at(descriptor.first_index), descriptor.address,
              descriptor.length);
        } else {
          if (!EnsureReferenceClientImageLoaded()) {

            openwow::diagnostics::Log(
                openwow::diagnostics::LogLevel::kError,
                "WardenClient: withholding check response; MEM adapter unavailable");
            return;
          }
          bytes = ReadReferenceClientMemory(
              string_at(descriptor.first_index), descriptor.address,
              descriptor.length);
        }
        if (!bytes.has_value() || bytes->size() != descriptor.length) {
          response_valid = body.WriteU8(1);
        } else {
          response_valid = body.WriteU8(0) && body.WriteBytes(*bytes);
        }
        break;
      }
      case WardenCheckType::kMpqCheck: {
        std::optional<Sha1Digest> digest;
        if (probe_callbacks_.hash_archive_file) {
          digest = probe_callbacks_.hash_archive_file(
              string_at(descriptor.first_index));
        } else {
          digest = HashArchiveFile(string_at(descriptor.first_index));
        }
        response_valid = digest.has_value()
            ? body.WriteU8(0) && body.WriteBytes(*digest)
            : body.WriteU8(1);
        break;
      }
      case WardenCheckType::kLuaStrCheck: {
        std::optional<std::string> result;
        if (probe_callbacks_.evaluate_lua) {
          result = probe_callbacks_.evaluate_lua(
              string_at(descriptor.first_index));
        } else {
          LuaProbeResult lua_result = EvaluateGameLua(
              string_at(descriptor.first_index));
          if (!lua_result.available) {
            openwow::diagnostics::Log(
                openwow::diagnostics::LogLevel::kError,
                "WardenClient: withholding check response; Lua adapter unavailable");
            return;
          }
          result = std::move(lua_result.value);
        }
        if (!result.has_value()) {
          response_valid = body.WriteU8(1);
        } else {
          const std::size_t result_size =
              std::min<std::size_t>(result->size(), 0xFFu);
          const auto result_bytes = std::span<const std::uint8_t>(
              reinterpret_cast<const std::uint8_t*>(result->data()),
              result_size);
          response_valid = body.WriteU8(0) &&
                           body.WriteU8(static_cast<std::uint8_t>(
                               result_size)) &&
                           body.WriteBytes(result_bytes);
        }
        break;
      }
      case WardenCheckType::kPageCheckA:
      case WardenCheckType::kPageCheckB:
      case WardenCheckType::kDriverCheck:
      case WardenCheckType::kProcCheck:
      case WardenCheckType::kModuleCheck: {
        WardenSignatureProbe probe{
            .type = descriptor.type,
            .seed = descriptor.seed,
            .digest = descriptor.digest,
            .address = descriptor.address,
            .length = descriptor.length,
        };
        if (descriptor.type == WardenCheckType::kDriverCheck) {
          probe.primary_name = string_at(descriptor.first_index);
        } else if (descriptor.type == WardenCheckType::kProcCheck) {
          probe.primary_name = string_at(descriptor.first_index);
          probe.secondary_name = string_at(descriptor.second_index);
        }
        const bool matches = probe_callbacks_.signature_matches &&
                             probe_callbacks_.signature_matches(probe);
        response_valid = body.WriteU8(matches ? kSignatureMatch
                                              : kSignatureAbsent);
        break;
      }
    }
    if (!response_valid) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "WardenClient: check response exceeded 512 bytes");
      return;
    }
  }

  ByteWriter response(kMaxNormalizedPayloadSize + 7);
  const std::uint16_t body_size =
      static_cast<std::uint16_t>(body.bytes().size());
  if (!response.WriteU8(kCheckResultOpcode) || !response.WriteU16(body_size) ||
      !response.WriteU32(XorSha1Checksum(body.bytes())) ||
      !response.WriteBytes(body.bytes())) {
    return;
  }
  QueueResponse(response.Take());
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                     "WardenClient: check response for " +
                         std::to_string(descriptors.size()) + " descriptors");
}

}
