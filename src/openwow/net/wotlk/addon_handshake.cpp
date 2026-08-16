#include "openwow/net/wotlk/addon_handshake.h"

#include "openwow/core/md5.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/net/protocol/packet_compression.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <string_view>
#include <utility>

namespace openwow::net::wotlk {

namespace {

void AppendU8(std::vector<std::uint8_t>& out, const std::uint8_t value) {
  out.push_back(value);
}

void AppendU32(std::vector<std::uint8_t>& out, const std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void AppendCString(std::vector<std::uint8_t>& out, const std::string& value) {
  out.insert(out.end(), value.begin(), value.end());
  out.push_back(0);
}

bool ReadU8(const std::uint8_t* data,
            const std::size_t size,
            std::size_t& pos,
            std::uint8_t& out) {
  if (pos + 1 > size) {
    return false;
  }
  out = data[pos++];
  return true;
}

bool ReadU32(const std::uint8_t* data,
             const std::size_t size,
             std::size_t& pos,
             std::uint32_t& out) {
  if (pos + sizeof(out) > size) {
    return false;
  }
  out = static_cast<std::uint32_t>(data[pos]) |
        (static_cast<std::uint32_t>(data[pos + 1]) << 8u) |
        (static_cast<std::uint32_t>(data[pos + 2]) << 16u) |
        (static_cast<std::uint32_t>(data[pos + 3]) << 24u);
  pos += sizeof(out);
  return true;
}

bool ReadBytes(const std::uint8_t* data,
               const std::size_t size,
               std::size_t& pos,
               std::uint8_t* out,
               const std::size_t count) {
  if (pos + count > size) {
    return false;
  }
  std::memcpy(out, data + pos, count);
  pos += count;
  return true;
}

bool ReadCString(const std::uint8_t* data,
                 const std::size_t size,
                 std::size_t& pos,
                 std::string& out,
                 const std::size_t max_len) {
  out.clear();
  std::size_t copied = 0;
  while (pos < size && copied < max_len) {
    const char ch = static_cast<char>(data[pos++]);
    ++copied;
    if (ch == '\0') {
      return true;
    }
    out.push_back(ch);
  }
  return false;
}

std::uint32_t ReadU32LE(const std::uint8_t* data) {
  return static_cast<std::uint32_t>(data[0]) |
         (static_cast<std::uint32_t>(data[1]) << 8) |
         (static_cast<std::uint32_t>(data[2]) << 16) |
         (static_cast<std::uint32_t>(data[3]) << 24);
}

void WriteU32LE(std::vector<std::uint8_t>& out, const std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

std::array<std::uint8_t, 16> ComputeMd5Digest(const std::string_view value) {
  return openwow::core::MD5_Digest(value.data(), value.size());
}

RealmAddonCatalogEntry CatalogEntryFromDbc(
    const openwow::data::dbc::BannedAddOnsEntry& source) {
  RealmAddonCatalogEntry entry{};
  entry.id = source.id;
  std::memcpy(entry.name_digest.data(),
              source.name_md5,
              entry.name_digest.size());
  std::memcpy(entry.version_digest.data(),
              source.version_md5,
              entry.version_digest.size());
  entry.revision = source.last_modified;
  entry.flags = source.flags;
  return entry;
}

bool LoadCatalogFileRecords(const std::filesystem::path& path,
                            std::vector<RealmAddonCatalogEntry>& entries) {
  entries.clear();

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return true;
  }

  std::vector<std::uint8_t> bytes(
      (std::istreambuf_iterator<char>(input)),
      std::istreambuf_iterator<char>());
  constexpr std::size_t kCatalogEntrySize = 44;
  if ((bytes.size() % kCatalogEntrySize) != 0) {
    entries.clear();
    return false;
  }

  for (std::size_t offset = 0; offset < bytes.size(); offset += kCatalogEntrySize) {
    RealmAddonCatalogEntry entry{};
    entry.id = ReadU32LE(bytes.data() + offset);
    std::memcpy(entry.name_digest.data(), bytes.data() + offset + 4,
                entry.name_digest.size());
    std::memcpy(entry.version_digest.data(),
                bytes.data() + offset + 20,
                entry.version_digest.size());
    entry.revision = ReadU32LE(bytes.data() + offset + 36);
    entry.flags = ReadU32LE(bytes.data() + offset + 40);
    entries.push_back(entry);
  }

  return true;
}

bool SaveCatalogRegistryToFile(const std::filesystem::path& path,
                               const std::vector<RealmAddonCatalogEntry>& entries) {
  if (entries.empty()) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return !ec;
  }

  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return false;
    }
  }

  std::vector<std::uint8_t> bytes;
  bytes.reserve(entries.size() * 44);
  for (const auto& entry : entries) {
    WriteU32LE(bytes, entry.id);
    bytes.insert(bytes.end(), entry.name_digest.begin(), entry.name_digest.end());
    bytes.insert(bytes.end(), entry.version_digest.begin(), entry.version_digest.end());
    WriteU32LE(bytes, entry.revision);
    WriteU32LE(bytes, entry.flags);
  }

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return output.good();
}

bool CatalogEntryMatchesAddon(const RealmAddonCatalogEntry& entry,
                              const std::array<std::uint8_t, 16>& name_digest,
                              const std::array<std::uint8_t, 16>& version_digest) {
  if ((entry.flags & 1u) == 0u) {
    return false;
  }
  if (entry.name_digest != name_digest) {
    return false;
  }
  return entry.version_digest == kEmptyStringMd5Digest ||
         entry.version_digest == version_digest;
}

bool CatalogMarksAddonBanned(
    const RealmAddonClientInfo& addon,
    const std::vector<RealmAddonCatalogEntry>& entries) {
  const auto name_digest = ComputeMd5Digest(addon.name);
  const auto version_digest = ComputeMd5Digest(addon.local_version);
  const auto matched_entry = std::find_if(
      entries.begin(),
      entries.end(),
      [&name_digest, &version_digest](const RealmAddonCatalogEntry& entry) {
        return CatalogEntryMatchesAddon(entry, name_digest, version_digest);
      });
  return matched_entry != entries.end();
}

enum class LocalSignatureStatus : std::uint8_t {
  MissingOrUnused = 0,
  InvalidSignatureFile = 1,
  DigestMismatch = 2,
  Match = 3,
};

struct LocalSignatureEvaluation {
  LocalSignatureStatus status{LocalSignatureStatus::MissingOrUnused};
  bool has_content_digest{false};
  std::array<std::uint8_t, 16> content_digest{};
};

inline constexpr std::array<std::uint8_t, 4> kRealmAddonSignatureExponent = {
    0x01, 0x00, 0x01, 0x00,
};

std::filesystem::path ComposeAddonMetadataPath(
    const RealmAddonClientInfo& addon,
    std::string_view extension);

bool IsPathSeparator(const char ch) {
  return ch == '\\' || ch == '/';
}

void TrimTrailingSpaces(std::string& text) {
  while (!text.empty() && text.back() == ' ') {
    text.pop_back();
  }
}

std::string ToUpperAscii(std::string text) {
  for (char& ch : text) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return text;
}

std::string NormalizeAddonReferencePath(std::string path) {
  TrimTrailingSpaces(path);

  bool replaced = true;
  while (replaced) {
    replaced = false;
    for (std::size_t pos = 0; (pos = path.find("..", pos)) != std::string::npos;
         pos += 2) {
      if (pos == 0 || pos + 2 >= path.size() ||
          !IsPathSeparator(path[pos - 1]) || !IsPathSeparator(path[pos + 2])) {
        continue;
      }

      const std::size_t erase_end = pos + 3;
      const std::size_t previous_separator =
          path.substr(0, pos - 1).find_last_of("\\/");
      const std::size_t erase_begin =
          previous_separator == std::string::npos ? 0 : previous_separator + 1;
      path.erase(erase_begin, erase_end - erase_begin);
      replaced = true;
      break;
    }
  }

  for (char& ch : path) {
    if (IsPathSeparator(ch)) {

      ch = static_cast<char>(std::filesystem::path::preferred_separator);
    }
  }

  return path;
}

std::string ParentDirectoryPrefix(const std::string& path) {
  const std::size_t slash = path.find_last_of('/');
  const std::size_t backslash = path.find_last_of('\\');
  const std::size_t separator = std::max(
      slash == std::string::npos ? 0u : slash + 1u,
      backslash == std::string::npos ? 0u : backslash + 1u);
  if (slash == std::string::npos && backslash == std::string::npos) {
    return {};
  }
  return path.substr(0, separator);
}

bool ReadFileBytes(const std::filesystem::path& path,
                   std::vector<std::uint8_t>& bytes) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    bytes.clear();
    return false;
  }

  bytes.assign(std::istreambuf_iterator<char>(input),
               std::istreambuf_iterator<char>());
  return true;
}

bool LoadAddonContentFile(const AddonContentFileLoader& load,
                          const std::string& client_path,
                          std::vector<std::uint8_t>& bytes) {
  if (client_path.empty()) {
    bytes.clear();
    return false;
  }
  if (load && load(client_path, bytes)) {
    return true;
  }

  std::string loose_path(client_path);
  std::replace(loose_path.begin(), loose_path.end(), '\\', '/');
  return ReadFileBytes(std::filesystem::path(loose_path), bytes);
}

std::string ComposeAddonClientPath(const std::string_view addon_name,
                                   const std::string_view leaf) {
  if (addon_name.empty()) {
    return {};
  }
  return "Interface\\AddOns\\" + std::string(addon_name) + "\\" +
         std::string(leaf);
}

bool EqualsNoCase(const std::string_view lhs, const std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
        std::tolower(static_cast<unsigned char>(rhs[index]))) {
      return false;
    }
  }
  return true;
}

void AccumulateAddonReferenceDigest(const AddonContentFileLoader& load,
                                    openwow::core::MD5Context& md5,
                                    const std::string_view reference,
                                    const std::string_view prefix);

void AccumulateTocReferenceDigest(const AddonContentFileLoader& load,
                                  openwow::core::MD5Context& md5,
                                  const std::string_view contents,
                                  const std::string_view parent_prefix) {
  std::size_t cursor = 0;
  while (cursor < contents.size()) {
    while (cursor < contents.size() &&
           (contents[cursor] == '\r' || contents[cursor] == '\n')) {
      ++cursor;
    }
    if (cursor >= contents.size()) {
      break;
    }

    const std::size_t line_start = cursor;
    while (cursor < contents.size() &&
           contents[cursor] != '\r' && contents[cursor] != '\n') {
      ++cursor;
    }
    if (cursor == line_start || contents[line_start] == '#') {
      continue;
    }

    AccumulateAddonReferenceDigest(
        load,
        md5,
        contents.substr(line_start, cursor - line_start),
        parent_prefix);
  }
}

void AccumulateXmlReferenceDigest(const AddonContentFileLoader& load,
                                  openwow::core::MD5Context& md5,
                                  const std::string_view tag_contents,
                                  const std::string_view parent_prefix) {
  std::size_t cursor = 0;
  while (cursor < tag_contents.size() &&
         (tag_contents[cursor] == '\r' || tag_contents[cursor] == '\n' ||
          tag_contents[cursor] == ' ' || tag_contents[cursor] == '\t')) {
    ++cursor;
  }
  const std::size_t tag_start = cursor;
  while (cursor < tag_contents.size() &&
         tag_contents[cursor] != '\r' && tag_contents[cursor] != '\n' &&
         tag_contents[cursor] != ' ' && tag_contents[cursor] != '\t') {
    ++cursor;
  }
  if (tag_start == cursor) {
    return;
  }

  const std::string_view tag_name =
      tag_contents.substr(tag_start, cursor - tag_start);
  if (!EqualsNoCase(tag_name, "Script") &&
      !EqualsNoCase(tag_name, "Include")) {
    return;
  }

  while (cursor < tag_contents.size()) {
    while (cursor < tag_contents.size() &&
           (tag_contents[cursor] == '\r' || tag_contents[cursor] == '\n' ||
            tag_contents[cursor] == ' ' || tag_contents[cursor] == '\t')) {
      ++cursor;
    }
    const std::size_t name_start = cursor;
    while (cursor < tag_contents.size() &&
           tag_contents[cursor] != '\r' && tag_contents[cursor] != '\n' &&
           tag_contents[cursor] != ' ' && tag_contents[cursor] != '\t' &&
           tag_contents[cursor] != '=') {
      ++cursor;
    }
    if (name_start == cursor) {
      return;
    }

    const std::string_view attribute_name =
        tag_contents.substr(name_start, cursor - name_start);
    if (!EqualsNoCase(attribute_name, "file")) {
      if (cursor < tag_contents.size()) {
        ++cursor;
      }
      continue;
    }

    while (cursor < tag_contents.size() &&
           (tag_contents[cursor] == '\r' || tag_contents[cursor] == '\n' ||
            tag_contents[cursor] == ' ' || tag_contents[cursor] == '\t' ||
            tag_contents[cursor] == '=' || tag_contents[cursor] == '"')) {
      ++cursor;
    }
    const std::size_t value_start = cursor;
    while (cursor < tag_contents.size() &&
           tag_contents[cursor] != '\r' && tag_contents[cursor] != '\n' &&
           tag_contents[cursor] != '\t' && tag_contents[cursor] != '"' &&
           tag_contents[cursor] != '/') {
      ++cursor;
    }
    if (value_start == cursor) {
      return;
    }

    AccumulateAddonReferenceDigest(
        load,
        md5,
        tag_contents.substr(value_start, cursor - value_start),
        parent_prefix);
    return;
  }
}

void AccumulateXmlTagDigest(const AddonContentFileLoader& load,
                            openwow::core::MD5Context& md5,
                            const std::string_view contents,
                            const std::string_view parent_prefix) {
  std::size_t cursor = 0;
  while (cursor < contents.size()) {
    const std::size_t open = contents.find('<', cursor);
    if (open == std::string_view::npos) {
      return;
    }
    const std::size_t close = contents.find('>', open + 1);
    if (close == std::string_view::npos) {
      return;
    }
    AccumulateXmlReferenceDigest(
        load, md5, contents.substr(open + 1, close - open - 1), parent_prefix);
    cursor = close + 1;
  }
}

void AccumulateAddonReferenceDigest(const AddonContentFileLoader& load,
                                    openwow::core::MD5Context& md5,
                                    const std::string_view reference,
                                    const std::string_view prefix) {
  std::string resolved = NormalizeAddonReferencePath(
      std::string(prefix) + std::string(reference));
  if (resolved.empty()) {
    return;
  }

  std::vector<std::uint8_t> file_bytes;
  if (!LoadAddonContentFile(load, resolved, file_bytes)) {
    return;
  }

  openwow::core::MD5_Update(&md5, file_bytes.data(), file_bytes.size());
  const std::string parent_prefix = ParentDirectoryPrefix(resolved);
  const std::filesystem::path path(resolved);
  const std::string extension = path.extension().string();
  if (EqualsNoCase(extension, ".toc")) {
    AccumulateTocReferenceDigest(
        load,
        md5,
        std::string_view(reinterpret_cast<const char*>(file_bytes.data()),
                         file_bytes.size()),
        parent_prefix);
    return;
  }
  if (EqualsNoCase(extension, ".xml")) {
    AccumulateXmlTagDigest(
        load,
        md5,
        std::string_view(reinterpret_cast<const char*>(file_bytes.data()),
                         file_bytes.size()),
        parent_prefix);
  }
}

std::array<std::uint8_t, 16> ComputeAddonLocalSignatureDigestByName(
    const AddonContentFileLoader& load, const std::string& addon_name) {
  openwow::core::MD5Context md5{};
  openwow::core::MD5_Init(&md5);

  const std::string toc_path =
      ComposeAddonClientPath(addon_name, addon_name + ".toc");
  if (!toc_path.empty()) {
    AccumulateAddonReferenceDigest(load, md5, toc_path, "");
  }

  std::vector<std::uint8_t> bindings_bytes;
  if (LoadAddonContentFile(load,
                           ComposeAddonClientPath(addon_name, "Bindings.xml"),
                           bindings_bytes)) {
    openwow::core::MD5_Update(&md5, bindings_bytes.data(),
                              bindings_bytes.size());
  }

  std::array<std::uint8_t, 16> digest{};
  openwow::core::MD5_Final(&md5, digest.data());
  return digest;
}

std::array<std::uint8_t, 16> ComputeAddonLocalSignatureDigest(
    const AddonContentFileLoader& load, const RealmAddonClientInfo& addon) {
  return ComputeAddonLocalSignatureDigestByName(load, addon.name);
}

const std::uint8_t* ResolveAddonVerificationKey(
    const RealmAddonClientInfo& addon) {
  if (addon.server_public_key_state == 0) {
    return nullptr;
  }
  if (addon.server_public_key_embedded) {
    return addon.server_public_key.data();
  }
  if (addon.has_public_key) {
    return addon.public_key.data();
  }
  return nullptr;
}

bool VerifySignatureFile(const std::vector<std::uint8_t>& signature_file,
                         const std::string_view signature_name,
                         const std::uint8_t* modulus) {
  if (modulus == nullptr || signature_file.size() != 276u) {
    return false;
  }

  void* signature_context = nullptr;
  openwow::core::SSignature_Create(&signature_context, 256, 4);
  if (signature_context == nullptr) {
    return false;
  }

  openwow::core::SSignature_Update(
      signature_context, signature_file.data(), signature_file.size() - 260u);
  std::string upper_name = ToUpperAscii(std::string(signature_name));
  openwow::core::SSignature_Update(signature_context, upper_name.data(),
                                   upper_name.size());
  openwow::core::SSignature_Update(signature_context,
                                   signature_file.data() + signature_file.size() - 260u,
                                   260u);
  return openwow::core::SSignature_Verify(signature_context, modulus,
                                          kRealmAddonSignatureExponent.data());
}

LocalSignatureEvaluation EvaluateLocalSignatureStatus(
    const AddonContentFileLoader& load, const RealmAddonClientInfo& addon) {
  const std::uint8_t* verification_key = ResolveAddonVerificationKey(addon);
  if (verification_key == nullptr) {
    return {};
  }

  const std::string toc_path =
      ComposeAddonClientPath(addon.name, addon.name + ".toc");
  if (toc_path.empty()) {
    return {};
  }

  std::vector<std::uint8_t> signature_file;
  const std::string signature_path = toc_path + ".sig";
  if (!LoadAddonContentFile(load, signature_path, signature_file)) {
    return {};
  }
  if (signature_file.size() != 276u) {
    return {.status = LocalSignatureStatus::InvalidSignatureFile};
  }

  const std::string signature_name =
      signature_path.substr(ParentDirectoryPrefix(signature_path).size());
  if (!VerifySignatureFile(signature_file, signature_name, verification_key)) {
    return {.status = LocalSignatureStatus::InvalidSignatureFile};
  }

  LocalSignatureEvaluation result;
  result.has_content_digest = true;
  result.content_digest = ComputeAddonLocalSignatureDigest(load, addon);
  result.status = std::equal(result.content_digest.begin(),
                             result.content_digest.end(),
                             signature_file.begin())
                      ? LocalSignatureStatus::Match
                      : LocalSignatureStatus::DigestMismatch;

  return result;
}

std::filesystem::path ComposeAddonMetadataPath(
    const RealmAddonClientInfo& addon,
    const std::string_view extension) {
  if (addon.addon_directory.empty() || addon.name.empty()) {
    return {};
  }
  return addon.addon_directory / (addon.name + std::string(extension));
}

void CreateMetadataParentDirectoryIfNeeded(const std::filesystem::path& path) {
  if (path.empty() || path.parent_path().empty()) {
    return;
  }

  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
}

void PersistServerPublicKeyFile(const RealmAddonClientInfo& addon) {
  if (addon.server_public_key_state == 0 || !addon.server_public_key_embedded) {
    return;
  }

  const auto path = ComposeAddonMetadataPath(addon, ".pub");
  if (path.empty()) {
    return;
  }
  CreateMetadataParentDirectoryIfNeeded(path);

  std::array<std::uint8_t, 257> bytes{};
  bytes[0] = addon.server_public_key_state;
  std::copy(addon.server_public_key.begin(),
            addon.server_public_key.end(),
            bytes.begin() + 1);

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return;
  }
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

void PersistServerUpdateUrlFile(const RealmAddonClientInfo& addon) {
  if (!addon.server_has_url) {
    return;
  }

  const auto path = ComposeAddonMetadataPath(addon, ".url");
  if (path.empty()) {
    return;
  }

  if (addon.server_update_url.empty()) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return;
  }

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return;
  }
  output << "[InternetShortcut]\r\nURL=" << addon.server_update_url << "\r\n";
}

}

std::array<std::uint8_t, 16> ComputeAddonContentDigest(
    const std::string& addon_name, const AddonContentFileLoader& load) {
  return ComputeAddonLocalSignatureDigestByName(load, addon_name);
}

std::uint32_t ComputeRealmAddonInfoCrc32(const std::uint8_t* data,
                                         const std::size_t size) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

RealmAddonHandshakeState& RealmAddonHandshakeState::Instance() {
  static RealmAddonHandshakeState instance;
  return instance;
}

void RealmAddonHandshakeState::RebuildCatalogIndex(
    std::vector<RuntimeCatalogEntry>& entries,
    std::unordered_map<std::uint32_t, std::size_t>& index) {
  index.clear();
  for (std::size_t i = 0; i < entries.size(); ++i) {
    index.emplace(entries[i].entry.id, i);
  }
}

RealmAddonHandshakeState::CatalogLookupResult
RealmAddonHandshakeState::FindOrCreateCatalogEntryById(
    std::vector<RuntimeCatalogEntry>& entries,
    std::unordered_map<std::uint32_t, std::size_t>& index,
    const std::uint32_t id,
    const bool built_in_on_create) {
  if (id == 0) {
    return {};
  }

  const auto existing = index.find(id);
  if (existing != index.end()) {
    return {&entries[existing->second], false};
  }

  RuntimeCatalogEntry runtime_entry{};
  runtime_entry.entry.id = id;
  runtime_entry.built_in = built_in_on_create;
  entries.insert(entries.begin(), std::move(runtime_entry));
  RebuildCatalogIndex(entries, index);
  return {&entries.front(), true};
}

void RealmAddonHandshakeState::SetBuiltinCatalogEntries(
    std::vector<RealmAddonCatalogEntry> entries) {
  std::lock_guard<std::mutex> lock(mutex_);
  builtin_catalog_source_entries_ = std::move(entries);
  catalog_entries_.clear();
  catalog_entry_index_.clear();
  catalog_cache_loaded_ = false;
}

void RealmAddonHandshakeState::BindBuiltinCatalogFromDbc(
    const openwow::data::dbc::DbcLoader* dbc) {
  std::vector<RealmAddonCatalogEntry> entries;
  if (dbc != nullptr) {
    const auto& banned_addons = dbc->banned_addons().entries();
    entries.reserve(banned_addons.size());
    for (const auto& source_entry : banned_addons) {
      entries.push_back(CatalogEntryFromDbc(source_entry));
    }
  }
  SetBuiltinCatalogEntries(std::move(entries));
}

void RealmAddonHandshakeState::SetClientAddons(
    std::vector<RealmAddonClientInfo> addons) {
  std::lock_guard<std::mutex> lock(mutex_);
  client_addons_ = std::move(addons);
}

void RealmAddonHandshakeState::SetRuntimePorts(RealmAddonHandshakePorts ports) {
  std::lock_guard<std::mutex> lock(mutex_);
  runtime_ports_ = std::move(ports);
}

void RealmAddonHandshakeState::ClearRuntimePorts() {
  std::lock_guard<std::mutex> lock(mutex_);
  runtime_ports_ = {};
}

void RealmAddonHandshakeState::SetCatalogEntries(
    std::vector<RealmAddonCatalogEntry> entries) {
  std::lock_guard<std::mutex> lock(mutex_);
  injected_cache_file_entries_ = std::move(entries);
  has_injected_cache_file_entries_ = true;
  catalog_entries_.clear();
  catalog_entry_index_.clear();
  catalog_cache_loaded_ = false;
}

std::vector<RealmAddonClientInfo>
RealmAddonHandshakeState::SnapshotClientAddons() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return client_addons_;
}

std::vector<RealmAddonCatalogEntry>
RealmAddonHandshakeState::SnapshotCatalogEntries() const {
  auto* self = const_cast<RealmAddonHandshakeState*>(this);
  self->LoadCatalogCacheIfNeeded();

  std::lock_guard<std::mutex> lock(mutex_);
  return SnapshotCatalogEntriesLocked();
}

std::uint32_t RealmAddonHandshakeState::catalog_revision_max() const {
  auto* self = const_cast<RealmAddonHandshakeState*>(this);
  self->LoadCatalogCacheIfNeeded();

  std::lock_guard<std::mutex> lock(mutex_);
  return catalog_revision_max_locked();
}

std::uint32_t RealmAddonHandshakeState::GetAddonSecurity(
    const std::string& addon_name) const {
  auto* self = const_cast<RealmAddonHandshakeState*>(this);
  self->LoadClientAddonsIfEmpty();

  std::lock_guard<std::mutex> lock(mutex_);
  const auto addon_it = std::find_if(
      client_addons_.begin(),
      client_addons_.end(),
      [&addon_name](const RealmAddonClientInfo& addon) {
        return addon.name == addon_name;
      });
  if (addon_it == client_addons_.end()) {
    return 1;
  }
  return addon_it->security_level;
}

std::vector<std::uint8_t> RealmAddonHandshakeState::BuildSerializedClientInfo()
    const {
  auto* self = const_cast<RealmAddonHandshakeState*>(this);
  self->LoadClientAddonsIfEmpty();
  self->LoadCatalogCacheIfNeeded();

  std::vector<std::uint8_t> inner;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (client_addons_.empty()) {
      return {};
    }

    AppendU32(inner, 0);
    std::uint32_t enabled_count = 0;
    for (const auto& addon : client_addons_) {

      if (!addon.is_secure) {
        continue;
      }

      const std::uint8_t public_key_state = addon.public_key_state;
      AppendCString(inner, addon.name);
      AppendU8(inner, public_key_state);
      AppendU32(inner,
                public_key_state != 0
                    ? ComputeRealmAddonInfoCrc32(addon.public_key.data(),
                                                 addon.public_key.size())
                    : 0u);
      AppendU32(
          inner,
          addon.update_url.empty()
              ? 0u
              : ComputeRealmAddonInfoCrc32(
                    reinterpret_cast<const std::uint8_t*>(addon.update_url.data()),
                    addon.update_url.size()));
      ++enabled_count;
    }

    std::memcpy(inner.data(), &enabled_count, sizeof(enabled_count));
    AppendU32(inner, catalog_revision_max_locked());
  }

  std::vector<std::uint8_t> outer;
  AppendU32(outer, static_cast<std::uint32_t>(inner.size()));

  const auto compressed =
      openwow::net::PacketCompression::Compress(inner.data(), inner.size());
  outer.insert(outer.end(), compressed.begin(), compressed.end());
  return outer;
}

bool RealmAddonHandshakeState::ProcessServerInfo(const std::uint8_t* data,
                                                 const std::size_t size,
                                                 std::size_t* consumed) {
  LoadClientAddonsIfEmpty();
  LoadCatalogCacheIfNeeded();

  std::unique_lock<std::mutex> lock(mutex_);

  std::vector<RealmAddonClientInfo> addons = client_addons_;
  std::vector<RuntimeCatalogEntry> working_catalog = catalog_entries_;
  std::unordered_map<std::uint32_t, std::size_t> working_index =
      catalog_entry_index_;

  std::size_t pos = 0;
  const auto fail = [&]() {
    if (consumed != nullptr) {
      *consumed = pos;
    }
    return false;
  };

  for (auto& addon : addons) {

    if (!addon.is_secure) {
      continue;
    }

    if (!ReadU8(data, size, pos, addon.server_state)) {
      return fail();
    }

    std::uint8_t has_public_key = 0;
    if (!ReadU8(data, size, pos, has_public_key)) {
      return fail();
    }
    addon.server_public_key_state = has_public_key;
    addon.server_has_public_key = (has_public_key != 0);
    addon.server_public_key_embedded = false;
    addon.server_public_key.fill(0);
    addon.server_public_key_revision = 0;
    addon.server_public_key_newer = false;
    addon.security_level = 1;
    addon.is_corrupt = false;

    if (addon.server_has_public_key) {
      std::uint8_t public_key_embedded = 0;
      if (!ReadU8(data, size, pos, public_key_embedded)) {
        return fail();
      }
      addon.server_public_key_embedded = (public_key_embedded != 0);
      if (addon.server_public_key_embedded &&
          !ReadBytes(data,
                     size,
                     pos,
                     addon.server_public_key.data(),
                     addon.server_public_key.size())) {
        return fail();
      }

      if (!ReadU32(data, size, pos, addon.server_public_key_revision)) {
        return fail();
      }
      addon.server_public_key_newer =
          addon.server_public_key_revision > addon.public_key_revision;
    }
    addon.public_key_state = addon.server_public_key_state;
    addon.has_public_key = addon.server_has_public_key;
    if (addon.server_public_key_embedded) {
      addon.public_key = addon.server_public_key;
    }

    std::uint8_t has_url = 0;
    if (!ReadU8(data, size, pos, has_url)) {
      return fail();
    }
    addon.server_has_url = (has_url != 0);
    addon.server_update_url.clear();
    if (addon.server_has_url &&
        !ReadCString(data, size, pos, addon.server_update_url, 0x100)) {
      return fail();
    }

    PersistServerPublicKeyFile(addon);
    PersistServerUpdateUrlFile(addon);
  }

  std::uint32_t catalog_count = 0;
  if (!ReadU32(data, size, pos, catalog_count)) {
    return fail();
  }

  for (std::uint32_t i = 0; i < catalog_count; ++i) {
    RealmAddonCatalogEntry entry{};
    if (!ReadU32(data, size, pos, entry.id) ||
        !ReadBytes(data, size, pos, entry.name_digest.data(), entry.name_digest.size()) ||
        !ReadBytes(data, size, pos, entry.version_digest.data(), entry.version_digest.size()) ||
        !ReadU32(data, size, pos, entry.revision) ||
        !ReadU32(data, size, pos, entry.flags)) {
      return fail();
    }
    auto lookup = FindOrCreateCatalogEntryById(
        working_catalog, working_index, entry.id, false);
    if (lookup.entry == nullptr) {
      continue;
    }

    lookup.entry->built_in = false;
    lookup.entry->entry = entry;
  }

  std::vector<RealmAddonCatalogEntry> effective_catalog;
  effective_catalog.reserve(working_catalog.size());
  for (const auto& runtime_entry : working_catalog) {
    effective_catalog.push_back(runtime_entry.entry);
  }
  for (auto& addon : addons) {

    if (!addon.is_secure) {
      continue;
    }

    addon.security_level = 1;
    addon.is_corrupt = false;
    addon.has_secure_content_digest = false;
    addon.secure_content_digest.fill(0);
    if (addon.server_state == 0) {
      addon.security_level = 2;
    } else {
      const LocalSignatureEvaluation signature =
          EvaluateLocalSignatureStatus(runtime_ports_.load_client_file, addon);
      addon.has_secure_content_digest = signature.has_content_digest;
      addon.secure_content_digest = signature.content_digest;
      switch (signature.status) {
        case LocalSignatureStatus::Match:
          addon.security_level = 0;
          break;
        case LocalSignatureStatus::DigestMismatch:
          addon.is_corrupt = true;
          break;
        case LocalSignatureStatus::MissingOrUnused:
        case LocalSignatureStatus::InvalidSignatureFile:
          break;
      }
    }
    if (CatalogMarksAddonBanned(addon, effective_catalog)) {
      addon.security_level = 2;
    }
  }

  client_addons_ = std::move(addons);
  const auto synchronized_addons = client_addons_;
  const auto synchronize_server_info = runtime_ports_.synchronize_server_info;
  const auto resolve_cache_path = runtime_ports_.resolve_catalog_cache_file_path;
  const auto cache_path_override = catalog_cache_file_path_override_;
  std::vector<RealmAddonCatalogEntry> persisted_overlay;
  persisted_overlay.reserve(working_catalog.size());
  for (const auto& runtime_entry : working_catalog) {

    if (!runtime_entry.built_in) {
      persisted_overlay.push_back(runtime_entry.entry);
    }
  }

  catalog_entries_.clear();
  catalog_entry_index_.clear();
  injected_cache_file_entries_.clear();
  has_injected_cache_file_entries_ = false;
  catalog_cache_loaded_ = false;
  if (consumed != nullptr) {
    *consumed = pos;
  }
  lock.unlock();

  if (synchronize_server_info) {
    for (const auto& addon : synchronized_addons) {
      synchronize_server_info(addon);
    }
  }
  const std::filesystem::path cache_path =
      cache_path_override.empty()
          ? (resolve_cache_path ? resolve_cache_path() : std::filesystem::path{})
          : cache_path_override;
  (void)SaveCatalogRegistryToFile(cache_path, persisted_overlay);
  return true;
}

void RealmAddonHandshakeState::ResetForTests() {
  std::lock_guard<std::mutex> lock(mutex_);
  client_addons_.clear();
  builtin_catalog_source_entries_.clear();
  injected_cache_file_entries_.clear();
  has_injected_cache_file_entries_ = false;
  catalog_entries_.clear();
  catalog_entry_index_.clear();
  catalog_cache_file_path_override_.clear();
  catalog_cache_loaded_ = true;
}

void RealmAddonHandshakeState::SetCatalogCacheFilePathForTests(
    std::filesystem::path path) {
  std::lock_guard<std::mutex> lock(mutex_);
  catalog_cache_file_path_override_ = std::move(path);
  injected_cache_file_entries_.clear();
  has_injected_cache_file_entries_ = false;
  catalog_entries_.clear();
  catalog_entry_index_.clear();
  catalog_cache_loaded_ = false;
}

void RealmAddonHandshakeState::LoadClientAddonsIfEmpty() {
  std::function<std::vector<RealmAddonClientInfo>()> discover;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_addons_.empty() || !runtime_ports_.discover_client_addons) {
      return;
    }
    discover = runtime_ports_.discover_client_addons;
  }

  auto imported = discover();

  std::lock_guard<std::mutex> lock(mutex_);
  if (client_addons_.empty()) {
    client_addons_ = std::move(imported);
  }
}

void RealmAddonHandshakeState::LoadCatalogCacheIfNeeded() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (catalog_cache_loaded_) {
    return;
  }

  std::vector<RealmAddonCatalogEntry> cache_file_entries;
  if (has_injected_cache_file_entries_) {
    cache_file_entries = injected_cache_file_entries_;
  } else {
    const std::filesystem::path cache_path =
        catalog_cache_file_path_override_.empty()
            ? (runtime_ports_.resolve_catalog_cache_file_path
                   ? runtime_ports_.resolve_catalog_cache_file_path()
                   : std::filesystem::path{})
            : catalog_cache_file_path_override_;
    (void)LoadCatalogFileRecords(cache_path, cache_file_entries);
  }

  RebuildCatalogFromSourcesLocked(cache_file_entries);
  catalog_cache_loaded_ = true;
}

void RealmAddonHandshakeState::RebuildCatalogFromSourcesLocked(
    const std::vector<RealmAddonCatalogEntry>& cache_file_entries) {
  catalog_entries_.clear();
  catalog_entry_index_.clear();

  for (const auto& source_entry : builtin_catalog_source_entries_) {
    auto lookup =
        FindOrCreateCatalogEntryById(catalog_entries_, catalog_entry_index_,
                                     source_entry.id, true);
    if (lookup.entry == nullptr) {
      continue;
    }
    if (lookup.created || lookup.entry->entry.revision <= source_entry.revision) {
      lookup.entry->built_in = true;
      lookup.entry->entry = source_entry;
    }
  }

  for (auto it = cache_file_entries.rbegin(); it != cache_file_entries.rend(); ++it) {
    auto lookup =
        FindOrCreateCatalogEntryById(catalog_entries_, catalog_entry_index_,
                                     it->id, false);
    if (lookup.entry == nullptr) {
      continue;
    }
    if (lookup.created || lookup.entry->entry.revision <= it->revision) {
      lookup.entry->built_in = false;
      lookup.entry->entry = *it;
    }
  }
}

std::uint32_t RealmAddonHandshakeState::catalog_revision_max_locked() const {
  std::uint32_t revision = 0;
  for (const auto& runtime_entry : catalog_entries_) {
    revision = std::max(revision, runtime_entry.entry.revision);
  }
  return revision;
}

std::vector<RealmAddonCatalogEntry>
RealmAddonHandshakeState::SnapshotCatalogEntriesLocked() const {
  std::vector<RealmAddonCatalogEntry> entries;
  entries.reserve(catalog_entries_.size());
  for (const auto& runtime_entry : catalog_entries_) {
    entries.push_back(runtime_entry.entry);
  }
  return entries;
}

}
