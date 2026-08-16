#include "openwow/core/legacy_buffered_log_file.h"

#include "openwow/core/storm_error.h"
#include "openwow/core/storm_string.h"
#include "openwow/core/storm_path.h"
#include "openwow/core/storm_utils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openwow::core {
namespace {

std::uint64_t GetSteadyTickMilliseconds() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
}

LegacyBufferedLogTimestampSample DefaultTimestampSample() {
  return {
      .tick_milliseconds = GetSteadyTickMilliseconds(),
      .wall_clock_time = std::chrono::system_clock::now(),
  };
}

struct SharedTimestampPrefixCache {
  std::mutex mutex;
  LegacyBufferedLogFile::TimestampSampleProvider provider = DefaultTimestampSample;
  std::uint64_t cached_tick = ~std::uint64_t{0};
  std::string cached_prefix;
};

SharedTimestampPrefixCache& GetSharedTimestampPrefixCache() {
  static SharedTimestampPrefixCache cache;
  return cache;
}

struct SharedOpenPathState {
  std::mutex mutex;
  std::string path_prefix;
};

SharedOpenPathState& GetSharedOpenPathState() {
  static SharedOpenPathState state;
  return state;
}

std::string FormatTimestampPrefix(
    const std::chrono::system_clock::time_point wall_clock_time) {
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(wall_clock_time.time_since_epoch())
      % 1000;
  const std::time_t now_time = std::chrono::system_clock::to_time_t(wall_clock_time);

  std::tm local_time{};
#if defined(_WIN32)
  localtime_s(&local_time, &now_time);
#else
  localtime_r(&now_time, &local_time);
#endif

  std::ostringstream formatted;
  formatted << (local_time.tm_mon + 1) << "/" << local_time.tm_mday << " "
            << std::setw(2) << std::setfill('0') << local_time.tm_hour << ":"
            << std::setw(2) << std::setfill('0') << local_time.tm_min << ":"
            << std::setw(2) << std::setfill('0') << local_time.tm_sec << "."
            << std::setw(3) << std::setfill('0') << milliseconds.count() << "  ";
  return formatted.str();
}

constexpr std::size_t kLegacyBufferedLogPathCapacity = 260u;

bool ShouldReturnBufferedLogDisplayPathUnchanged(
    const std::string_view display_path) {
  if (display_path.empty()) {
    return true;
  }
  if (display_path.size() > 1u && display_path[1] == ':') {
    return true;
  }
  return display_path.find('\\') != std::string_view::npos;
}

std::string ResolveBufferedLogOpenDisplayPath(const std::string_view display_path) {
  if (ShouldReturnBufferedLogDisplayPathUnchanged(display_path)) {
    return std::string(display_path);
  }

  const std::string display_path_string(display_path);
  const auto copy_prefix_and_path =
      [&display_path_string](const std::string_view path_prefix) {
        const std::string path_prefix_string(path_prefix);
        std::array<char, kLegacyBufferedLogPathCapacity> bounded_path{};
        const std::size_t prefix_length =
            SStrCopy(bounded_path.data(), path_prefix_string.c_str(),
                     bounded_path.size());
        SStrCopy(bounded_path.data() + prefix_length, display_path_string.c_str(),
                 bounded_path.size() - prefix_length);
        return std::string(bounded_path.data());
      };

  auto& shared_open_path_state = GetSharedOpenPathState();
  {
    const std::scoped_lock lock(shared_open_path_state.mutex);
    if (!shared_open_path_state.path_prefix.empty()) {
      return copy_prefix_and_path(shared_open_path_state.path_prefix);
    }
  }

  std::array<char, kLegacyBufferedLogPathCapacity> exe_directory{};
  if (!GetExeDirectory(exe_directory.data(),
                       static_cast<std::uint32_t>(exe_directory.size()))
      || exe_directory[0] == '\0') {
    return std::string(display_path);
  }
  std::string exe_directory_path(exe_directory.data());
  if (!exe_directory_path.empty() && exe_directory_path.back() != '\\'
      && exe_directory_path.back() != '/') {
    exe_directory_path.push_back('\\');
  }
  return copy_prefix_and_path(exe_directory_path);
}

struct OpenLogFileRegistry {
  std::mutex mutex;
  std::unordered_set<LegacyBufferedLogFile*> files;
  bool shutdown = false;
};

OpenLogFileRegistry& GetOpenLogFileRegistry() {
  static auto* registry = new OpenLogFileRegistry();
  return *registry;
}

}

LegacyBufferedLogFile::LegacyBufferedLogFile(std::string display_path,
                                             const LegacyBufferedLogOpenMode open_mode) {

  (void)Open(std::move(display_path), open_mode);
}

LegacyBufferedLogFile::~LegacyBufferedLogFile() {
  Close();
}

bool LegacyBufferedLogFile::Open(std::string display_path,
                                 const LegacyBufferedLogOpenMode open_mode) {
  Close();

  {
    auto& registry = GetOpenLogFileRegistry();
    const std::scoped_lock lock(registry.mutex);
    if (registry.shutdown) {
      return false;
    }
    registry.files.insert(this);
  }

  display_path_ = std::move(display_path);
  open_mode_ = open_mode;
  pending_bytes_.clear();
  open_enabled_ = true;

  return EnsureOpen();
}

void LegacyBufferedLogFile::Close() {
  FlushPending();
  if (stream_.is_open()) {
    stream_.close();
  }
  pending_bytes_.clear();

  if (open_enabled_) {
    auto& registry = GetOpenLogFileRegistry();
    const std::scoped_lock lock(registry.mutex);
    registry.files.erase(this);
  }

  open_enabled_ = false;
}

bool LegacyBufferedLogFile::IsOpen() const {
  return stream_.is_open();
}

const std::string& LegacyBufferedLogFile::display_path() const {
  return display_path_;
}

void LegacyBufferedLogFile::SetIndentSpaceCount(const std::size_t indent_space_count) {
  indent_space_count_ = indent_space_count;
}

void LegacyBufferedLogFile::AppendLine(const std::string_view line) {
  if (!EnsureOpen()) {
    return;
  }

  const std::string prefix = TimestampPrefix();
  const std::size_t indent_space_count = std::min(indent_space_count_, kMaxIndentSpaceCount);
  const std::size_t available =
      kBufferCapacity > pending_bytes_.size() ? kBufferCapacity - pending_bytes_.size() : 0u;
  if (available <= prefix.size() + indent_space_count + 2u) {
    FlushPending();
  }

  if (!EnsureOpen()) {
    return;
  }

  const std::size_t remaining =
      kBufferCapacity > pending_bytes_.size() ? kBufferCapacity - pending_bytes_.size() : 0u;
  if (remaining <= prefix.size() + indent_space_count + 2u) {
    return;
  }

  const std::size_t max_payload =
      remaining - prefix.size() - indent_space_count - 2u;
  pending_bytes_.append(prefix);
  pending_bytes_.append(indent_space_count, ' ');
  pending_bytes_.append(line.data(), std::min(line.size(), max_payload));
  pending_bytes_.append("\r\n");

  if (pending_bytes_.size() >= kFlushThreshold) {
    FlushPending();
  }
}

void LegacyBufferedLogFile::FlushPending() {
  if (!stream_.is_open() || pending_bytes_.empty()) {
    return;
  }

  stream_.write(pending_bytes_.data(), static_cast<std::streamsize>(pending_bytes_.size()));
  stream_.flush();
  pending_bytes_.clear();
}

void LegacyBufferedLogFile::FlushAll() {
  auto& registry = GetOpenLogFileRegistry();
  std::vector<LegacyBufferedLogFile*> snapshot;
  {
    const std::scoped_lock lock(registry.mutex);
    snapshot.assign(registry.files.begin(), registry.files.end());
  }
  for (auto* file : snapshot) {
    file->FlushPending();
  }
}

void LegacyBufferedLogFile::ShutdownAll() {
  FlushAll();

  auto& registry = GetOpenLogFileRegistry();
  std::vector<LegacyBufferedLogFile*> snapshot;
  {
    const std::scoped_lock lock(registry.mutex);
    snapshot.assign(registry.files.begin(), registry.files.end());
    registry.files.clear();
    registry.shutdown = true;
  }

  for (auto* file : snapshot) {
    const std::string saved_path = file->display_path_;
    file->FlushPending();
    if (file->stream_.is_open()) {
      file->stream_.close();
    }
    file->pending_bytes_.clear();
    file->open_enabled_ = false;
    CheckHandleRelease("HSLOG", saved_path.c_str());
  }
}

void LegacyBufferedLogFile::ResetShutdownForTesting() {
  auto& registry = GetOpenLogFileRegistry();
  const std::scoped_lock lock(registry.mutex);
  registry.shutdown = false;
}

bool LegacyBufferedLogFile::EnsureOpen() {
  if (stream_.is_open()) {
    return true;
  }
  if (!open_enabled_ || display_path_.empty()) {
    return false;
  }

  const std::string open_display_path =
      ResolveBufferedLogOpenDisplayPath(display_path_);
  CreateDisplayPathDirectoriesBestEffort(open_display_path);
  const auto file_path = DisplayPathToFilesystemPath(open_display_path);
  auto stream_mode = std::ios::binary | std::ios::out;
  stream_mode |= open_mode_ == LegacyBufferedLogOpenMode::kAppend ? std::ios::app
                                                                  : std::ios::trunc;
  stream_.open(file_path, stream_mode);
  return stream_.is_open();
}

void LegacyBufferedLogFile::SetSharedOpenPathPrefixForTesting(
    std::string path_prefix) {
  auto& shared_open_path_state = GetSharedOpenPathState();
  const std::scoped_lock lock(shared_open_path_state.mutex);
  shared_open_path_state.path_prefix = std::move(path_prefix);
}

void LegacyBufferedLogFile::ResetSharedOpenPathPrefixForTesting() {
  SetSharedOpenPathPrefixForTesting({});
}

void LegacyBufferedLogFile::SetTimestampSampleProviderForTesting(
    TimestampSampleProvider provider) {
  auto& cache = GetSharedTimestampPrefixCache();
  const std::scoped_lock lock(cache.mutex);
  cache.provider = provider ? std::move(provider) : TimestampSampleProvider(DefaultTimestampSample);
  cache.cached_tick = ~std::uint64_t{0};
  cache.cached_prefix.clear();
}

void LegacyBufferedLogFile::ResetTimestampSampleProviderForTesting() {
  SetTimestampSampleProviderForTesting({});
}

std::string LegacyBufferedLogFile::TimestampPrefix() {
  auto& cache = GetSharedTimestampPrefixCache();
  const std::scoped_lock lock(cache.mutex);

  const LegacyBufferedLogTimestampSample sample = cache.provider();
  if (sample.tick_milliseconds != cache.cached_tick) {
    cache.cached_tick = sample.tick_milliseconds;
    cache.cached_prefix = FormatTimestampPrefix(sample.wall_clock_time);
  }

  return cache.cached_prefix;
}

std::filesystem::path LegacyBufferedLogFile::DisplayPathToFilesystemPath(
    const std::string_view display_path) {
  std::string normalized(display_path);

  std::replace(normalized.begin(), normalized.end(), '\\',
               static_cast<char>(std::filesystem::path::preferred_separator));
  return std::filesystem::path(normalized);
}

void LegacyBufferedLogFile::CreateDisplayPathDirectoriesBestEffort(
    const std::string_view display_path) {
  std::string directory_path(display_path);
  if (!directory_path.empty()) {
    const auto separator_index = directory_path.find_last_of("\\/");
    if (separator_index != std::string::npos) {
      const auto root_length = static_cast<std::size_t>(
          std::clamp(GetStormLogFileCreateRootPathLength(directory_path.c_str()), 0,
                     static_cast<int>(directory_path.size())));
      if (separator_index + 1u >= root_length) {
        directory_path.resize(separator_index + 1u);
      } else {
        directory_path.resize(root_length);
      }
    }
  }

  if (directory_path.empty()) {
    return;
  }

  const auto root_length = static_cast<std::size_t>(
      std::clamp(GetStormLogFileCreateRootPathLength(directory_path.c_str()), 0,
                 static_cast<int>(directory_path.size())));
  for (std::size_t index = root_length; index < directory_path.size(); ++index) {
    if (directory_path[index] == '/') {
      directory_path[index] = '\\';
    }
  }

  std::error_code ec;
  std::size_t next_separator = directory_path.find('\\', root_length);
  while (next_separator != std::string::npos) {
    const char saved = directory_path[next_separator];
    directory_path[next_separator] = '\0';
    std::filesystem::create_directory(
        DisplayPathToFilesystemPath(std::string_view(directory_path.c_str())), ec);
    directory_path[next_separator] = saved;
    next_separator = directory_path.find('\\', next_separator + 1u);
  }

  std::filesystem::create_directory(DisplayPathToFilesystemPath(directory_path), ec);
}

}
