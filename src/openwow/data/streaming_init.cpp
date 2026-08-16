
#include "openwow/data/streaming_init.h"

#include "openwow/core/client_init.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/runtime/time/game_time.h"
#include "openwow/core/storm_path.h"
#include "openwow/core/storm_utils.h"
#include "openwow/core/streaming_storage.h"
#include "openwow/net/os_url_download.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/adapters/filesystem/native_filesystem.h"
#include "openwow/vfs/retail/retail_path_resolver.h"
#include "openwow/vfs/retail/sfile_runtime.h"
#include "openwow/vfs/retail/io_unit/io_unit_compat.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace openwow::data {

namespace {

constexpr std::string_view kIndirectManifestPrefix = "http://";
constexpr std::string_view kFileSourceManifestPrefix = "file://";
constexpr std::string_view kManifestParameterBgPreloadSleep = "bgpreloadsleep";
constexpr std::string_view kManifestParameterIsTrial = "istrial";
constexpr int kManifestStatusContext = 20;
constexpr int kIndirectManifestRetryCount = 5;
constexpr std::uint32_t kIndirectManifestTimeoutMs = 3000;
constexpr std::size_t kManifestCanonicalPathCapacity = 1024;
constexpr std::size_t kFileManifestLookupPathCapacity = 260;
constexpr std::array<const char*, 5> kManifestArchiveNameTable = {
    "Installer Tome.MPQ",
    "Movies.MPQ",
    "expansion.MPQ",
    "expansionspeech.MPQ",
    "expansionloc.MPQ",
};
constexpr std::array<std::uint32_t, 24> kFrameDeltaThresholds = {
    0u,   1u,   8u,   11u,  14u,  18u,  24u,  32u,
    42u,  56u,  74u,  97u,  128u, 170u, 224u, 295u,
    390u, 515u, 680u, 897u, 1184u, 1563u, 2063u, 0xFFFFFFFFu,
};

bool AsciiStartsWithIgnoreCase(std::string_view value, std::string_view prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }
  return openwow::text::EqualsIgnoreCaseAscii(value.substr(0, prefix.size()),
                                              prefix);
}

std::string NormalizeManifestPath(std::string_view value,
                                  const bool lowercase_ascii,
                                  const std::size_t capacity) {
  std::string normalized;
  normalized.reserve(std::min(value.size(), capacity - 1u));
  for (const unsigned char ch : value) {
    if (normalized.size() >= capacity - 1u) {
      break;
    }

    char out = static_cast<char>(ch);
    if (out == '/') {
      out = '\\';
    }
    if (lowercase_ascii) {
      out = static_cast<char>(std::tolower(static_cast<unsigned char>(out)));
    }
    normalized.push_back(out);
  }
  return normalized;
}

std::string NormalizeManifestLookupPath(std::string_view value,
                                        const bool lowercase_ascii) {
  return NormalizeManifestPath(value, lowercase_ascii,
                               kFileManifestLookupPathCapacity);
}

std::string NormalizeCanonicalManifestPath(std::string_view value,
                                           const bool lowercase_ascii) {
  return NormalizeManifestPath(value, lowercase_ascii,
                               kManifestCanonicalPathCapacity);
}

std::string BuildManifestAbsolutePath(std::string_view raw_path) {
  if (raw_path.empty()) {
    return {};
  }

  std::array<char, kManifestCanonicalPathCapacity> absolute_path = {};
  const std::string source(raw_path);
  if (!openwow::vfs::FileSystem_MakeAbsolutePath(
          source.c_str(), absolute_path.data(), static_cast<int>(absolute_path.size()))) {
    return {};
  }

  return NormalizeCanonicalManifestPath(absolute_path.data(), false);
}

std::string BuildManifestFileLookupPath(std::string_view raw_path) {
  const std::string source(raw_path);
  const auto lookup_path =
      openwow::vfs::CanonicalizeStreamingManifestLookupPath(source.c_str());
  return lookup_path.value_or(std::string{});
}

std::string ResolveLegacyManifestAbsolutePath(std::string_view value) {
  return BuildManifestAbsolutePath(value);
}

std::int64_t ParseManifestInt64(std::string_view value) {
  const std::string storage(value);
  return std::strtoll(storage.c_str(), nullptr, 10);
}

std::uint32_t ParseManifestUint32(std::string_view value) {
  const std::string storage(value);
  return static_cast<std::uint32_t>(std::strtoul(storage.c_str(), nullptr, 10));
}

struct SemicolonFieldScan {
  std::array<std::string_view, 4> committed_fields{};
  std::size_t delimiter_run_count{0};
  std::string_view trailing_field;
};

SemicolonFieldScan ScanSemicolonFields(std::string_view line) {
  SemicolonFieldScan result;
  std::size_t token_start = 0;
  std::size_t cursor = 0;
  while (cursor < line.size()) {
    const std::size_t separator = line.find(';', cursor);
    if (separator == std::string_view::npos) {
      break;
    }

    if (result.delimiter_run_count < result.committed_fields.size()) {
      result.committed_fields[result.delimiter_run_count] =
          line.substr(token_start, separator - token_start);
    }
    ++result.delimiter_run_count;

    std::size_t separator_end = separator;
    while (separator_end < line.size() && line[separator_end] == ';') {
      ++separator_end;
    }

    token_start = separator_end;
    cursor = separator_end;
  }

  result.trailing_field = line.substr(token_start);
  return result;
}

void ApplyLegacyManifestFileField(const std::size_t field_index,
                                  std::string_view token,
                                  StreamingLegacyFileEntry* entry) {
  if (!entry) {
    return;
  }

  switch (field_index) {
    case 0:
      entry->raw_path.assign(token);
      entry->absolute_source_path = ResolveLegacyManifestAbsolutePath(token);
      entry->lookup_path =
          openwow::vfs::CanonicalizeStreamingManifestLookupPath(
              entry->raw_path.c_str())
              .value_or(std::string{});
      break;
    case 1:
      entry->size = ParseManifestInt64(token);
      break;
    case 2:
      entry->file_version.assign(token);
      break;
    case 3:
      entry->flags = ParseManifestUint32(token);
      break;
    default:
      break;
  }
}

bool ParseLegacyManifestFileEntry(std::string_view line,
                                  StreamingLegacyFileEntry* entry) {
  if (!entry || line.empty()) {
    return false;
  }

  *entry = {};
  const SemicolonFieldScan fields = ScanSemicolonFields(line);
  const std::size_t committed_count =
      std::min(fields.delimiter_run_count, fields.committed_fields.size());
  for (std::size_t field_index = 0; field_index < committed_count;
       ++field_index) {
    ApplyLegacyManifestFileField(
        field_index, fields.committed_fields[field_index], entry);
  }

  if (fields.delimiter_run_count == 3) {
    ApplyLegacyManifestFileField(3, fields.trailing_field, entry);
  }

  return true;
}

std::int32_t ParseTransportManifestInteger(std::string_view value) {
  return static_cast<std::int32_t>(
      std::strtol(std::string(value).c_str(), nullptr, 10));
}

void ApplyTransportManifestSectionField(
    const std::size_t field_index,
    std::string_view field,
    std::string* entry_name,
    StreamingManifestTransportSection* transport_section) {
  if (!entry_name || !transport_section) {
    return;
  }

  switch (field_index) {
    case 0:
      entry_name->assign(field);
      break;
    case 1:
      transport_section->md5_size = ParseTransportManifestInteger(field);
      break;
    case 2:
      transport_section->split_size = ParseTransportManifestInteger(field);
      break;
    default:
      break;
  }
}

bool ParseTransportManifestEntry(std::string_view line,
                                 std::string* entry_name,
                                 StreamingManifestTransportSection* transport_section) {
  if (!entry_name || !transport_section) {
    return false;
  }

  entry_name->clear();
  *transport_section = {};
  if (line.empty()) {
    return false;
  }

  std::size_t field_index = 0;
  std::size_t token_start = 0;
  for (std::size_t cursor = 0; cursor < line.size(); ++cursor) {
    if (line[cursor] != ';') {
      continue;
    }

    ApplyTransportManifestSectionField(field_index,
                                       line.substr(token_start, cursor - token_start),
                                       entry_name,
                                       transport_section);
    while (cursor < line.size() && line[cursor] == ';') {
      ++cursor;
    }

    ++field_index;
    token_start = cursor;
    if (cursor >= line.size()) {
      break;
    }
    --cursor;
  }

  return true;
}

struct StreamingSourceManifestEntry {
  std::string alias;
  std::string locator;
};

struct StreamingSourceLocator {
  std::string root_uri;
  std::string relative_path;
};

struct FileManifestRuntimeEntry {
  std::string normalized_lookup_key;
  openwow::core::FileManifestEntry entry;
  std::optional<StreamingManifestTransportSection> resolved_transport_section;
  std::int32_t prefix_gate_count{0};
  std::int32_t prefix_status_code{0};
  bool prefix_active{false};
};

void EnsureStreamingManifestRuntimeInitialized();

std::vector<FileManifestRuntimeEntry>& FileManifestRuntimeEntriesStorage() {
  static std::vector<FileManifestRuntimeEntry> entries;
  return entries;
}

std::vector<FileManifestRuntimeEntry>& FileManifestRuntimeEntries() {
  EnsureStreamingManifestRuntimeInitialized();
  return FileManifestRuntimeEntriesStorage();
}

std::string NormalizeFileManifestRuntimeLookupKey(
    const openwow::core::FileManifestEntry& entry) {
  return NormalizeManifestLookupPath(
      openwow::core::StreamingStorage::BuildManifestEntryRelativeFilePath(entry),
      true);
}

std::string NormalizeFileManifestLookupKey(std::string_view lookup_key) {
  return NormalizeManifestLookupPath(lookup_key, true);
}

std::string NormalizeFileManifestDirectoryQuery(std::string_view path) {
  std::string raw_path(path);
  std::array<char, 260> bounded_path = {};
  std::array<char, 260> normalized_path = {};

  openwow::core::CopyStormPath(
      bounded_path.data(), raw_path.c_str(), static_cast<int>(bounded_path.size()));
  const char separator =
      openwow::core::ChooseStormPathSeparator(bounded_path.data());
  openwow::core::EnsureTrailingStormPathSeparator(
      bounded_path.data(), static_cast<int>(bounded_path.size()), separator);
  openwow::core::NormalizePathToBackslashes(
      bounded_path.data(),
      normalized_path.data(),
      static_cast<int>(normalized_path.size()));
  return normalized_path.data();
}

struct FileManifestDirectoryMatch {
  std::string directory_prefix;
  std::string filename;
};

FileManifestDirectoryMatch SplitFileManifestDirectoryMatch(
    const openwow::core::FileManifestEntry& entry) {
  const std::string relative_path =
      openwow::core::StreamingStorage::BuildManifestEntryRelativeFilePath(entry);
  const std::size_t separator = relative_path.find_last_of("/\\");

  std::array<char, 260> bounded_directory = {};
  std::array<char, 260> bounded_filename = {};
  if (separator == std::string::npos) {
    openwow::core::CopyStormPath(bounded_filename.data(),
                                 relative_path.c_str(),
                                 static_cast<int>(bounded_filename.size()));
    return {.directory_prefix = {}, .filename = bounded_filename.data()};
  }

  const std::string directory_prefix = relative_path.substr(0, separator + 1);
  const std::string filename = relative_path.substr(separator + 1);
  openwow::core::CopyStormPath(bounded_directory.data(),
                               directory_prefix.c_str(),
                               static_cast<int>(bounded_directory.size()));
  openwow::core::CopyStormPath(bounded_filename.data(),
                               filename.c_str(),
                               static_cast<int>(bounded_filename.size()));
  return {
      .directory_prefix = bounded_directory.data(),
      .filename = bounded_filename.data(),
  };
}

bool AsciiPrefixEqualsIgnoreCase(std::string_view value,
                                 std::string_view prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }

  for (std::size_t index = 0; index < prefix.size(); ++index) {
    const auto lhs = static_cast<unsigned char>(value[index]);
    const auto rhs = static_cast<unsigned char>(prefix[index]);
    if (std::tolower(lhs) != std::tolower(rhs)) {
      return false;
    }
  }

  return true;
}

template <typename EntryContainer>
auto LowerBoundFileManifestRuntimeEntry(EntryContainer& entries,
                                        const std::string_view lookup_key) {
  return std::lower_bound(
      entries.begin(),
      entries.end(),
      lookup_key,
      [](const FileManifestRuntimeEntry& lhs, const std::string_view rhs) {
        return lhs.normalized_lookup_key < rhs;
      });
}

FileManifestRuntimeEntry BuildFileManifestRuntimeEntry(
    std::string normalized_lookup_key,
    openwow::core::FileManifestEntry entry,
    std::optional<StreamingManifestTransportSection> resolved_transport_section,
    const std::int32_t prefix_gate_count,
    const std::int32_t prefix_status_code,
    const bool prefix_active) {
  FileManifestRuntimeEntry runtime_entry;
  runtime_entry.normalized_lookup_key = std::move(normalized_lookup_key);
  runtime_entry.entry = std::move(entry);
  runtime_entry.resolved_transport_section =
      std::move(resolved_transport_section);
  runtime_entry.prefix_gate_count = prefix_gate_count;
  runtime_entry.prefix_status_code = prefix_status_code;
  runtime_entry.prefix_active = prefix_active;
  return runtime_entry;
}

std::vector<FileManifestRuntimeEntry>::iterator FindFileManifestEntryByLookupKey(
    const std::string_view lookup_key) {
  auto& entries = FileManifestRuntimeEntries();
  const auto normalized_lookup_key = NormalizeFileManifestLookupKey(lookup_key);
  const auto it = LowerBoundFileManifestRuntimeEntry(entries,
                                                     normalized_lookup_key);
  if (it == entries.end() || it->normalized_lookup_key != normalized_lookup_key) {
    return entries.end();
  }
  return it;
}

std::vector<FileManifestRuntimeEntry>::const_iterator
FindMatchingFileManifestPrefixEntryConst(const std::string_view path) {
  const auto& entries = FileManifestRuntimeEntries();
  return std::find_if(entries.begin(),
                      entries.end(),
                      [path](const FileManifestRuntimeEntry& entry) {
                        return AsciiPrefixEqualsIgnoreCase(path,
                                                           entry.entry.path);
                      });
}

std::vector<FileManifestRuntimeEntry>::const_iterator
FindFileManifestEntryByLookupKeyConst(const std::string_view lookup_key) {
  const auto& entries = FileManifestRuntimeEntries();
  const auto normalized_lookup_key = NormalizeFileManifestLookupKey(lookup_key);
  const auto it = LowerBoundFileManifestRuntimeEntry(entries,
                                                     normalized_lookup_key);
  if (it == entries.end() || it->normalized_lookup_key != normalized_lookup_key) {
    return entries.end();
  }
  return it;
}

bool InsertFileManifestRuntimeEntryWithLookupKey(
    std::string normalized_lookup_key,
    openwow::core::FileManifestEntry entry,
    std::optional<StreamingManifestTransportSection> resolved_transport_section,
    const std::int32_t prefix_gate_count,
    const std::int32_t prefix_status_code,
    const bool prefix_active) {
  auto& entries = FileManifestRuntimeEntries();
  FileManifestRuntimeEntry runtime_entry = BuildFileManifestRuntimeEntry(
      std::move(normalized_lookup_key),
      std::move(entry),
      std::move(resolved_transport_section),
      prefix_gate_count,
      prefix_status_code,
      prefix_active);

  const auto it = LowerBoundFileManifestRuntimeEntry(
      entries, runtime_entry.normalized_lookup_key);
  if (it != entries.end()
      && it->normalized_lookup_key == runtime_entry.normalized_lookup_key) {
    return false;
  }

  entries.insert(it, std::move(runtime_entry));
  return true;
}

std::optional<StreamingSourceLocator> SplitStreamingSourceLocator(
    std::string_view locator) {
  if (locator.empty()) {
    return std::nullopt;
  }

  std::size_t prefix_length = 0;
  if (AsciiStartsWithIgnoreCase(locator, kIndirectManifestPrefix)) {
    prefix_length = kIndirectManifestPrefix.size();
  } else if (AsciiStartsWithIgnoreCase(locator, kFileSourceManifestPrefix)) {
    prefix_length = kFileSourceManifestPrefix.size();
  } else {
    return std::nullopt;
  }

  const std::size_t slash_pos = locator.find('/', prefix_length);
  if (slash_pos == std::string_view::npos) {
    return std::nullopt;
  }

  std::size_t relative_start = slash_pos;
  while (relative_start < locator.size() && locator[relative_start] == '/') {
    ++relative_start;
  }

  StreamingSourceLocator split;
  split.root_uri.assign(locator.substr(0, slash_pos));
  split.root_uri.push_back('/');
  split.relative_path.assign(locator.substr(relative_start));
  return split;
}

bool ParseSourceManifestEntryLine(std::string_view line,
                                  StreamingSourceManifestEntry* entry) {
  if (!entry || line.empty()) {
    return false;
  }

  *entry = {};
  const SemicolonFieldScan fields = ScanSemicolonFields(line);
  if (fields.delimiter_run_count == 0) {
    return true;
  }

  entry->alias.assign(fields.committed_fields[0]);
  entry->locator.assign(fields.delimiter_run_count == 1
                            ? fields.trailing_field
                            : fields.committed_fields[1]);

  return true;
}

std::int64_t ParseManifestTimeslotTime(std::string_view value) {
  int hour = 0;
  int minute = 0;
  const std::string storage(value);
  std::sscanf(storage.c_str(), "%d:%d", &hour, &minute);

  openwow::core::ida::CalendarTimeFields fields;
  fields.year = 2000;
  fields.month = 1;
  fields.day = 1;
  fields.hour = hour;
  fields.minute = minute;
  return openwow::core::ida::CalendarTimeNsSince2000FromFields(fields);
}

bool TryApplyManifestTimeslotField(std::string_view key,
                                   std::string_view value,
                                   StreamingManifestTimeslot* timeslot) {
  if (!timeslot) {
    return false;
  }

  if (openwow::text::EqualsIgnoreCaseAscii(key, "starttime")) {
    timeslot->start_time_ns_since_2000 = ParseManifestTimeslotTime(value);
    return true;
  }

  if (openwow::text::EqualsIgnoreCaseAscii(key, "endtime")) {
    timeslot->end_time_ns_since_2000 = ParseManifestTimeslotTime(value);
    return true;
  }

  if (openwow::text::EqualsIgnoreCaseAscii(key, "usage")) {
    timeslot->usage =
        static_cast<std::int32_t>(std::strtol(std::string(value).c_str(),
                                              nullptr, 10));
    return true;
  }

  return false;
}

void ApplyManifestServerScheduleRange(
    std::vector<StreamingManifestServerScheduleEntry>* schedule,
    const StreamingManifestServerScheduleEntry& block,
    const std::int64_t trailing_start_time_ns_since_2000) {
  if (!schedule) {
    return;
  }

  using ScheduleEntries = std::vector<StreamingManifestServerScheduleEntry>;
  using ScheduleDiff = ScheduleEntries::difference_type;

  std::optional<std::size_t> trailing_boundary_index;
  std::size_t index = 0;
  while (index < schedule->size()) {
    if (trailing_boundary_index.has_value()) {
      const auto trailing_start =
          (*schedule)[*trailing_boundary_index].start_time_ns_since_2000;
      if ((*schedule)[index].start_time_ns_since_2000 > trailing_start) {
        break;
      }

      if (index == *trailing_boundary_index) {
        ++index;
        continue;
      }

      (*schedule)[*trailing_boundary_index].usage = (*schedule)[index].usage;
      schedule->erase(schedule->begin() + static_cast<ScheduleDiff>(index));
      continue;
    }

    if (block.start_time_ns_since_2000
        > (*schedule)[index].start_time_ns_since_2000) {
      ++index;
      continue;
    }

    schedule->insert(schedule->begin() + static_cast<ScheduleDiff>(index), block);
    trailing_boundary_index = index + 1;
    (*schedule)[*trailing_boundary_index].start_time_ns_since_2000 =
        trailing_start_time_ns_since_2000;
    index = *trailing_boundary_index;
  }

  if (trailing_boundary_index.has_value()) {
    return;
  }

  schedule->push_back(block);
  if (trailing_start_time_ns_since_2000 != 0) {
    schedule->push_back(
        {.start_time_ns_since_2000 = trailing_start_time_ns_since_2000,
         .usage = 100});
  }
}

void ApplyManifestServerTimeslot(StreamingManifestServer* server,
                                 const StreamingManifestTimeslot& timeslot) {
  if (!server) {
    return;
  }

  constexpr std::int64_t kManifestDayStartTimeNsSince2000 = 0;

  StreamingManifestServerScheduleEntry block{
      .start_time_ns_since_2000 = timeslot.start_time_ns_since_2000,
      .usage = timeslot.usage,
  };
  if (timeslot.start_time_ns_since_2000 == timeslot.end_time_ns_since_2000) {
    server->schedule.clear();
    block.start_time_ns_since_2000 = kManifestDayStartTimeNsSince2000;
    ApplyManifestServerScheduleRange(&server->schedule, block, 0);
    return;
  }

  if (timeslot.start_time_ns_since_2000 < timeslot.end_time_ns_since_2000) {
    ApplyManifestServerScheduleRange(
        &server->schedule, block, timeslot.end_time_ns_since_2000);
    return;
  }

  block.start_time_ns_since_2000 = kManifestDayStartTimeNsSince2000;
  ApplyManifestServerScheduleRange(
      &server->schedule, block, timeslot.end_time_ns_since_2000);

  block.start_time_ns_since_2000 = timeslot.start_time_ns_since_2000;
  ApplyManifestServerScheduleRange(&server->schedule, block, 0);
}

extern StreamingManifestState g_streaming_manifest_state;

bool ResolveManifestServerTimeslotSchedules() {
  bool all_timeslots_resolved = true;
  for (auto& [server_name, server] : g_streaming_manifest_state.servers) {
    (void)server_name;
    for (const auto& timeslot_id : server.timeslot_ids) {
      const auto timeslot_it = g_streaming_manifest_state.timeslots.find(timeslot_id);
      if (timeslot_it == g_streaming_manifest_state.timeslots.end()) {
        all_timeslots_resolved = false;
        continue;
      }

      ApplyManifestServerTimeslot(&server, timeslot_it->second);
    }
  }
  return all_timeslots_resolved;
}

bool ResolveManifestFileEntries() {
  bool all_entries_resolved = true;
  for (auto& [lookup_key, file_entry] : g_streaming_manifest_state.file_entries) {
    file_entry.resolved_transport_section.reset();

    if (file_entry.entry.size == 0) {
      (void)openwow::vfs::FileSystem_CreateDirectory(lookup_key.c_str(), false);
      continue;
    }

    if (!file_entry.entry.path.empty()) {
      const auto path_alias_it =
          g_streaming_manifest_state.path_aliases.find(file_entry.entry.path);
      if (path_alias_it == g_streaming_manifest_state.path_aliases.end()) {
        all_entries_resolved = false;
      } else {
        file_entry.entry.path = path_alias_it->second;
      }
    }

    if (file_entry.entry.transportItem.empty()) {
      continue;
    }

    const auto transport_it = g_streaming_manifest_state.transport_sections.find(
        file_entry.entry.transportItem);
    if (transport_it == g_streaming_manifest_state.transport_sections.end()) {
      if (g_streaming_manifest_state.version > 1) {
        all_entries_resolved = false;
      }
      continue;
    }

    file_entry.resolved_transport_section = transport_it->second;
  }

  return all_entries_resolved;
}

void SyncFileManifestRuntimeEntriesFromState() {
  auto& entries = FileManifestRuntimeEntriesStorage();
  entries.clear();
  entries.reserve(g_streaming_manifest_state.file_entries.size());

  for (const auto& [lookup_key, file_entry] : g_streaming_manifest_state.file_entries) {
    InsertFileManifestRuntimeEntryWithLookupKey(
        lookup_key,
        file_entry.entry,
        file_entry.resolved_transport_section,
        0,
        0,
        false);
  }
}

bool TryApplyManifestTransportSectionField(
    std::string_view key,
    std::string_view value,
    StreamingManifestTransportSection* transport_section) {
  if (!transport_section) {
    return false;
  }

  if (openwow::text::EqualsIgnoreCaseAscii(key, "md5size")) {
    transport_section->md5_size = ParseTransportManifestInteger(value);
    return true;
  }

  if (openwow::text::EqualsIgnoreCaseAscii(key, "splitsize")) {
    transport_section->split_size = ParseTransportManifestInteger(value);
    return true;
  }

  return false;
}

bool TryApplyManifestServerField(std::string_view key,
                                 std::string_view value,
                                 StreamingManifestServer* server) {
  if (!server) {
    return false;
  }

  openwow::core::StreamingStorageConfig config;
  config.location = server->location;
  config.timeSlotIds = server->timeslot_ids;
  config.maxRetry = server->max_retry;
  config.canDeactivate = server->can_deactivate;

  const std::string key_storage(key);
  const std::string value_storage(value);
  const bool parsed = openwow::core::StreamingStorage::Instance().ParseAttribute(
      config, key_storage.c_str(), value_storage.c_str());
  if (!parsed) {
    return false;
  }

  server->location = std::move(config.location);
  server->timeslot_ids = std::move(config.timeSlotIds);
  server->max_retry = config.maxRetry;
  server->can_deactivate = config.canDeactivate;
  return true;
}

bool TryApplyManifestFileField(std::string_view key,
                               std::string_view value,
                               StreamingManifestFileEntry* file_entry) {
  if (!file_entry) {
    return false;
  }

  const std::string key_storage(key);
  const std::string value_storage(value);
  return openwow::core::StreamingStorage::Instance().SetManifestEntryAttribute(
      file_entry->entry, key_storage.c_str(), value_storage.c_str());
}

extern StreamingManifestState g_streaming_manifest_state;

const std::string* FindManifestParameterValue(std::string_view name) {

  const auto& parameters = g_streaming_manifest_state.parameters;
  const auto it = parameters.find(std::string(name));
  if (it == parameters.end()) {
    return nullptr;
  }

  return &it->second;
}

}

void SetManifestParameterIntValue(std::string_view name, int value) {
  g_streaming_manifest_state.parameters[std::string(name)] =
      std::to_string(value);
}

namespace {

bool ReadManifestTrialModeFlag() {
  const std::string* value =
      FindManifestParameterValue(kManifestParameterIsTrial);
  if (!value || value->empty()) {
    return false;
  }

  return std::strtol(value->c_str(), nullptr, 10) != 0;
}

int ReadManifestBgPreloadSleep() {
  const std::string* value =
      FindManifestParameterValue(kManifestParameterBgPreloadSleep);
  if (!value || value->empty()) {
    return 100;
  }

  return std::max(0,
                  static_cast<int>(std::strtol(value->c_str(), nullptr, 10)));
}

void ApplyManifestParameter(std::string_view name, std::string_view value) {

  auto& parameters = g_streaming_manifest_state.parameters;
  parameters[std::string(name)] = std::string(value);

  if (name == kManifestParameterIsTrial) {
    g_streaming_manifest_state.trial_mode = ReadManifestTrialModeFlag();
    return;
  }

  if (name == kManifestParameterBgPreloadSleep) {
    openwow::core::StreamingStorage::Instance().SetBgPreloadSleepFromManifestParameter(
        ReadManifestBgPreloadSleep());
  }
}

void RegisterManifestSourceLocator(std::string_view alias,
                                   std::string_view locator) {
  const auto split = SplitStreamingSourceLocator(locator);
  if (!split.has_value()) {
    return;
  }

  auto& source_server = g_streaming_manifest_state.servers[split->root_uri];
  source_server = {};
  source_server.location = split->root_uri;
  g_streaming_manifest_state.source_root_uris.insert(split->root_uri);
  g_streaming_manifest_state.path_aliases[std::string(alias)] =
      split->relative_path;
}

enum class ManifestInfoActiveSectionKind {
  kNone,
  kFile,
  kParameter,
  kServer,
  kServerPath,
  kTransport,
  kTimeslot,
};

struct ManifestInfoActiveSection {
  ManifestInfoActiveSectionKind kind{ManifestInfoActiveSectionKind::kNone};
  std::string_view parameter_name;
  StreamingManifestFileEntry* file_entry{nullptr};
  StreamingManifestServer* server{nullptr};
  std::string* server_path{nullptr};
  StreamingManifestTransportSection* transport_section{nullptr};
  StreamingManifestTimeslot* timeslot{nullptr};
};

bool TryApplyManifestInfoActiveField(const ManifestInfoActiveSection& section,
                                     std::string_view key,
                                     std::string_view value) {
  switch (section.kind) {
    case ManifestInfoActiveSectionKind::kFile:
      return TryApplyManifestFileField(key, value, section.file_entry);
    case ManifestInfoActiveSectionKind::kParameter:
      if (!openwow::text::EqualsIgnoreCaseAscii(key, "value")) {
        return false;
      }
      ApplyManifestParameter(section.parameter_name, value);
      return true;
    case ManifestInfoActiveSectionKind::kServer:
      return TryApplyManifestServerField(key, value, section.server);
    case ManifestInfoActiveSectionKind::kServerPath:
      if (!section.server_path
          || !openwow::text::EqualsIgnoreCaseAscii(key, "path")) {
        return false;
      }
      section.server_path->assign(value);
      return true;
    case ManifestInfoActiveSectionKind::kTransport:
      return TryApplyManifestTransportSectionField(
          key, value, section.transport_section);
    case ManifestInfoActiveSectionKind::kTimeslot:
      return TryApplyManifestTimeslotField(key, value, section.timeslot);
    case ManifestInfoActiveSectionKind::kNone:
      return false;
  }

  return false;
}

struct ManifestLine {
  std::string_view key;
  std::string_view value;
  bool has_key{false};
};

enum class ManifestEmbeddedNulPolicy {
  kStopAfterLine,
  kContinueAfterLine,
};

bool IsManifestTrimChar(const char ch) {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

std::string_view TrimLeadingManifestToken(std::string_view token) {
  std::size_t begin = 0;
  while (begin < token.size() && IsManifestTrimChar(token[begin])) {
    ++begin;
  }
  return token.substr(begin);
}

bool NextManifestLine(std::string_view buffer,
                      std::size_t* offset,
                      ManifestLine* line,
                      const ManifestEmbeddedNulPolicy embedded_nul_policy) {
  if (!offset || !line || *offset >= buffer.size()
      || buffer[*offset] == '\0') {
    return false;
  }

  const std::size_t start = *offset;
  std::size_t cursor = start;
  std::size_t first_equal_pos = std::string_view::npos;
  std::size_t last_equal_pos = std::string_view::npos;
  bool hit_nul = false;
  while (cursor < buffer.size()) {
    const char ch = buffer[cursor];
    if (ch == '\0') {
      hit_nul = true;
      break;
    }
    if (ch == '\r' || ch == '\n') {
      break;
    }
    if (ch == '=') {
      if (first_equal_pos == std::string_view::npos) {
        first_equal_pos = cursor;
      }
      last_equal_pos = cursor;
    }
    ++cursor;
  }

  std::size_t next = cursor;
  if (hit_nul) {
    if (embedded_nul_policy
        == ManifestEmbeddedNulPolicy::kContinueAfterLine) {
      ++next;
    }
  } else {
    while (next < buffer.size()
           && (buffer[next] == '\r' || buffer[next] == '\n')) {
      ++next;
    }
  }
  *offset = next;

  line->has_key = first_equal_pos != std::string_view::npos;
  if (line->has_key) {
    line->key = buffer.substr(start, first_equal_pos - start);
    line->value = buffer.substr(last_equal_pos + 1, cursor - last_equal_pos - 1);
  } else {
    line->key = {};
    line->value = buffer.substr(start, cursor - start);
  }
  return true;
}

bool NextManifestInfoLine(std::string_view buffer,
                          std::size_t* offset,
                          ManifestLine* line) {
  if (!offset || !line || *offset >= buffer.size()
      || buffer[*offset] == '\0') {
    return false;
  }

  const std::size_t start = *offset;
  std::size_t cursor = start;
  std::size_t first_equal_pos = std::string_view::npos;
  std::size_t last_equal_pos = std::string_view::npos;
  while (cursor < buffer.size()) {
    const char ch = buffer[cursor];
    if (ch == '\0') {
      return false;
    }
    if (ch == '\r' || ch == '\n') {
      break;
    }
    if (ch == '=') {
      if (first_equal_pos == std::string_view::npos) {
        first_equal_pos = cursor;
      }
      last_equal_pos = cursor;
    }
    ++cursor;
  }

  if (cursor == buffer.size()) {
    return false;
  }

  std::size_t next = cursor;
  while (next < buffer.size()
         && (buffer[next] == '\r' || buffer[next] == '\n')) {
    ++next;
  }
  *offset = next;

  line->has_key = first_equal_pos != std::string_view::npos;
  if (line->has_key) {
    line->key = TrimLeadingManifestToken(
        buffer.substr(start, first_equal_pos - start));
    line->value = TrimLeadingManifestToken(
        buffer.substr(last_equal_pos + 1, cursor - last_equal_pos - 1));
  } else {
    line->key = {};
    line->value = TrimLeadingManifestToken(buffer.substr(start, cursor - start));
  }
  return true;
}

const char* g_tracker_url = nullptr;

char g_streaming_locale[8] = {};

int g_streaming_build_number = 0;

int g_streaming_speed_test = 0;

bool g_streaming_initialized = false;
bool g_streaming_manifest_runtime_initialized = false;
char g_streaming_tracker_peer_id[21] = {};
char g_streaming_tracker_key[9] = {};

constexpr int kStormStatusChainTailLimit = 16;
constexpr char kEmptyStreamingStatusText[] = "";

struct StreamingStatusNode {
  int push_index{0};
  int storm_error{0};
  int context{0};
  std::string raw_text;
  std::string formatted_text;
  std::unique_ptr<StreamingStatusNode> next;
};

struct StreamingStatusState {
  std::unique_ptr<StreamingStatusNode> head;
  std::string message;
  int current_code{0};
  int context{0};
  int storm_error{0};
};

StreamingManifestState g_streaming_manifest_state;
std::array<double, 24> g_streaming_frame_delta_histogram = {};
std::uint32_t g_streaming_last_frame_tick = 0;
StreamingFrameCallback g_streaming_frame_callback = nullptr;

struct StreamingReportCounters {
  std::atomic<std::uint64_t> background_downloaded_bytes{0};
  std::atomic<std::uint64_t> deferred_downloaded_bytes{0};
  std::atomic<std::uint64_t> local_read_bytes{0};
  std::atomic<std::uint32_t> retry_count{0};
};

const char kUSTrackerURL[] =
    "http://us.tracker.worldofwarcraft.com:3724/announce";
const char kEUTrackerURL[] =
    "http://eu.tracker.worldofwarcraft.com:3724/announce";
constexpr std::uint32_t kStreamingTrackerTimeoutMs = 10000;

std::uint32_t DefaultStreamingFrameTick() {
  using clock = std::chrono::steady_clock;
  return static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          clock::now().time_since_epoch())
          .count());
}

bool DefaultStreamingTelemetryDispatch(const std::string& url) {

  std::string ignored_body;
  return openwow::net::DownloadUrlToStringWithResult(
      url.c_str(), &ignored_body, kStreamingTrackerTimeoutMs, nullptr);
}

std::function<std::uint32_t()>& StreamingFrameTickSource() {
  static std::function<std::uint32_t()> tick_source = DefaultStreamingFrameTick;
  return tick_source;
}

unsigned int DefaultStreamingRandomSeedSource() {
  return static_cast<unsigned int>(std::time(nullptr));
}

void DefaultStreamingRandomSeedSink(const unsigned int seed) {
  g_streaming_manifest_state.random.Seed(seed);
}

std::function<unsigned int()>& StreamingRandomSeedSource() {
  static std::function<unsigned int()> seed_source =
      DefaultStreamingRandomSeedSource;
  return seed_source;
}

std::function<void(unsigned int)>& StreamingRandomSeedSink() {
  static std::function<void(unsigned int)> seed_sink =
      DefaultStreamingRandomSeedSink;
  return seed_sink;
}

std::int64_t DefaultStreamingCurrentTimeSource() {
  return openwow::core::GameClock::GetCurrentTimeNsSince2000();
}

std::function<std::int64_t()>& StreamingCurrentTimeSource() {
  static std::function<std::int64_t()> current_time_source =
      DefaultStreamingCurrentTimeSource;
  return current_time_source;
}

int DefaultStreamingRandomValueSource() {
  return static_cast<int>(g_streaming_manifest_state.random.Next());
}

std::function<int()>& StreamingRandomValueSource() {
  static std::function<int()> random_value_source =
      DefaultStreamingRandomValueSource;
  return random_value_source;
}

void ClearStreamingManifestRuntimeState() {

  g_streaming_manifest_state = StreamingManifestState{};
  FileManifestRuntimeEntriesStorage().clear();
}

void EnsureStreamingManifestRuntimeInitialized() {
  if (g_streaming_manifest_runtime_initialized) {
    return;
  }

  StreamingRandomSeedSink()(StreamingRandomSeedSource()());
  g_streaming_manifest_runtime_initialized = true;
}

std::function<bool(const std::string&)>& StreamingTelemetryDispatch() {
  static std::function<bool(const std::string&)> dispatch =
      DefaultStreamingTelemetryDispatch;
  return dispatch;
}

StreamingReportCounters& MutableStreamingReportCounters() {
  static StreamingReportCounters counters;
  return counters;
}

StreamingStatusState& CurrentStreamingStatus() {

  static thread_local StreamingStatusState status;
  return status;
}

void ReplaceCurrentStreamingStatusChain(
    std::unique_ptr<StreamingStatusNode> replacement_head) {
  auto& status = CurrentStreamingStatus();
  status.head = std::move(replacement_head);
  if (!status.head) {
    status.message.clear();
    status.context = 0;
    status.storm_error = 0;
    return;
  }

  status.message = status.head->formatted_text;
  status.context = status.head->context;
  status.storm_error = status.head->storm_error;
}

int GetCurrentStreamingStatusCodeImpl() {
  return CurrentStreamingStatus().current_code;
}

void SetCurrentStreamingStatusCodeImpl(const int code) {
  CurrentStreamingStatus().current_code = code;
}

std::string BuildStreamingStatusText(const int push_index,
                                     const int storm_error,
                                     std::string_view raw_text,
                                     std::string_view previous_text) {
  std::string formatted;
  formatted.reserve(previous_text.size() + raw_text.size() + 64);
  formatted += '[';
  formatted += std::to_string(push_index);
  formatted += "] err=";
  formatted += std::to_string(storm_error);
  formatted += " text=";
  formatted.append(raw_text.data(), raw_text.size());
  formatted.push_back('\n');
  formatted.append(previous_text.data(), previous_text.size());
  return formatted;
}

void TrimStreamingStatusTail(StreamingStatusNode* head) {
  if (!head || head->push_index < kStormStatusChainTailLimit || !head->next) {
    return;
  }

  StreamingStatusNode* node = head;
  while (node->next && node->next->next) {
    node = node->next.get();
  }
  node->next.reset();
}

void PushStreamingStatusMessageImpl(std::string message,
                                    const int context,
                                    const int storm_error) {
  auto& status = CurrentStreamingStatus();
  auto node = std::make_unique<StreamingStatusNode>();
  node->push_index = status.head ? status.head->push_index + 1 : 0;
  node->storm_error = storm_error;
  node->context = context;
  node->raw_text = std::move(message);
  node->formatted_text = BuildStreamingStatusText(
      node->push_index, storm_error, node->raw_text, status.message);
  node->next = std::move(status.head);
  TrimStreamingStatusTail(node.get());
  ReplaceCurrentStreamingStatusChain(std::move(node));
}

std::uint64_t AdjustDownloadedBytes(const std::uint64_t downloaded_bytes) {
  return downloaded_bytes != 0 ? downloaded_bytes : 2u;
}

bool IsTrackerUrlSafe(const unsigned char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z')
         || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == '.'
         || ch == '-' || ch == '/';
}

std::string PercentEncodeTrackerComponent(std::string_view value) {
  static constexpr char kHex[] = "0123456789ABCDEF";

  std::string encoded;
  encoded.reserve(value.size() * 3);
  for (const unsigned char ch : value) {
    if (IsTrackerUrlSafe(ch)) {
      encoded.push_back(static_cast<char>(ch));
      continue;
    }

    encoded.push_back('%');
    encoded.push_back(kHex[(ch >> 4) & 0x0F]);
    encoded.push_back(kHex[ch & 0x0F]);
  }

  return encoded;
}

std::string BuildTrackerQueryFragment(std::string_view prefix,
                                      std::string_view value) {
  std::string fragment(prefix);
  fragment += PercentEncodeTrackerComponent(value);
  return fragment;
}

char* GetStreamingTrackerPeerId() {
  if (g_streaming_tracker_peer_id[0] == '\0') {
    for (std::size_t index = 0; index < 20; ++index) {
      g_streaming_tracker_peer_id[index] =
          static_cast<char>(g_streaming_manifest_state.random.Next() % 222u +
                            33u);
    }
    g_streaming_tracker_peer_id[20] = '\0';
  }
  return g_streaming_tracker_peer_id;
}

char* GetStreamingTrackerKey() {
  if (g_streaming_tracker_key[0] == '\0') {
    for (std::size_t index = 0; index < 8; ++index) {
      g_streaming_tracker_key[index] =
          static_cast<char>(g_streaming_manifest_state.random.Next() % 9u +
                            '0');
    }
    g_streaming_tracker_key[8] = '\0';
  }
  return g_streaming_tracker_key;
}

std::string BuildDefaultTrackerStatsSuffix() {
  std::string stats = "&stats=v,5,b,";
  stats += std::to_string(g_streaming_build_number);
  stats += ",l,";
  stats += g_streaming_locale;
  return stats;
}

std::string FormatStreamingFrameDeltaHistogramBuckets() {
  std::string buckets;
  double carry = 0.0;
  for (std::size_t index = 0; index < g_streaming_frame_delta_histogram.size();
       ++index) {
    const double bucket_value = carry + g_streaming_frame_delta_histogram[index];
    const auto rounded_value =
        static_cast<unsigned int>(bucket_value + 0.5);
    carry = bucket_value - static_cast<double>(rounded_value);
    buckets += std::to_string(rounded_value);
    if (index + 1 != g_streaming_frame_delta_histogram.size()) {
      buckets.push_back(',');
    }
  }
  return buckets;
}

std::string BuildStreamingReportStatsSuffix() {
  const auto& counters = MutableStreamingReportCounters();
  const auto elapsed_time_ns = std::max<std::int64_t>(
      0, openwow::core::GetInitTimerElapsedTimeNs());
  const auto elapsed_time_seconds =
      static_cast<double>(elapsed_time_ns) * 0.000000001;
  const auto background_downloaded_bytes =
      counters.background_downloaded_bytes.load(std::memory_order_relaxed);
  const auto deferred_downloaded_bytes =
      counters.deferred_downloaded_bytes.load(std::memory_order_relaxed);
  const auto local_read_bytes =
      counters.local_read_bytes.load(std::memory_order_relaxed);
  const auto retry_count = counters.retry_count.load(std::memory_order_relaxed);
  const auto compute_rate = [elapsed_time_seconds](const std::uint64_t bytes) {
    if (elapsed_time_seconds <= 0.0) {
      return 0;
    }
    return static_cast<int>(static_cast<double>(bytes) / elapsed_time_seconds);
  };

  std::string stats = BuildDefaultTrackerStatsSuffix();
  stats += ",st,";
  stats += std::to_string(g_streaming_speed_test);
  stats += ",t,";
  stats += std::to_string(
      static_cast<int>(0.000001 * static_cast<double>(elapsed_time_ns)));
  stats += ",sr,";
  stats += std::to_string(compute_rate(background_downloaded_bytes));
  stats += ",sd,";
  stats += std::to_string(compute_rate(deferred_downloaded_bytes));
  stats += ",sl,";
  stats += std::to_string(compute_rate(local_read_bytes));
  stats += ",rt,";
  stats += std::to_string(retry_count);
  stats += ",sl,";
  stats += std::to_string(
      openwow::core::StreamingStorage::Instance().GetBgPreloadSleep());
  stats += ",ft,";
  stats += FormatStreamingFrameDeltaHistogramBuckets();
  return stats;
}

std::string BuildTrackerUrl(std::string_view info_hash,
                            const int event_phase,
                            const std::uint64_t downloaded_bytes,
                            const char* stats_override) {
  if (!g_tracker_url) {
    return {};
  }

  std::string url(g_tracker_url);
  url += BuildTrackerQueryFragment("?info_hash=", info_hash);
  url += BuildTrackerQueryFragment("&peer_id=", GetStreamingTrackerPeerId());
  url += BuildTrackerQueryFragment("&key=", GetStreamingTrackerKey());
  url += "&port=3724";

  switch (event_phase) {
    case 0:
      url += "&uploaded=0&downloaded=0&left=2&event=started";
      break;
    case 1:
      url += "&uploaded=0&downloaded="
             + std::to_string(AdjustDownloadedBytes(downloaded_bytes))
             + "&left=0&event=completed";
      break;
    case 2:
      url += "&uploaded=0&downloaded="
             + std::to_string(AdjustDownloadedBytes(downloaded_bytes))
             + "&left=0&event=stopped";
      break;
    case 3:
      url += "&uploaded=0&downloaded=1&left=1&event=stopped";
      break;
    default:
      return url;
  }

  if (stats_override != nullptr) {
    url += stats_override;
  } else {
    url += BuildDefaultTrackerStatsSuffix();
  }
  return url;
}

void DispatchTrackerEvent(std::string_view info_hash,
                          const int event_phase,
                          const std::uint64_t downloaded_bytes,
                          const char* stats_override) {
  const std::string url =
      BuildTrackerUrl(info_hash, event_phase, downloaded_bytes, stats_override);
  if (url.empty()) {
    return;
  }

  auto& dispatch = StreamingTelemetryDispatch();
  if (dispatch) {
    dispatch(url);
  }
}

void SetStreamingStatusMessage(std::string message,
                               int context = 0,
                               int storm_error = 0) {
  PushStreamingStatusMessageImpl(std::move(message), context, storm_error);
}

void ResetCurrentStreamingStatusChainImpl() {
  ReplaceCurrentStreamingStatusChain(nullptr);
}

void ClearStreamingStatusMessage() {

  ResetCurrentStreamingStatusChainImpl();
}

bool ReadManifestFileFromDisk(const char* manifest_path, std::string* out_buffer) {
  if (!manifest_path || !out_buffer) {
    return false;
  }

  void* loaded_buffer = nullptr;
  int loaded_size = 0;
  if (!openwow::vfs::IOUnitContainer_LoadDirectFile(
          manifest_path, &loaded_buffer, &loaded_size, 4)) {
    return false;
  }

  out_buffer->assign(static_cast<const char*>(loaded_buffer),
                     static_cast<std::size_t>(
                         static_cast<std::uint32_t>(loaded_size)));
  (void)openwow::vfs::SFileFreeLoadedData(loaded_buffer);
  return true;
}

bool LoadManifestImpl(const char* manifest_path);
using ManifestBufferParser = bool (*)(std::string_view buffer);
struct DirectManifestStatusMessages {
  std::string_view load_failure;
  std::string_view parse_failure;
};

constexpr DirectManifestStatusMessages kFileManifestDirectFileStatusMessages = {
    "FileManifest::ReadDirectFile - LoadFile failed",
    "FileManifest::ReadDirectFile - ReadDirectFileFromBuffer failed",
};
constexpr DirectManifestStatusMessages kManifestInfoDirectFileStatusMessages = {
    "Manifests::ReadDirectFile - LoadFile failed",
    "Manifests::ReadDirectFile - ReadDirectFileFromBuffer failed",
};

bool LoadStreamingManifestDirectFile(const char* manifest_path,
                                     ManifestBufferParser parser,
                                     DirectManifestStatusMessages status_messages);
bool LoadStreamingManifestIndirectFile(const char* manifest_path,
                                       ManifestBufferParser parser);
bool DownloadIndirectManifestWithRetries(
  const char* manifest_path,
  std::string* manifest_buffer,
  openwow::net::OsUrlDownloadCompletionCode* completion_code);
bool LoadFileManifestDirectFile(const char* manifest_path);
bool LoadFileManifestIndirectFile(const char* manifest_path);
bool LoadFileManifestFile(const char* manifest_path);
bool LoadManifestInfoDirectFile(const char* manifest_path);
bool LoadManifestInfoIndirectFile(const char* manifest_path);
bool LoadManifestInfoFile(const char* manifest_path);

bool ParseManifestInfoBuffer(std::string_view buffer);
bool ParseFileManifestBuffer(std::string_view buffer);
bool ParseSourceManifestBuffer(std::string_view buffer);
bool ParseTransportManifestBuffer(std::string_view buffer);

bool LoadStreamingManifestDirectFile(const char* manifest_path,
                                     const ManifestBufferParser parser,
                                     const DirectManifestStatusMessages status_messages) {
  if (parser == nullptr) {
    return false;
  }

  std::string manifest_buffer;
  if (!ReadManifestFileFromDisk(manifest_path, &manifest_buffer)) {
    SetStreamingStatusMessage(std::string(status_messages.load_failure),
                              kManifestStatusContext);
    return false;
  }

  if (!parser(manifest_buffer)) {
    SetStreamingStatusMessage(std::string(status_messages.parse_failure),
                              kManifestStatusContext);
    return false;
  }

  return true;
}

bool LoadStreamingManifestIndirectFile(const char* manifest_path,
                                       const ManifestBufferParser parser) {
  if (parser == nullptr) {
    return false;
  }

  std::string manifest_buffer;
  openwow::net::OsUrlDownloadCompletionCode completion_code =
      openwow::net::OsUrlDownloadCompletionCode::kSuccess;
  if (DownloadIndirectManifestWithRetries(
          manifest_path, &manifest_buffer, &completion_code)) {
    return parser(manifest_buffer);
  }

  const int storm_error = static_cast<int>(completion_code);
  if (storm_error != 0 && manifest_path && *manifest_path) {
    SetStreamingStatusMessage(
        std::string("FileManifest::ReadIndirectFile - DownloadURL failed - ")
            + manifest_path,
        kManifestStatusContext,
        storm_error);
  } else {
    SetStreamingStatusMessage(
        "FileManifest::ReadIndirectFile - DownloadURL failed with no error",
        kManifestStatusContext,
        storm_error);
  }
  return false;
}

bool DownloadIndirectManifestWithRetries(
    const char* const manifest_path,
    std::string* const manifest_buffer,
    openwow::net::OsUrlDownloadCompletionCode* const completion_code) {
  if (manifest_buffer == nullptr || completion_code == nullptr) {
    return false;
  }

  std::uint32_t timeout_ms = kIndirectManifestTimeoutMs;
  for (int attempt = 0; attempt < kIndirectManifestRetryCount; ++attempt) {
    if (openwow::net::DownloadUrlToStringWithResult(
            manifest_path, manifest_buffer, timeout_ms, completion_code)) {
      return true;
    }

    manifest_buffer->clear();
    if (*completion_code
        == openwow::net::OsUrlDownloadCompletionCode::kTimeout) {
      timeout_ms *= 2u;
    }
  }

  return false;
}

bool LoadSourceManifestDirectFile(const char* manifest_path) {
  std::string manifest_buffer;
  if (!ReadManifestFileFromDisk(manifest_path, &manifest_buffer)) {
    SetStreamingStatusMessage("SourceManifest::ReadDirectfile - failed to load file",
                              kManifestStatusContext);
    return false;
  }

  return ParseSourceManifestBuffer(manifest_buffer);
}

bool LoadSourceManifestIndirectFile(const char* manifest_path) {
  std::string manifest_buffer;
  openwow::net::OsUrlDownloadCompletionCode completion_code =
      openwow::net::OsUrlDownloadCompletionCode::kSuccess;
  if (DownloadIndirectManifestWithRetries(
          manifest_path, &manifest_buffer, &completion_code)) {
    return ParseSourceManifestBuffer(manifest_buffer);
  }

  const int storm_error = static_cast<int>(completion_code);
  if (storm_error != 0) {
    SetStreamingStatusMessage(
        std::string("SourceManifest::ReadIndirectFile - DownloadURL failed - ")
            + manifest_path,
        kManifestStatusContext,
        storm_error);
    SetStreamingStatusMessage("Failed to load source manifest.",
                              kManifestStatusContext,
                              storm_error);
  } else {
    SetStreamingStatusMessage("SourceManifest::ReadIndirectFile - DownloadURL failed",
                              kManifestStatusContext);
  }
  return false;
}

bool LoadSourceManifest(const char* manifest_path) {
  if (!manifest_path || !*manifest_path) {
    return true;
  }

  if (AsciiStartsWithIgnoreCase(manifest_path, kIndirectManifestPrefix)) {
    return LoadSourceManifestIndirectFile(manifest_path);
  }

  return LoadSourceManifestDirectFile(manifest_path);
}

bool LoadTransportManifestDirectFile(const char* manifest_path) {
  std::string manifest_buffer;
  if (!ReadManifestFileFromDisk(manifest_path, &manifest_buffer)) {
    SetStreamingStatusMessage("Transport::ReadDirectFile - failed to load file",
                              kManifestStatusContext);
    return false;
  }

  return ParseTransportManifestBuffer(manifest_buffer);
}

bool LoadTransportManifestIndirectFile(const char* manifest_path) {
  std::string manifest_buffer;
  openwow::net::OsUrlDownloadCompletionCode completion_code =
      openwow::net::OsUrlDownloadCompletionCode::kSuccess;
  if (DownloadIndirectManifestWithRetries(
          manifest_path, &manifest_buffer, &completion_code)) {
    return ParseTransportManifestBuffer(manifest_buffer);
  }

  const int storm_error = static_cast<int>(completion_code);
  if (storm_error != 0 && manifest_path && *manifest_path) {
    SetStreamingStatusMessage(
        std::string("Transport::ReadIndirectFile - DownloadURL failed - ") + manifest_path,
        kManifestStatusContext,
        storm_error);
    SetStreamingStatusMessage("Failed to load transport manifest.",
                              kManifestStatusContext,
                              storm_error);
  } else {
    SetStreamingStatusMessage("Transport::ReadIndirectFile - DownloadURL failed",
                              kManifestStatusContext);
  }
  return false;
}

bool LoadTransportManifest(const char* manifest_path) {
  if (!manifest_path || !*manifest_path) {
    return true;
  }

  if (AsciiStartsWithIgnoreCase(manifest_path, kIndirectManifestPrefix)) {
    return LoadTransportManifestIndirectFile(manifest_path);
  }

  return LoadTransportManifestDirectFile(manifest_path);
}

bool LoadFileManifestDirectFile(const char* manifest_path) {
  return LoadStreamingManifestDirectFile(manifest_path,
                                         ParseFileManifestBuffer,
                                         kFileManifestDirectFileStatusMessages);
}

bool LoadFileManifestIndirectFile(const char* manifest_path) {
  return LoadStreamingManifestIndirectFile(manifest_path, ParseFileManifestBuffer);
}

bool LoadFileManifestFile(const char* manifest_path) {
  if (!manifest_path) {
    return true;
  }

  if (AsciiStartsWithIgnoreCase(manifest_path, kIndirectManifestPrefix)) {
    return LoadFileManifestIndirectFile(manifest_path);
  }

  return LoadFileManifestDirectFile(manifest_path);
}

bool LoadManifestInfoDirectFile(const char* manifest_path) {
  return LoadStreamingManifestDirectFile(manifest_path,
                                         ParseManifestInfoBuffer,
                                         kManifestInfoDirectFileStatusMessages);
}

bool LoadManifestInfoIndirectFile(const char* manifest_path) {
  return LoadStreamingManifestIndirectFile(manifest_path, ParseManifestInfoBuffer);
}

bool LoadManifestInfoFile(const char* manifest_path) {
  if (!manifest_path) {
    return true;
  }

  if (AsciiStartsWithIgnoreCase(manifest_path, kIndirectManifestPrefix)) {
    return LoadManifestInfoIndirectFile(manifest_path);
  }

  return LoadManifestInfoDirectFile(manifest_path);
}

bool ParseManifestInfoBuffer(std::string_view buffer) {
  std::size_t offset = 0;
  ManifestInfoActiveSection active_section;
  while (offset < buffer.size() && buffer[offset] != '\0') {
    ManifestLine line;
    if (!NextManifestInfoLine(buffer, &offset, &line)) {
      SetStreamingStatusMessage(
          "ManifestInfo::ReadDirectFileFromBuffer - GetItemTag failed",
          kManifestStatusContext);
      return false;
    }

    if (!line.has_key) {
      SetStreamingStatusMessage(
          "ManifestInfo::ReadDirectFileFromBuffer - Tag Parsing failed",
          kManifestStatusContext);
      return false;
    }

    if (TryApplyManifestInfoActiveField(active_section, line.key, line.value)) {
      continue;
    }

    if (openwow::text::EqualsIgnoreCaseAscii(line.key, "version")) {
      g_streaming_manifest_state.version =
          static_cast<std::int32_t>(
              std::strtol(std::string(line.value).c_str(), nullptr, 10));
      if (g_streaming_manifest_state.version == 1) {
        return ParseFileManifestBuffer(buffer.substr(offset));
      }
      continue;
    }

    if (openwow::text::EqualsIgnoreCaseAscii(line.key, "manifest")) {
      const std::string nested_manifest(line.value);
      (void)LoadManifestInfoFile(nested_manifest.c_str());
      continue;
    }

    if (openwow::text::EqualsIgnoreCaseAscii(line.key, "timeslot")) {
      auto [slot_it, inserted] =
          g_streaming_manifest_state.timeslots.try_emplace(std::string(line.value));
      if (!inserted) {
        slot_it->second = {};
      }
      active_section.kind = ManifestInfoActiveSectionKind::kTimeslot;
      active_section.parameter_name = {};
      active_section.file_entry = nullptr;
      active_section.server = nullptr;
      active_section.server_path = nullptr;
      active_section.transport_section = nullptr;
      active_section.timeslot = &slot_it->second;
      continue;
    }

    if (openwow::text::EqualsIgnoreCaseAscii(line.key, "file")) {
      const std::string lookup_key = BuildManifestFileLookupPath(line.value);
      auto [file_it, inserted] =
          g_streaming_manifest_state.file_entries.try_emplace(lookup_key);
      if (!inserted) {
        file_it->second = {};
      }
      file_it->second.raw_path.assign(line.value);
      file_it->second.lookup_path = lookup_key;
      active_section.kind = ManifestInfoActiveSectionKind::kFile;
      active_section.parameter_name = {};
      active_section.file_entry = &file_it->second;
      active_section.server = nullptr;
      active_section.server_path = nullptr;
      active_section.transport_section = nullptr;
      active_section.timeslot = nullptr;
      continue;
    }

    if (openwow::text::EqualsIgnoreCaseAscii(line.key, "parameter")) {
      ApplyManifestParameter(line.value, {});
      active_section.kind = ManifestInfoActiveSectionKind::kParameter;
      active_section.parameter_name = line.value;
      active_section.file_entry = nullptr;
      active_section.server = nullptr;
      active_section.server_path = nullptr;
      active_section.transport_section = nullptr;
      active_section.timeslot = nullptr;
      continue;
    }

    if (openwow::text::EqualsIgnoreCaseAscii(line.key, "server")) {
      auto [server_it, inserted] =
          g_streaming_manifest_state.servers.try_emplace(std::string(line.value));
      if (!inserted) {
        server_it->second = {};
      }
      active_section.kind = ManifestInfoActiveSectionKind::kServer;
      active_section.parameter_name = {};
      active_section.file_entry = nullptr;
      active_section.server = &server_it->second;
      active_section.server_path = nullptr;
      active_section.transport_section = nullptr;
      active_section.timeslot = nullptr;
      continue;
    }

    if (openwow::text::EqualsIgnoreCaseAscii(line.key, "serverpath")) {
      auto [server_path_it, inserted] =
          g_streaming_manifest_state.path_aliases.try_emplace(
              std::string(line.value));
      if (!inserted) {
        server_path_it->second.clear();
      }
      active_section.kind = ManifestInfoActiveSectionKind::kServerPath;
      active_section.parameter_name = {};
      active_section.file_entry = nullptr;
      active_section.server = nullptr;
      active_section.server_path = &server_path_it->second;
      active_section.transport_section = nullptr;
      active_section.timeslot = nullptr;
      continue;
    }

    if (openwow::text::EqualsIgnoreCaseAscii(line.key, "transport")) {
      auto [transport_it, inserted] =
          g_streaming_manifest_state.transport_sections.try_emplace(
              std::string(line.value));
      if (!inserted) {
        transport_it->second = {};
      }
      active_section.kind = ManifestInfoActiveSectionKind::kTransport;
      active_section.parameter_name = {};
      active_section.file_entry = nullptr;
      active_section.server = nullptr;
      active_section.server_path = nullptr;
      active_section.transport_section = &transport_it->second;
      active_section.timeslot = nullptr;
      continue;
    }

    SetStreamingStatusMessage(
        "ManifestInfo::ReadDirectFileFromBuffer - Tag Parsing failed",
        kManifestStatusContext);
    return false;
  }

  return true;
}

bool ParseSourceManifestBuffer(std::string_view buffer) {
  std::size_t offset = 0;
  while (offset < buffer.size() && buffer[offset] != '\0') {
    ManifestLine line;

    if (!NextManifestLine(buffer,
                          &offset,
                          &line,
                          ManifestEmbeddedNulPolicy::kContinueAfterLine)) {
      SetStreamingStatusMessage(
          "SourceManifest::ReadDirectfile - GetItemTag failed");
      return false;
    }

    if (line.has_key && AsciiStartsWithIgnoreCase(line.key, "version")) {
      if (std::strtol(std::string(line.value).c_str(), nullptr, 10) != 1) {
        return false;
      }
      continue;
    }

    if (line.has_key && AsciiStartsWithIgnoreCase(line.key, "source")) {
      RegisterManifestSourceLocator("old-version1-base", line.value);
      continue;
    }

    StreamingSourceManifestEntry entry;
    if (!ParseSourceManifestEntryLine(line.value, &entry)) {
      continue;
    }

    RegisterManifestSourceLocator(entry.alias, entry.locator);
  }

  return true;
}

bool ParseTransportManifestBuffer(std::string_view buffer) {
  std::size_t offset = 0;
  while (offset < buffer.size()) {
    ManifestLine line;
    if (!NextManifestLine(buffer,
                          &offset,
                          &line,
                          ManifestEmbeddedNulPolicy::kStopAfterLine)) {
      break;
    }

    if (line.has_key && AsciiStartsWithIgnoreCase(line.key, "version")) {
      if (std::strtol(std::string(line.value).c_str(), nullptr, 10) != 1) {
        return false;
      }
      continue;
    }

    const std::string_view entry_line = line.value;
    std::string entry_name;
    StreamingManifestTransportSection transport_section;

    (void)ParseTransportManifestEntry(entry_line,
                                      &entry_name,
                                      &transport_section);

    auto [transport_it, inserted] =
        g_streaming_manifest_state.transport_sections.try_emplace(entry_name);
    (void)inserted;
    transport_it->second = transport_section;
  }

  return true;
}

bool ParseFileManifestBuffer(std::string_view buffer) {
  const auto append_legacy_file_entry = [](std::string_view record) {
    if (record.empty()) {
      return;
    }

    StreamingLegacyFileEntry entry;
    if (ParseLegacyManifestFileEntry(record, &entry)) {
      auto [file_it, inserted] =
          g_streaming_manifest_state.file_entries.try_emplace(entry.lookup_path);
      if (!inserted) {
        file_it->second = {};
      }
      file_it->second.raw_path = entry.raw_path;
      file_it->second.lookup_path = entry.lookup_path;
      file_it->second.entry.name = entry.raw_path;
      file_it->second.entry.path = "old-version1-base";
      file_it->second.entry.transportItem = entry.raw_path;
      file_it->second.entry.size = entry.size;
      file_it->second.entry.fileVersion = entry.file_version;
      file_it->second.entry.flags = entry.flags;
      if (g_streaming_manifest_state.path_aliases.contains(entry.raw_path)) {
        file_it->second.entry.path = entry.raw_path;
      }
      g_streaming_manifest_state.legacy_file_entries.push_back(std::move(entry));
    }
  };

  std::size_t offset = 0;
  while (offset < buffer.size()) {
    ManifestLine line;
    if (!NextManifestLine(buffer,
                          &offset,
                          &line,
                          ManifestEmbeddedNulPolicy::kContinueAfterLine)) {
      break;
    }

    if (!line.has_key) {
      append_legacy_file_entry(line.value);
      continue;
    }

    if (AsciiStartsWithIgnoreCase(line.key, "version")) {
      if (std::strtol(std::string(line.value).c_str(), nullptr, 10) != 1) {
        return false;
      }
      continue;
    }

    if (AsciiStartsWithIgnoreCase(line.key, "isTrial")) {
      ApplyManifestParameter(kManifestParameterIsTrial, line.value);
      continue;
    }

    if (AsciiStartsWithIgnoreCase(line.key, "manifest")) {
      const std::string nested_manifest(line.value);
      return LoadFileManifestFile(nested_manifest.c_str());
    }

    if (AsciiStartsWithIgnoreCase(line.key, "sourcemanifest")) {
      if (line.value.empty()) {
        continue;
      }
      g_streaming_manifest_state.source_manifest.assign(line.value);
      g_streaming_manifest_state.source_manifest_is_indirect =
          AsciiStartsWithIgnoreCase(line.value, kIndirectManifestPrefix);
      if (!LoadSourceManifest(g_streaming_manifest_state.source_manifest.c_str())) {
        SetStreamingStatusMessage(
            "FileManifest::ReadDirectFileFromBuffer - SourceManifest::Initialize failed",
            kManifestStatusContext);
        return false;
      }
      continue;
    }

    if (AsciiStartsWithIgnoreCase(line.key, "transportmanifest")) {
      if (line.value.empty()) {
        continue;
      }
      g_streaming_manifest_state.transport_manifest.assign(line.value);
      g_streaming_manifest_state.transport_manifest_is_indirect =
          AsciiStartsWithIgnoreCase(line.value, kIndirectManifestPrefix);
      if (!LoadTransportManifest(g_streaming_manifest_state.transport_manifest.c_str())) {
        SetStreamingStatusMessage(
            "FileManifest::ReadDirectFileFromBuffer - Transport::Initialize failed",
            kManifestStatusContext);
        return false;
      }
      continue;
    }

    if (AsciiStartsWithIgnoreCase(line.key, "source")) {
      RegisterManifestSourceLocator("old-version1-base", line.value);
      continue;
    }

    if (AsciiStartsWithIgnoreCase(line.key, "bgpreloadsleep")) {
      ApplyManifestParameter(kManifestParameterBgPreloadSleep, line.value);
      continue;
    }

    append_legacy_file_entry(line.value);
  }

  return true;
}

bool LoadManifestImpl(const char* manifest_path) {
  if (!manifest_path) {
    return true;
  }

  EnsureStreamingManifestRuntimeInitialized();

  const bool loaded = LoadManifestInfoFile(manifest_path);
  if (loaded) {
    const bool files_resolved = ResolveManifestFileEntries();
    SyncFileManifestRuntimeEntriesFromState();
    const bool timeslots_resolved = ResolveManifestServerTimeslotSchedules();
    return files_resolved && timeslots_resolved;
  }

  return false;
}

}

int GetCurrentStreamingStatusCode() {
  return GetCurrentStreamingStatusCodeImpl();
}

void SetCurrentStreamingStatusCode(int code) {
  SetCurrentStreamingStatusCodeImpl(code);
}

void PushStreamingStatusMessage(std::string message, int context, int storm_error) {
  PushStreamingStatusMessageImpl(std::move(message), context, storm_error);
}

void ResetCurrentStreamingStatusChain() {
  ResetCurrentStreamingStatusChainImpl();
}

const char* ManifestArchiveIndexToName(const std::uint32_t index) {
  if (index >= kManifestArchiveNameTable.size()) {
    return "";
  }
  return kManifestArchiveNameTable[index];
}

const char* GetTrackerURLForLocale(const char* locale) {
  if (!locale) {
    return nullptr;
  }

  if (std::strcmp(locale, "enUS") == 0 || std::strcmp(locale, "esMX") == 0) {
    return kUSTrackerURL;
  }

  if (std::strcmp(locale, "deDE") == 0 || std::strcmp(locale, "enGB") == 0
      || std::strcmp(locale, "esES") == 0
      || std::strcmp(locale, "frFR") == 0) {
    return kEUTrackerURL;
  }

  return nullptr;
}

void Streaming_SetLocale(const char* locale) {
  g_tracker_url = GetTrackerURLForLocale(locale);
  std::memset(g_streaming_locale, 0, sizeof(g_streaming_locale));
  if (locale) {
    std::strncpy(g_streaming_locale, locale, 5);
    g_streaming_locale[4] = '\0';
  }
}

void Streaming_SetBuildNumber(int build_number) {
  g_streaming_build_number = build_number;
}

void Streaming_SetSpeedTest(int speed_test) {
  g_streaming_speed_test = speed_test;
}

void Streaming_ReportStats(bool is_startup, bool has_new_account) {
  if (is_startup) {
    if (has_new_account) {
      DispatchTrackerEvent("HOWMANYUNIQUEUSERARE", 0, 0, nullptr);
      DispatchTrackerEvent("HOWMANYUNIQUEUSERARE", 1, 0, nullptr);
      DispatchTrackerEvent("HOWMANYUNIQUEUSERARE", 2, 0, nullptr);
    }
    DispatchTrackerEvent("DOUSERSLOVESTREAMING", 0, 0, nullptr);
  } else {
    const auto& counters = MutableStreamingReportCounters();
    const auto downloaded_bytes =
        counters.background_downloaded_bytes.load(std::memory_order_relaxed)
        + counters.deferred_downloaded_bytes.load(std::memory_order_relaxed);
    std::string stats = BuildStreamingReportStatsSuffix();

    DispatchTrackerEvent(
        "DOUSERSLOVESTREAMING", 1, downloaded_bytes, stats.c_str());
    DispatchTrackerEvent(
        "DOUSERSLOVESTREAMING", 2, downloaded_bytes, nullptr);
  }
}

void Streaming_RecordDeferredDownloadBytes(const std::uint64_t bytes) {
  if (bytes == 0) {
    return;
  }

  MutableStreamingReportCounters().deferred_downloaded_bytes.fetch_add(
      bytes, std::memory_order_relaxed);
}

void Streaming_RecordLocalReadBytes(const std::uint64_t bytes) {
  if (bytes == 0) {
    return;
  }

  MutableStreamingReportCounters().local_read_bytes.fetch_add(
      bytes, std::memory_order_relaxed);
}

void Streaming_RecordDownloadRetry() {
  MutableStreamingReportCounters().retry_count.fetch_add(
      1u, std::memory_order_relaxed);
}

bool Streaming_ConfigureBgPreloadSleep(int speed_test) {
  return openwow::core::StreamingStorage::ConfigureBgPreloadSleep(speed_test);
}

int Streaming_RegisterFrameCallback(StreamingFrameCallback callback) {
  g_streaming_frame_callback = callback;
  return static_cast<int>(reinterpret_cast<std::uintptr_t>(callback));
}

void Streaming_RecordFrameDeltaHistogram() {
  const std::uint32_t current_tick = StreamingFrameTickSource()();
  if (g_streaming_last_frame_tick == 0) {
    g_streaming_last_frame_tick = current_tick;
    return;
  }

  const std::uint32_t delta = current_tick - g_streaming_last_frame_tick;
  g_streaming_last_frame_tick = current_tick;

  std::size_t bucket = 0;
  while (bucket + 1 < kFrameDeltaThresholds.size()
         && delta >= kFrameDeltaThresholds[bucket + 1]) {
    if (bucket + 2 >= kFrameDeltaThresholds.size()) {
      break;
    }
    if (delta < kFrameDeltaThresholds[bucket + 2]) {
      ++bucket;
      break;
    }
    if (bucket + 3 < kFrameDeltaThresholds.size()
        && delta < kFrameDeltaThresholds[bucket + 3]) {
      bucket += 2;
      break;
    }
    if (bucket + 4 < kFrameDeltaThresholds.size()
        && delta < kFrameDeltaThresholds[bucket + 4]) {
      bucket += 3;
      break;
    }
    if (bucket + 5 < kFrameDeltaThresholds.size()
        && delta < kFrameDeltaThresholds[bucket + 5]) {
      bucket += 4;
      break;
    }
    if (bucket + 6 < kFrameDeltaThresholds.size()
        && delta < kFrameDeltaThresholds[bucket + 6]) {
      bucket += 5;
      break;
    }
    bucket += 6;
    if (bucket >= g_streaming_frame_delta_histogram.size()) {
      break;
    }
  }

  if (bucket == 0) {
    g_streaming_frame_delta_histogram[0] += 1.0;
    return;
  }

  if (bucket >= g_streaming_frame_delta_histogram.size() - 1) {
    g_streaming_frame_delta_histogram.back() += 1.0;
    return;
  }

  double low_midpoint =
      0.5 * static_cast<double>(kFrameDeltaThresholds[bucket]
                                + kFrameDeltaThresholds[bucket + 1] - 1);
  std::size_t low_bucket = bucket;
  double high_midpoint = 0.0;
  if (static_cast<std::uint32_t>(low_midpoint) <= delta) {
    high_midpoint =
        0.5 * static_cast<double>(kFrameDeltaThresholds[bucket + 1]
                                  + kFrameDeltaThresholds[bucket + 2] - 1);
  } else {
    high_midpoint = low_midpoint;
    --low_bucket;
    low_midpoint =
        0.5 * static_cast<double>(kFrameDeltaThresholds[low_bucket]
                                  + kFrameDeltaThresholds[low_bucket + 1] - 1);
  }

  const double low_weight =
      (high_midpoint - static_cast<double>(delta)) / (high_midpoint - low_midpoint);
  g_streaming_frame_delta_histogram[low_bucket] += low_weight;
  g_streaming_frame_delta_histogram[low_bucket + 1] += 1.0 - low_weight;
}

void Streaming_RunRegisteredFrameCallback() {
  if (g_streaming_frame_callback) {
    g_streaming_frame_callback();
  }
}

bool Streaming_LoadManifest(const char* manifest_path) {
  return LoadManifestImpl(manifest_path);
}

bool InitializeStreaming(const char* manifest_path,
                         const char* storage_path,
                         const char* path_variant_suffix,
                         const char* data_path_override,
                         bool force_streaming) {
  if (g_streaming_initialized) {
    return true;
  }

  openwow::vfs::InvalidateStreamingManifestLookupPathCache();

  char saved_working_dir[260] = {};
  if (!openwow::vfs::FileSystem_GetWorkingDirectory(
          saved_working_dir, static_cast<int>(sizeof(saved_working_dir)))) {
    SetStreamingStatusMessage("InitializeStreaming - Failed to get working directory",
                              kManifestStatusContext,
                              0);
    return false;
  }

  std::filesystem::path active_data_path =
      openwow::vfs::ToNativePath(saved_working_dir);
  if (manifest_path
      && !AsciiStartsWithIgnoreCase(manifest_path, kIndirectManifestPrefix)) {
    char absolute_manifest[260] = {};
    if (!openwow::vfs::ResolveExistingPathAbsolute(
            manifest_path, absolute_manifest, static_cast<int>(sizeof(absolute_manifest)),
            openwow::vfs::ExistingPathRequirement::kFileOnly)) {
      SetStreamingStatusMessage("InitializeStreaming - Failed to make absolute manifest path",
                                kManifestStatusContext,
                                0);
      return false;
    }
    active_data_path = openwow::vfs::ToNativePath(absolute_manifest).parent_path();
  }

  if (data_path_override && *data_path_override) {
    char absolute_override[260] = {};
    if (openwow::vfs::ResolveExistingPathAbsolute(
            data_path_override, absolute_override,
            static_cast<int>(sizeof(absolute_override)),
            openwow::vfs::ExistingPathRequirement::kDirectoryOnly)) {
      active_data_path = openwow::vfs::ToNativePath(absolute_override);
    } else {
      SetStreamingStatusMessage("InitializeStreaming - Invalid data path specified",
                                kManifestStatusContext,
                                0);
    }
  }

  if (path_variant_suffix && *path_variant_suffix) {
    openwow::core::StreamingStorage::Instance().SetVariantPathSuffix(
        path_variant_suffix);
  }

  const std::string active_data_path_storm =
      openwow::vfs::ToStormPathString(active_data_path);
  if (!openwow::vfs::FileSystem_SetWorkingDirectory(
          active_data_path_storm.c_str())) {
    SetStreamingStatusMessage("InitializeStreaming - Failed to change working directory",
                              kManifestStatusContext,
                              0);
    return false;
  }

  const bool manifest_ok = Streaming_LoadManifest(manifest_path);
  if (!manifest_ok) {
    (void)openwow::vfs::FileSystem_SetWorkingDirectory(saved_working_dir);
    SetStreamingStatusMessage("InitializeStreaming - Could not load manifest",
                              kManifestStatusContext,
                              0);
    return false;
  }

  bool storage_ok = false;
  auto& streaming_storage = openwow::core::StreamingStorage::Instance();
  if (storage_path && *storage_path) {
    storage_ok = streaming_storage.Init(storage_path, force_streaming);
    if (!storage_ok && !force_streaming) {
      storage_ok = streaming_storage.InitSimple();
      (void)openwow::core::DeleteStormFilePath(storage_path);
    }
  } else {
    storage_ok = streaming_storage.InitSimple();
  }

  if (!storage_ok) {
    (void)openwow::vfs::FileSystem_SetWorkingDirectory(saved_working_dir);
    SetStreamingStatusMessage("InitializeStreaming - Could not initialize storage",
                              kManifestStatusContext,
                              0);
    return false;
  }

  streaming_storage.ResetPreloadPartialProgressCache();
  streaming_storage.InitializeBackgroundDownloadWorkers();
  g_streaming_initialized = true;

  if (!openwow::vfs::FileSystem_SetWorkingDirectory(saved_working_dir)) {
    SetStreamingStatusMessage("InitializeStreaming - Failed to restore working directory",
                              kManifestStatusContext,
                              0);
    return false;
  }

  return true;
}

void ShutdownStreaming() {
  openwow::core::StreamingStorage::Instance().Shutdown();
  g_streaming_initialized = false;
  g_streaming_manifest_runtime_initialized = false;
  ClearStreamingManifestRuntimeState();
  openwow::vfs::InvalidateStreamingManifestLookupPathCache();
}

bool IsStreamingInitialized() {
  return g_streaming_initialized;
}

bool IsOnlineModeActive() {
  return g_streaming_initialized && g_streaming_manifest_state.trial_mode;
}

const std::string& GetStreamingStatusMessage() {
  return CurrentStreamingStatus().message;
}

const char* GetStreamingStatusMessageCString() {
  const auto& status = CurrentStreamingStatus();
  return status.head ? status.head->formatted_text.c_str()
                     : kEmptyStreamingStatusText;
}

bool StreamingStatusContainsContext(int context) {
  const auto& status = CurrentStreamingStatus();
  for (const StreamingStatusNode* node = status.head.get(); node;
       node = node->next.get()) {
    if (node->context == context) {
      return true;
    }
  }
  return false;
}

bool StreamingStatusContainsContextAndError(int context, int storm_error) {
  const auto& status = CurrentStreamingStatus();
  for (const StreamingStatusNode* node = status.head.get(); node;
       node = node->next.get()) {
    if (node->context == context && node->storm_error == storm_error) {
      return true;
    }
  }
  return false;
}

const StreamingManifestState& GetStreamingManifestState() {
  return g_streaming_manifest_state;
}

std::int64_t NormalizeManifestScheduleQueryTime(
    const std::int64_t time_ns_since_2000) {
  const auto breakdown =
      openwow::core::ida::CalendarTimeBreakdownFromNsSince2000(
          time_ns_since_2000);
  openwow::core::ida::CalendarTimeFields fields;
  fields.year = 2000;
  fields.month = 1;
  fields.day = 1;
  fields.hour = breakdown.hour;
  fields.minute = breakdown.minute;
  return openwow::core::ida::CalendarTimeNsSince2000FromFields(fields);
}

std::int32_t GetManifestServerCurrentScheduleUsage(
    const StreamingManifestServer& server,
    const std::int64_t time_ns_since_2000) {
  if (!server.enabled) {
    return 0;
  }

  const auto normalized_time =
      NormalizeManifestScheduleQueryTime(time_ns_since_2000);
  std::int32_t usage = 0;
  for (const auto& entry : server.schedule) {
    if (entry.start_time_ns_since_2000 > normalized_time) {
      break;
    }
    usage = entry.usage;
  }

  return usage;
}

struct SelectedManifestFileServer {
  std::string location;
  std::int32_t max_retry{5};
};

std::optional<SelectedManifestFileServer> SelectManifestFileServer(
    const openwow::core::FileManifestEntry& entry) {
  struct WeightedCandidate {
    StreamingManifestServer* server = nullptr;
    std::int32_t cumulative_weight = 0;
  };

  const auto query_time = StreamingCurrentTimeSource()();
  std::vector<WeightedCandidate> candidates;
  candidates.reserve(entry.validServers.empty()
                         ? g_streaming_manifest_state.servers.size()
                         : entry.validServers.size());

  std::int32_t total_weight = 0;
  const auto append_candidate = [&](StreamingManifestServer& server) {
    const auto usage = GetManifestServerCurrentScheduleUsage(server, query_time);
    if (usage <= 0) {
      return;
    }

    total_weight += usage;
    candidates.push_back(
        {.server = &server, .cumulative_weight = total_weight});
  };

  if (entry.validServers.empty()) {
    for (auto& [server_name, server] : g_streaming_manifest_state.servers) {
      (void)server_name;
      append_candidate(server);
    }
  } else {
    for (const auto& server_name : entry.validServers) {
      const auto server_it = g_streaming_manifest_state.servers.find(server_name);
      if (server_it == g_streaming_manifest_state.servers.end()) {
        continue;
      }
      append_candidate(server_it->second);
    }
  }

  if (total_weight <= 0 || candidates.empty()) {
    return std::nullopt;
  }

  const auto random_value = StreamingRandomValueSource()();
  const auto selected_weight = static_cast<std::int32_t>(
      static_cast<std::uint32_t>(random_value)
      % static_cast<std::uint32_t>(total_weight));
  const auto candidate_it = std::find_if(
      candidates.begin(),
      candidates.end(),
      [selected_weight](const WeightedCandidate& candidate) {
        return candidate.cumulative_weight > selected_weight;
      });
  if (candidate_it == candidates.end() || candidate_it->server == nullptr) {
    return std::nullopt;
  }

  ++candidate_it->server->selection_count;
  return SelectedManifestFileServer{
      .location = candidate_it->server->location,
      .max_retry = candidate_it->server->max_retry,
  };
}

std::string SelectManifestFileServerLocation(
    const openwow::core::FileManifestEntry& entry) {
  const auto selected = SelectManifestFileServer(entry);
  return selected ? selected->location : std::string{};
}

std::string BuildFileManifestDirectFilePath(
    const std::string_view lookup_key) {
  const auto request = BuildFileManifestDirectFileRequest(lookup_key);
  return request ? request->source_url : std::string{};
}

std::optional<StreamingManifestDirectFileRequest>
BuildFileManifestDirectFileRequest(const std::string_view lookup_key) {
  const auto& entries = FileManifestRuntimeEntries();
  const auto it = FindFileManifestEntryByLookupKeyConst(lookup_key);
  if (it == entries.end()) {
    return std::nullopt;
  }

  const std::string relative_path =
      openwow::core::StreamingStorage::BuildManifestEntryRelativeFilePath(
          it->entry);
  std::string direct_file_path;
  std::int32_t max_retry = 5;
  if (const auto selected_server = SelectManifestFileServer(it->entry);
      selected_server.has_value()) {
    direct_file_path = selected_server->location;
    max_retry = selected_server->max_retry;
  }

  direct_file_path.reserve(direct_file_path.size() + relative_path.size());
  direct_file_path.append(relative_path);
  return StreamingManifestDirectFileRequest{
      .source_url = std::move(direct_file_path),
      .max_retry = max_retry,
      .resolved_transport_section = it->resolved_transport_section,
  };
}

std::vector<std::string> EnumerateFileManifestDirectoryFiles(
    const std::string_view path) {
  const std::string normalized_directory =
      NormalizeFileManifestDirectoryQuery(path);

  std::vector<std::string> files;
  const auto& entries = FileManifestRuntimeEntries();
  files.reserve(entries.size());
  for (const auto& runtime_entry : entries) {
    if (runtime_entry.prefix_gate_count < 1) {
      continue;
    }

    const auto match = SplitFileManifestDirectoryMatch(runtime_entry.entry);
    if (!openwow::text::EqualsIgnoreCaseAscii(normalized_directory,
                                              match.directory_prefix)) {
      continue;
    }

    files.push_back(match.filename);
  }
  return files;
}

int GetFileManifestPrefixStatus(const std::string_view lookup_key,
                                const std::string_view path) {
  const auto& entries = FileManifestRuntimeEntries();
  if (FindFileManifestEntryByLookupKeyConst(lookup_key) == entries.end()) {
    return 5;
  }

  const auto it = FindMatchingFileManifestPrefixEntryConst(path);
  if (it == entries.end()) {
    return 5;
  }

  return it->prefix_status_code;
}

std::optional<bool> GetFileManifestEntryActiveFlag(
    const std::string_view lookup_key) {
  const auto& entries = FileManifestRuntimeEntries();
  const auto it = FindFileManifestEntryByLookupKeyConst(lookup_key);
  if (it == entries.end()) {
    return std::nullopt;
  }

  return it->prefix_active;
}

std::optional<StreamingManifestTransportSection>
GetFileManifestResolvedTransportSection(const std::string_view lookup_key) {
  const auto& entries = FileManifestRuntimeEntries();
  const auto it = FindFileManifestEntryByLookupKeyConst(lookup_key);
  if (it == entries.end()) {
    return std::nullopt;
  }

  return it->resolved_transport_section;
}

void RegisterFileManifestEntry(openwow::core::FileManifestEntry entry,
                               const std::int32_t prefix_gate_count,
                               const std::int32_t prefix_status_code,
                               const bool prefix_active,
                               std::optional<StreamingManifestTransportSection>
                                   resolved_transport_section) {
  std::string normalized_lookup_key =
      NormalizeFileManifestRuntimeLookupKey(entry);
  InsertFileManifestRuntimeEntryWithLookupKey(
      std::move(normalized_lookup_key),
      std::move(entry),
      std::move(resolved_transport_section),
      prefix_gate_count,
      prefix_status_code,
      prefix_active);
}

void ClearFileManifestPrefixActiveOnDeferredReadFailure(
    const std::string_view lookup_key,
    const std::string_view path) {
  auto& entries = FileManifestRuntimeEntries();
  if (FindFileManifestEntryByLookupKey(lookup_key) == entries.end()) {
    return;
  }

  for (auto& entry : entries) {
    if (!AsciiPrefixEqualsIgnoreCase(path, entry.entry.path)) {
      continue;
    }
    if (entry.prefix_gate_count >= 1) {
      entry.prefix_active = false;
    }
  }
}

const char* GetStreamingTrackerURL() {
  return g_tracker_url;
}

const char* GetStreamingLocaleTag() {
  return g_streaming_locale;
}

int GetStreamingBuildNumber() {
  return g_streaming_build_number;
}

int GetStreamingSpeedTest() {
  return g_streaming_speed_test;
}

void ClearStreamingStatusMessageForInit() {
  ClearStreamingStatusMessage();
}

const std::array<double, 24>& GetStreamingFrameDeltaHistogramForTests() {
  return g_streaming_frame_delta_histogram;
}

void SetStreamingFrameDeltaHistogramForTests(
    const std::array<double, 24>& histogram) {
  g_streaming_frame_delta_histogram = histogram;
}

void SetStreamingReportCountersForTests(
    const std::uint64_t background_downloaded_bytes,
    const std::uint64_t deferred_downloaded_bytes,
    const std::uint64_t local_read_bytes,
    const std::uint32_t retry_count) {
  auto& counters = MutableStreamingReportCounters();
  counters.background_downloaded_bytes.store(background_downloaded_bytes,
                                             std::memory_order_relaxed);
  counters.deferred_downloaded_bytes.store(deferred_downloaded_bytes,
                                           std::memory_order_relaxed);
  counters.local_read_bytes.store(local_read_bytes, std::memory_order_relaxed);
  counters.retry_count.store(retry_count, std::memory_order_relaxed);
}

std::string PercentEncodeTrackerComponentForTests(std::string_view value) {
  return PercentEncodeTrackerComponent(value);
}

std::string BuildTrackerUrlForTests(std::string_view info_hash,
                                    const int event_phase,
                                    const std::uint64_t downloaded_bytes,
                                    const char* stats_override) {
  return BuildTrackerUrl(info_hash, event_phase, downloaded_bytes,
                         stats_override);
}

void SetStreamingFrameTickSourceForTests(
    std::function<std::uint32_t()> tick_source) {
  StreamingFrameTickSource() =
      tick_source ? std::move(tick_source) : DefaultStreamingFrameTick;
}

void SetStreamingRandomSeedSourceForTests(
    std::function<unsigned int()> seed_source) {
  StreamingRandomSeedSource() = seed_source ? std::move(seed_source)
                                            : DefaultStreamingRandomSeedSource;
}

void SetStreamingRandomSeedSinkForTests(
    std::function<void(unsigned int)> seed_sink) {
  StreamingRandomSeedSink() = seed_sink ? std::move(seed_sink)
                                        : DefaultStreamingRandomSeedSink;
}

void SetStreamingCurrentTimeSourceForTests(
    std::function<std::int64_t()> current_time_source) {
  StreamingCurrentTimeSource() =
      current_time_source ? std::move(current_time_source)
                          : DefaultStreamingCurrentTimeSource;
}

void SetStreamingRandomValueSourceForTests(
    std::function<int()> random_value_source) {
  StreamingRandomValueSource() =
      random_value_source ? std::move(random_value_source)
                          : DefaultStreamingRandomValueSource;
}

void SetStreamingTelemetryDispatchForTests(
    std::function<bool(const std::string&)> dispatch) {
  StreamingTelemetryDispatch() =
      dispatch ? std::move(dispatch) : DefaultStreamingTelemetryDispatch;
}

void PushStreamingStatusMessageForTests(std::string message,
                                        int context,
                                        int storm_error) {
  ::openwow::data::PushStreamingStatusMessage(
      std::move(message), context, storm_error);
}

void SetStreamingStatusMessageForTests(std::string message,
                                       int context,
                                       int storm_error) {
  ClearStreamingStatusMessage();
  ::openwow::data::PushStreamingStatusMessage(
      std::move(message), context, storm_error);
}

void ResetStreamingInitStateForTests() {
  g_streaming_initialized = false;
  g_streaming_manifest_runtime_initialized = false;
  ClearStreamingStatusMessage();
  ::openwow::data::SetCurrentStreamingStatusCode(0);
  ClearStreamingManifestRuntimeState();
  openwow::vfs::InvalidateStreamingManifestLookupPathCache();
  g_tracker_url = nullptr;
  std::memset(g_streaming_locale, 0, sizeof(g_streaming_locale));
  std::memset(g_streaming_tracker_peer_id, 0, sizeof(g_streaming_tracker_peer_id));
  std::memset(g_streaming_tracker_key, 0, sizeof(g_streaming_tracker_key));
  g_streaming_build_number = 0;
  g_streaming_speed_test = 0;
  g_streaming_frame_delta_histogram = {};
  g_streaming_last_frame_tick = 0;
  g_streaming_frame_callback = nullptr;
  SetStreamingReportCountersForTests(0, 0, 0, 0);
  SetStreamingFrameTickSourceForTests(nullptr);
  SetStreamingRandomSeedSourceForTests(nullptr);
  SetStreamingRandomSeedSinkForTests(nullptr);
  SetStreamingCurrentTimeSourceForTests(nullptr);
  SetStreamingRandomValueSourceForTests(nullptr);
  SetStreamingTelemetryDispatchForTests(nullptr);
  openwow::net::SetUrlDownloadHandlerForTests({});
  openwow::net::SetUrlDownloadObserverForTests({});
  openwow::core::StreamingStorage::Instance().ResetForTests();
}

}
