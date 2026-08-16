#include "openwow/vfs/virtual_file_system.h"

#include "openwow/vfs/adapters/mpq/mpq_archive.h"
#include "openwow/vfs/client_path_identity.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <future>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#include "openwow/foundation/text/ascii.h"

namespace openwow::vfs {

using openwow::text::ToLowerAscii;

namespace {

constexpr std::size_t kMaxResolvedPathEntries = 65'536;
constexpr std::size_t kMaxEnumerationEntries = 128;

std::atomic<std::uint64_t> g_next_mount_view_revision{1};

std::uint64_t NextMountViewRevision() {
  return g_next_mount_view_revision.fetch_add(1, std::memory_order_relaxed);
}

void HashCombine(std::size_t& seed, const std::size_t value) {
  seed ^= value + static_cast<std::size_t>(0x9e3779b9u) + (seed << 6u) +
          (seed >> 2u);
}

struct PathCacheKey {
  std::uint64_t mount_view_revision{0};
  std::string normalized_path;

  bool operator==(const PathCacheKey&) const = default;
};

struct PathCacheKeyHash {
  std::size_t operator()(const PathCacheKey& key) const noexcept {
    std::size_t hash = std::hash<std::uint64_t>{}(key.mount_view_revision);
    HashCombine(hash, std::hash<std::string>{}(key.normalized_path));
    return hash;
  }
};

struct EnumerationCacheKey {
  std::uint64_t mount_view_revision{0};
  std::string normalized_root;
  bool recursive{false};
  std::optional<MountKind> mount_kind;

  bool operator==(const EnumerationCacheKey&) const = default;
};

struct EnumerationCacheKeyHash {
  std::size_t operator()(const EnumerationCacheKey& key) const noexcept {
    std::size_t hash = std::hash<std::uint64_t>{}(key.mount_view_revision);
    HashCombine(hash, std::hash<std::string>{}(key.normalized_root));
    HashCombine(hash, std::hash<bool>{}(key.recursive));
    HashCombine(hash, key.mount_kind.has_value()
                          ? static_cast<std::size_t>(*key.mount_kind) + 1u
                          : 0u);
    return hash;
  }
};

std::string MpqCacheKey(const MountPoint& mount) {
  std::string key = mount.source_root.lexically_normal().string();
  if (!mount.mpq_patches.empty()) {
    key += "|patches=";
    for (const auto& patch : mount.mpq_patches) {
      key += patch.lexically_normal().string();
      key.push_back(';');
    }
  }
  return key;
}

std::shared_ptr<mpq::MpqArchive> OpenMpqArchive(
    const MountPoint& mount) {
  auto archive = std::make_shared<mpq::MpqArchive>();
  if (!archive->Open(mount.source_root)) {
    return nullptr;
  }
  for (const auto& patch : mount.mpq_patches) {
    (void)archive->ApplyPatch(patch);
  }
  return archive;
}

}

struct VirtualFileSystem::ResolvedPath {
  enum class Kind : std::uint8_t {
    kMissing,
    kFilesystem,
    kMpqArchive,
  };

  Kind kind{Kind::kMissing};
  std::size_t mount_index{0};
  std::filesystem::path filesystem_path;
  std::shared_ptr<mpq::MpqArchive> archive;
};

struct VirtualFileSystem::CacheState {
  std::mutex archives_mutex;
  std::unordered_map<
      std::string,
      std::shared_future<std::shared_ptr<mpq::MpqArchive>>>
      archives;

  std::mutex filesystem_mutex;
  std::unordered_map<std::string, std::filesystem::path> filesystem_paths;

  std::shared_mutex resolved_paths_mutex;
  std::unordered_map<PathCacheKey, ResolvedPath, PathCacheKeyHash>
      resolved_paths;

  std::mutex enumerations_mutex;
  std::unordered_map<EnumerationCacheKey,
                     std::shared_future<std::vector<std::filesystem::path>>,
                     EnumerationCacheKeyHash>
      enumerations;
};

VirtualFileSystem::VirtualFileSystem()
    : cache_state_(std::make_shared<CacheState>()) {}

VirtualFileSystem::~VirtualFileSystem() = default;

void VirtualFileSystem::Mount(const MountPoint& mount) {
  mounts_.push_back(mount);
  std::sort(mounts_.begin(), mounts_.end(), [](const MountPoint& a, const MountPoint& b) {
    if (a.priority != b.priority) {
      return a.priority > b.priority;
    }
    return a.id < b.id;
  });
  mount_view_revision_ = NextMountViewRevision();
}

std::vector<MountPoint> VirtualFileSystem::mounts() const {
  return mounts_;
}

void VirtualFileSystem::InvalidateLookupCaches() {
  mount_view_revision_ = NextMountViewRevision();

  {
    std::lock_guard lock(cache_state_->filesystem_mutex);
    cache_state_->filesystem_paths.clear();
  }
  {
    std::unique_lock lock(cache_state_->resolved_paths_mutex);
    cache_state_->resolved_paths.clear();
  }
  {
    std::lock_guard lock(cache_state_->enumerations_mutex);
    cache_state_->enumerations.clear();
  }
}

void VirtualFileSystem::PrewarmMpqArchives() const {
  for (const auto& mount : mounts_) {
    if (!mount.enabled || mount.kind != MountKind::kMpqArchive ||
        mount.source_root.empty()) {
      continue;
    }

    const std::string key = MpqCacheKey(mount);
    std::lock_guard lock(cache_state_->archives_mutex);
    if (cache_state_->archives.contains(key)) {
      continue;
    }
    cache_state_->archives.emplace(
        key,
        std::async(std::launch::async,
                   [mount]() { return OpenMpqArchive(mount); })
            .share());
  }
}

void VirtualFileSystem::PrewarmFileEnumeration(
    const std::string& virtual_path_root,
    const bool recursive) const {
  const auto normalized_root = NormalizeVirtualPath(virtual_path_root);
  if (normalized_root.empty()) {
    return;
  }

  EnumerationCacheKey key{
      .mount_view_revision = mount_view_revision_,
      .normalized_root = normalized_root,
      .recursive = recursive,
  };
  std::lock_guard lock(cache_state_->enumerations_mutex);
  if (cache_state_->enumerations.contains(key)) {
    return;
  }
  if (cache_state_->enumerations.size() >= kMaxEnumerationEntries) {
    cache_state_->enumerations.clear();
  }

  VirtualFileSystem snapshot = *this;
  cache_state_->enumerations.emplace(
      std::move(key),
      std::async(std::launch::async,
                 [snapshot = std::move(snapshot), normalized_root, recursive]() {
                   return snapshot.EnumerateFilesUncached(normalized_root,
                                                          recursive,
                                                          std::nullopt);
                 })
          .share());
}

bool VirtualFileSystem::CanOpenMpqMount(const std::string_view mount_id) const {
  for (const auto& mount : mounts_) {
    if (mount.id != mount_id || !mount.enabled ||
        mount.kind != MountKind::kMpqArchive || mount.source_root.empty()) {
      continue;
    }
    if (AcquireMpqArchive(mount) != nullptr) {
      return true;
    }
  }
  return false;
}

bool VirtualFileSystem::Exists(const std::string& virtual_path) const {
  const auto normalized = NormalizeVirtualPath(virtual_path);
  if (normalized.empty()) {
    return false;
  }
  return ResolvePath(normalized).kind != ResolvedPath::Kind::kMissing;
}

std::optional<std::filesystem::path> VirtualFileSystem::Resolve(const std::string& virtual_path) const {
  const auto identity = MakeClientPathIdentity(virtual_path);
  if (identity.empty()) {
    return std::nullopt;
  }

  const auto resolved = ResolvePath(identity.lookup_path);
  if (resolved.kind == ResolvedPath::Kind::kFilesystem) {
    return resolved.filesystem_path;
  }
  if (resolved.kind == ResolvedPath::Kind::kMpqArchive &&
      resolved.mount_index < mounts_.size()) {
    return std::filesystem::path(
        mounts_[resolved.mount_index].source_root.string() + "::" +
        identity.display_path);
  }
  return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> VirtualFileSystem::ReadFileBytes(
    const std::string& virtual_path) const {
  return ReadFileBytesImpl(virtual_path, std::nullopt);
}

std::optional<std::vector<std::uint8_t>> VirtualFileSystem::ReadFilePrefix(
    const std::string& virtual_path, const std::size_t max_bytes) const {
  return ReadFileBytesImpl(virtual_path, max_bytes);
}

std::optional<std::vector<std::uint8_t>> VirtualFileSystem::ReadFileBytesImpl(
    const std::string& virtual_path,
    const std::optional<std::size_t> max_bytes) const {
  const auto normalized = NormalizeVirtualPath(virtual_path);
  if (normalized.empty()) {
    return std::nullopt;
  }

  const auto read_filesystem = [max_bytes](const std::filesystem::path& path)
      -> std::optional<std::vector<std::uint8_t>> {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input.is_open()) {
      return std::nullopt;
    }
    if (!max_bytes.has_value()) {
      return std::vector<std::uint8_t>(
          (std::istreambuf_iterator<char>(input)),
          std::istreambuf_iterator<char>());
    }
    input.seekg(0, std::ios::end);
    const std::streamoff file_size = input.tellg();
    if (file_size < 0) {
      return std::nullopt;
    }
    input.seekg(0, std::ios::beg);
    if (!input.good()) {
      return std::nullopt;
    }
    const auto stream_limit = static_cast<std::uintmax_t>(
        std::numeric_limits<std::streamsize>::max());
    const auto file_size_unsigned = static_cast<std::uintmax_t>(file_size);
    const auto requested = static_cast<std::size_t>(std::min<std::uintmax_t>(
        {*max_bytes, file_size_unsigned, stream_limit}));
    std::vector<std::uint8_t> bytes(requested);
    if (!bytes.empty()) {
      input.read(reinterpret_cast<char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    }
    bytes.resize(static_cast<std::size_t>(input.gcount()));
    return bytes;
  };

  if (const auto cached = FindCachedPath(normalized); cached.has_value()) {
    if (cached->kind == ResolvedPath::Kind::kMissing) {
      return std::nullopt;
    }
    if (cached->kind == ResolvedPath::Kind::kMpqArchive && cached->archive) {
      auto bytes = max_bytes.has_value()
                       ? cached->archive->ReadFilePrefix(normalized, *max_bytes)
                       : cached->archive->ReadFile(normalized);
      if (bytes.has_value()) {
        return bytes;
      }
    } else if (cached->kind == ResolvedPath::Kind::kFilesystem) {
      if (auto bytes = read_filesystem(cached->filesystem_path);
          bytes.has_value()) {
        return bytes;
      }
    }

    EraseCachedPath(normalized);
  }

  for (std::size_t mount_index = 0; mount_index < mounts_.size();
       ++mount_index) {
    const auto& mount = mounts_[mount_index];
    if (!mount.enabled || mount.source_root.empty()) {
      continue;
    }

    if (mount.kind == MountKind::kMpqArchive) {
      const auto archive = AcquireMpqArchive(mount);
      if (!archive) {
        continue;
      }

      if (archive->HasDeleteMarker(normalized)) {
        CachePath(normalized, ResolvedPath{});
        return std::nullopt;
      }
      const auto bytes = max_bytes.has_value()
                             ? archive->ReadFilePrefix(normalized, *max_bytes)
                             : archive->ReadFile(normalized);
      if (bytes.has_value()) {
        CachePath(normalized,
                  ResolvedPath{.kind = ResolvedPath::Kind::kMpqArchive,
                               .mount_index = mount_index,
                               .archive = archive});
        return bytes;
      }
      continue;
    }

    const auto candidate = ResolveFilesystemPathCaseInsensitive(mount, normalized);
    if (!candidate.has_value()) {
      continue;
    }
    if (auto bytes = read_filesystem(*candidate); bytes.has_value()) {
      CachePath(normalized,
                ResolvedPath{.kind = ResolvedPath::Kind::kFilesystem,
                             .mount_index = mount_index,
                             .filesystem_path = *candidate});
      return bytes;
    }
  }

  CachePath(normalized, ResolvedPath{});
  return std::nullopt;
}

std::optional<std::string> VirtualFileSystem::ReadTextFile(const std::string& virtual_path) const {
  const auto bytes = ReadFileBytes(virtual_path);
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  return std::string(bytes->begin(), bytes->end());
}

std::vector<std::filesystem::path> VirtualFileSystem::EnumerateFiles(
    const std::string& virtual_path_root,
    const bool recursive) const {
  return EnumerateFilesImpl(virtual_path_root, recursive, std::nullopt);
}

std::vector<std::filesystem::path>
VirtualFileSystem::EnumerateFilesByMountKind(
    const std::string& virtual_path_root,
    const bool recursive,
    const MountKind mount_kind) const {
  return EnumerateFilesImpl(virtual_path_root, recursive, mount_kind);
}

std::vector<std::filesystem::path> VirtualFileSystem::EnumerateFilesImpl(
    const std::string& virtual_path_root,
    const bool recursive,
    const std::optional<MountKind> mount_kind) const {
  const auto normalized_root = NormalizeVirtualPath(virtual_path_root);
  if (normalized_root.empty()) {
    return {};
  }

  EnumerationCacheKey cache_key{
      .mount_view_revision = mount_view_revision_,
      .normalized_root = normalized_root,
      .recursive = recursive,
      .mount_kind = mount_kind,
  };
  std::shared_future<std::vector<std::filesystem::path>> future;
  std::optional<std::promise<std::vector<std::filesystem::path>>> producer;
  {
    std::lock_guard lock(cache_state_->enumerations_mutex);
    if (const auto cached = cache_state_->enumerations.find(cache_key);
        cached != cache_state_->enumerations.end()) {
      future = cached->second;
    } else {
      if (cache_state_->enumerations.size() >= kMaxEnumerationEntries) {
        cache_state_->enumerations.clear();
      }
      producer.emplace();
      future = producer->get_future().share();
      cache_state_->enumerations.emplace(std::move(cache_key), future);
    }
  }

  if (producer.has_value()) {
    try {
      producer->set_value(
          EnumerateFilesUncached(normalized_root, recursive, mount_kind));
    } catch (...) {
      producer->set_exception(std::current_exception());
    }
  }

  try {
    return future.get();
  } catch (...) {
    return {};
  }
}

std::vector<std::filesystem::path> VirtualFileSystem::EnumerateFilesUncached(
    const std::string& normalized_root,
    const bool recursive,
    const std::optional<MountKind> mount_kind) const {
  std::vector<std::vector<std::filesystem::path>> files_by_mount(mounts_.size());

  if (!mount_kind.has_value() || *mount_kind == MountKind::kMpqArchive) {
    PrewarmMpqArchives();
  }
  std::vector<std::pair<std::size_t, std::future<std::vector<std::string>>>>
      archive_enumerations;
  archive_enumerations.reserve(mounts_.size());

  std::vector<std::pair<std::size_t, std::future<std::vector<std::string>>>>
      archive_delete_markers;
  archive_delete_markers.reserve(mounts_.size());

  for (std::size_t mount_index = 0; mount_index < mounts_.size();
       ++mount_index) {
    const auto& mount = mounts_[mount_index];
    if (!mount.enabled || mount.source_root.empty() ||
        (mount_kind.has_value() && mount.kind != *mount_kind)) {
      continue;
    }

    if (mount.kind == MountKind::kMpqArchive) {
      const auto archive = AcquireMpqArchive(mount);
      if (!archive) {
        continue;
      }
      archive_enumerations.emplace_back(
          mount_index,
          std::async(std::launch::async,
                     [archive, normalized_root, recursive]() {
                       return archive->EnumerateFiles(normalized_root,
                                                      recursive);
                     }));
      archive_delete_markers.emplace_back(
          mount_index,
          std::async(std::launch::async,
                     [archive, normalized_root, recursive]() {
                       return archive->EnumerateDeleteMarkedFiles(
                           normalized_root, recursive);
                     }));
      continue;
    }

    const auto root_opt = ResolveFilesystemPathCaseInsensitive(mount, normalized_root);
    if (!root_opt.has_value()) {
      continue;
    }
    const auto root = *root_opt;
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) {
      continue;
    }

    if (recursive) {
      for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) {
          break;
        }
        if (entry.is_regular_file()) {
          const auto relative =
              std::filesystem::relative(entry.path(), mount.source_root, ec).generic_string();
          if (ec) {
            continue;
          }
          files_by_mount[mount_index].emplace_back("/" + relative);
        }
      }
    } else {
      for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) {
          break;
        }
        if (entry.is_regular_file()) {
          const auto relative =
              std::filesystem::relative(entry.path(), mount.source_root, ec).generic_string();
          if (ec) {
            continue;
          }
          files_by_mount[mount_index].emplace_back("/" + relative);
        }
      }
    }
  }

  for (auto& [mount_index, enumeration] : archive_enumerations) {
    try {
      for (auto& file : enumeration.get()) {
        files_by_mount[mount_index].emplace_back(std::move(file));
      }
    } catch (...) {

    }
  }
  std::vector<std::vector<std::string>> markers_by_mount(mounts_.size());
  for (auto& [mount_index, markers] : archive_delete_markers) {
    try {
      markers_by_mount[mount_index] = markers.get();
    } catch (...) {

    }
  }

  std::size_t candidate_count = 0;
  for (const auto& mount_files : files_by_mount) {
    candidate_count += mount_files.size();
  }
  std::unordered_set<std::string> seen;
  seen.reserve(candidate_count);
  std::vector<std::pair<std::string, std::filesystem::path>> winners;
  winners.reserve(candidate_count);
  for (std::size_t mount_index = 0; mount_index < files_by_mount.size();
       ++mount_index) {

    for (const auto& marker : markers_by_mount[mount_index]) {
      auto identity = MakeClientPathIdentity(marker);
      if (!identity.empty()) {
        seen.insert(std::move(identity.lookup_path));
      }
    }
    for (auto& file : files_by_mount[mount_index]) {
      auto identity = MakeClientPathIdentity(file.generic_string());
      if (identity.empty() || !seen.insert(identity.lookup_path).second) {
        continue;
      }
      winners.emplace_back(std::move(identity.lookup_path),
                           std::move(identity.display_path));
    }
  }
  std::sort(winners.begin(), winners.end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.first < rhs.first;
            });

  std::vector<std::filesystem::path> files;
  files.reserve(winners.size());
  for (auto& [lookup_path, display_path] : winners) {
    (void)lookup_path;
    files.push_back(std::move(display_path));
  }
  return files;
}

std::string VirtualFileSystem::NormalizeVirtualPath(const std::string& virtual_path) {
  return NormalizeClientLookupPath(virtual_path);
}

std::optional<VirtualFileSystem::ResolvedPath>
VirtualFileSystem::FindCachedPath(
    const std::string& normalized_virtual_path) const {
  const PathCacheKey key{
      .mount_view_revision = mount_view_revision_,
      .normalized_path = normalized_virtual_path,
  };
  std::shared_lock lock(cache_state_->resolved_paths_mutex);
  const auto cached = cache_state_->resolved_paths.find(key);
  if (cached == cache_state_->resolved_paths.end()) {
    return std::nullopt;
  }
  return cached->second;
}

VirtualFileSystem::ResolvedPath VirtualFileSystem::ResolvePath(
    const std::string& normalized_virtual_path) const {
  if (const auto cached = FindCachedPath(normalized_virtual_path);
      cached.has_value()) {
    if (cached->kind != ResolvedPath::Kind::kFilesystem) {
      return *cached;
    }

    std::error_code ec;
    if (std::filesystem::exists(cached->filesystem_path, ec)) {
      return *cached;
    }

    EraseCachedPath(normalized_virtual_path);
  }

  auto resolved = ResolvePathUncached(normalized_virtual_path);
  CachePath(normalized_virtual_path, resolved);
  return resolved;
}

VirtualFileSystem::ResolvedPath VirtualFileSystem::ResolvePathUncached(
    const std::string& normalized_virtual_path) const {
  for (std::size_t mount_index = 0; mount_index < mounts_.size();
       ++mount_index) {
    const auto& mount = mounts_[mount_index];
    if (!mount.enabled || mount.source_root.empty()) {
      continue;
    }

    if (mount.kind == MountKind::kMpqArchive) {
      const auto archive = AcquireMpqArchive(mount);
      if (!archive) {
        continue;
      }

      if (archive->HasDeleteMarker(normalized_virtual_path)) {
        return ResolvedPath{};
      }
      if (archive->Exists(normalized_virtual_path)) {
        return ResolvedPath{
            .kind = ResolvedPath::Kind::kMpqArchive,
            .mount_index = mount_index,
            .archive = archive,
        };
      }
      continue;
    }

    const auto candidate = ResolveFilesystemPathCaseInsensitive(
        mount, normalized_virtual_path);
    if (!candidate.has_value()) {
      continue;
    }
    std::error_code ec;
    if (std::filesystem::exists(*candidate, ec)) {
      return ResolvedPath{
          .kind = ResolvedPath::Kind::kFilesystem,
          .mount_index = mount_index,
          .filesystem_path = *candidate,
      };
    }
  }
  return {};
}

void VirtualFileSystem::CachePath(
    const std::string& normalized_virtual_path,
    const ResolvedPath& resolved) const {
  PathCacheKey key{
      .mount_view_revision = mount_view_revision_,
      .normalized_path = normalized_virtual_path,
  };
  std::unique_lock lock(cache_state_->resolved_paths_mutex);
  if (cache_state_->resolved_paths.size() >= kMaxResolvedPathEntries &&
      !cache_state_->resolved_paths.contains(key)) {

    cache_state_->resolved_paths.clear();
  }
  cache_state_->resolved_paths.insert_or_assign(std::move(key), resolved);
}

void VirtualFileSystem::EraseCachedPath(
    const std::string& normalized_virtual_path) const {
  const PathCacheKey key{
      .mount_view_revision = mount_view_revision_,
      .normalized_path = normalized_virtual_path,
  };
  std::unique_lock lock(cache_state_->resolved_paths_mutex);
  cache_state_->resolved_paths.erase(key);
}

std::shared_ptr<mpq::MpqArchive> VirtualFileSystem::AcquireMpqArchive(
    const MountPoint& mount) const {
  const std::string key = MpqCacheKey(mount);
  std::shared_future<std::shared_ptr<mpq::MpqArchive>> future;
  std::optional<std::promise<
      std::shared_ptr<mpq::MpqArchive>>>
      producer;

  {
    std::lock_guard lock(cache_state_->archives_mutex);
    const auto cached = cache_state_->archives.find(key);
    if (cached != cache_state_->archives.end()) {
      future = cached->second;
    } else {
      producer.emplace();
      future = producer->get_future().share();
      cache_state_->archives.emplace(key, future);
    }
  }

  if (producer.has_value()) {
    try {
      producer->set_value(OpenMpqArchive(mount));
    } catch (...) {
      producer->set_exception(std::current_exception());
    }
  }

  try {
    return future.get();
  } catch (...) {
    return nullptr;
  }
}

std::optional<std::filesystem::path> VirtualFileSystem::ResolveFilesystemPathCaseInsensitive(
    const MountPoint& mount,
    const std::string& normalized_virtual_path) const {
  if (mount.source_root.empty() || normalized_virtual_path.empty()) {
    return std::nullopt;
  }

  std::string rel = normalized_virtual_path;
  if (!rel.empty() && rel.front() == '/') {
    rel.erase(rel.begin());
  }
  if (rel.empty()) {
    return mount.source_root;
  }

  const std::string key =
      mount.source_root.lexically_normal().string() + "|" + rel;
  {
    std::lock_guard lock(cache_state_->filesystem_mutex);
    if (const auto it = cache_state_->filesystem_paths.find(key);
        it != cache_state_->filesystem_paths.end()) {
      return it->second;
    }
  }

  std::filesystem::path current = mount.source_root;
  std::size_t start = 0;
  std::error_code ec;

  while (start < rel.size()) {
    const std::size_t end = rel.find('/', start);
    const std::string component =
        (end == std::string::npos) ? rel.substr(start) : rel.substr(start, end - start);
    if (component.empty() || component == ".") {
      start = (end == std::string::npos) ? rel.size() : (end + 1);
      continue;
    }
    if (component == "..") {
      return std::nullopt;
    }

    if (!std::filesystem::is_directory(current, ec)) {
      return std::nullopt;
    }

    std::optional<std::filesystem::path> matched;
    for (const auto& entry : std::filesystem::directory_iterator(current, ec)) {
      if (ec) break;
      const auto name = entry.path().filename().string();
      if (ToLowerAscii(name) == component) {
        matched = entry.path();
        break;
      }
    }
    if (!matched.has_value()) {
      return std::nullopt;
    }
    current = *matched;
    start = (end == std::string::npos) ? rel.size() : (end + 1);
  }

  {
    std::lock_guard lock(cache_state_->filesystem_mutex);
    cache_state_->filesystem_paths.insert_or_assign(key, current);
  }
  return current;
}

}
