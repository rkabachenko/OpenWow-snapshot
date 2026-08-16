#include "openwow/debug/inspection/inspector_ui_models.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>

namespace openwow::debug {

float InspectorRangeSliderModel::RetailClamp(const float value,
                                             const float lower,
                                             const float upper) noexcept {

  const float no_more_than_upper = value <= upper ? value : upper;
  return lower <= no_more_than_upper ? no_more_than_upper : lower;
}

InspectorRangeDragEffect InspectorRangeSliderModel::PointerDown(
    const float pointer_x, const float pointer_y, const float bounds_width,
    const float bounds_height) noexcept {

  if (pointer_y <= bounds_height - 17.0F) {
    tracking_thumb_ = InspectorRangeThumb::kLower;
  } else if (pointer_y >= bounds_height - 11.0F) {
    tracking_thumb_ = InspectorRangeThumb::kUpper;
  } else {
    tracking_thumb_ = InspectorRangeThumb::kNone;
  }

  return PointerDragged(pointer_x, bounds_width);
}

InspectorRangeDragEffect InspectorRangeSliderModel::PointerDragged(
    const float pointer_x, const float bounds_width) noexcept {
  if (tracking_thumb_ == InspectorRangeThumb::kNone) {
    return {};
  }

  const float normalized = pointer_x / bounds_width;
  if (tracking_thumb_ == InspectorRangeThumb::kLower) {
    lower_value_ = RetailClamp(normalized, min_value_, upper_value_);
  } else {
    upper_value_ = RetailClamp(normalized, lower_value_, max_value_);
  }

  return {.thumb = tracking_thumb_,
          .dispatch_action = true,
          .request_redraw = true};
}

InspectorRangeDrawLayout InspectorRangeSliderModel::DrawLayout(
    const InspectorRect bounds) const {
  constexpr float kInset = 6.0F;
  constexpr float kTrackHeight = 6.0F;
  constexpr float kTrackBottomInset = 17.0F;

  const float track_width = bounds.width - 12.0F;
  const float track_y = bounds.height - kTrackBottomInset;
  InspectorRangeDrawLayout result{
      .track = {bounds.x + kInset, track_y, track_width, kTrackHeight},
      .label_center_x = bounds.x + bounds.width * 0.5F,
      .label_baseline_y = track_y - 26.0F,
  };

  if (!(min_value_ < max_value_ && min_value_ <= lower_value_ &&
        lower_value_ <= upper_value_ && upper_value_ <= max_value_)) {
    return result;
  }

  const float value_span = max_value_ - min_value_;

  const float lower_width =
      (lower_value_ - min_value_) * track_width / value_span;
  const float selected_x = lower_value_ * track_width / value_span + kInset;
  const float selected_width =
      (upper_value_ - lower_value_) * track_width / value_span;
  const float upper_x = upper_value_ * track_width / value_span + kInset;
  const float upper_width =
      (max_value_ - upper_value_) * track_width / value_span;

  result.lower_range =
      InspectorRect{kInset, track_y, lower_width, kTrackHeight};
  result.selected_range =
      InspectorRect{selected_x, track_y, selected_width, kTrackHeight};
  result.upper_range =
      InspectorRect{upper_x, track_y, upper_width, kTrackHeight};

  std::array<char, 96> label{};
  const int size = std::snprintf(label.data(), label.size(), "%.4f : %.4f",
                                 static_cast<double>(lower_value_),
                                 static_cast<double>(upper_value_));
  if (size > 0) {
    const auto bounded = static_cast<std::size_t>(size) < label.size()
                             ? static_cast<std::size_t>(size)
                             : label.size() - 1U;
    result.label.assign(label.data(), bounded);
  }
  return result;
}

InspectorScrollSyncTransition
InspectorVerticalScrollSyncModel::SetSynchronizedSource(
    const std::optional<InspectorScrollSource> source) noexcept {
  InspectorScrollSyncTransition transition{.unregister_source = source_,
                                           .register_source = source};
  source_ = source;
  return transition;
}

InspectorScrollSyncTransition
InspectorVerticalScrollSyncModel::StopSynchronizing() noexcept {
  InspectorScrollSyncTransition transition{.unregister_source = source_};
  source_.reset();
  return transition;
}

std::optional<InspectorScrollSyncEffect>
InspectorVerticalScrollSyncModel::SourceVisibleRectChanged(
    const InspectorScrollSource source,
    const InspectorRect source_visible_rect) const noexcept {
  if (!source_.has_value() || *source_ != source) {
    return std::nullopt;
  }

  return InspectorScrollSyncEffect{
      .target_document_rect = {0.0F, source_visible_rect.y, 1.0F,
                               source_visible_rect.height},
      .reflect_target_clip_view = true,
  };
}

namespace {

constexpr std::size_t DomainIndex(const InspectorDomain domain) noexcept {
  return static_cast<std::size_t>(domain);
}

bool IsValidDomain(const InspectorDomain domain) noexcept {
  return DomainIndex(domain) < DomainIndex(InspectorDomain::kCount);
}

InspectorDomain ValueDomain(const InspectorRecordValue& value) noexcept {
  return std::visit(
      [](const auto& snapshot) {
        using Snapshot = std::decay_t<decltype(snapshot)>;
        if constexpr (std::is_same_v<Snapshot, InspectorObjectSnapshot>) {
          return InspectorDomain::kObjects;
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorResourceSnapshot>) {
          return InspectorDomain::kResources;
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorDbcRecordSnapshot>) {
          return InspectorDomain::kDbc;
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorPacketSnapshot>) {
          return InspectorDomain::kPackets;
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorRenderSnapshot>) {
          return InspectorDomain::kRender;
        } else {
          return InspectorDomain::kCapture;
        }
      },
      value);
}

bool IdentityLess(const InspectorRecordIdentity& lhs,
                  const InspectorRecordIdentity& rhs) noexcept {
  return std::tie(lhs.domain, lhs.value, lhs.generation) <
         std::tie(rhs.domain, rhs.value, rhs.generation);
}

bool ContainsCaseInsensitive(const std::string_view text,
                             const std::string_view query) {
  return std::search(text.begin(), text.end(), query.begin(), query.end(),
                     [](const char lhs, const char rhs) {
                       return std::tolower(static_cast<unsigned char>(lhs)) ==
                              std::tolower(static_cast<unsigned char>(rhs));
                     }) != text.end();
}

std::string_view RecordType(const InspectorRecordValue& value) noexcept {
  return std::visit(
      [](const auto& snapshot) -> std::string_view {
        using Snapshot = std::decay_t<decltype(snapshot)>;
        if constexpr (std::is_same_v<Snapshot, InspectorObjectSnapshot>) {
          return snapshot.type;
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorResourceSnapshot>) {
          return snapshot.kind;
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorDbcRecordSnapshot>) {
          return "DBC";
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorPacketSnapshot>) {
          return snapshot.direction == InspectorPacketDirection::kClientToServer
                     ? "C->S"
                     : "S->C";
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorRenderSnapshot>) {
          return snapshot.backend;
        } else {
          return snapshot.primitive;
        }
      },
      value);
}

std::string_view RecordName(const InspectorRecordValue& value) noexcept {
  return std::visit(
      [](const auto& snapshot) -> std::string_view {
        using Snapshot = std::decay_t<decltype(snapshot)>;
        if constexpr (std::is_same_v<Snapshot, InspectorObjectSnapshot> ||
                      std::is_same_v<Snapshot, InspectorResourceSnapshot> ||
                      std::is_same_v<Snapshot, InspectorPacketSnapshot>) {
          return snapshot.name;
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorDbcRecordSnapshot>) {
          return snapshot.path;
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorRenderSnapshot>) {
          return snapshot.status;
        } else {
          return snapshot.label;
        }
      },
      value);
}

std::uint64_t RecordCount(const InspectorRecordValue& value) noexcept {
  return std::visit(
      [](const auto& snapshot) -> std::uint64_t {
        using Snapshot = std::decay_t<decltype(snapshot)>;
        if constexpr (std::is_same_v<Snapshot, InspectorResourceSnapshot>) {
          return snapshot.count.value_or(0);
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorDbcRecordSnapshot>) {
          return snapshot.fields.size();
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorRenderSnapshot>) {
          return snapshot.draw_calls;
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorCaptureSnapshot>) {
          return snapshot.resource_count;
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorObjectSnapshot>) {
          return snapshot.health;
        } else {
          return 1;
        }
      },
      value);
}

std::uint64_t RecordBytes(const InspectorRecordValue& value) noexcept {
  return std::visit(
      [](const auto& snapshot) -> std::uint64_t {
        using Snapshot = std::decay_t<decltype(snapshot)>;
        if constexpr (std::is_same_v<Snapshot, InspectorResourceSnapshot>) {
          return snapshot.bytes.value_or(0);
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorPacketSnapshot>) {
          return snapshot.payload_bytes;
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorRenderSnapshot>) {
          return snapshot.gpu_bytes;
        } else {
          return 0;
        }
      },
      value);
}

std::uint64_t RecordDuration(const InspectorRecordValue& value) noexcept {
  return std::visit(
      [](const auto& snapshot) -> std::uint64_t {
        using Snapshot = std::decay_t<decltype(snapshot)>;
        if constexpr (std::is_same_v<Snapshot, InspectorPacketSnapshot>) {
          return snapshot.timestamp_nanoseconds;
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorRenderSnapshot>) {
          return snapshot.gpu_duration_nanoseconds;
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorCaptureSnapshot>) {
          return snapshot.gpu_duration_nanoseconds;
        } else {
          return 0;
        }
      },
      value);
}

std::size_t RecordBytesUsed(const InspectorRecord& record) noexcept {
  std::size_t bytes = sizeof(record);
  std::visit(
      [&bytes](const auto& snapshot) {
        using Snapshot = std::decay_t<decltype(snapshot)>;
        if constexpr (std::is_same_v<Snapshot, InspectorObjectSnapshot>) {
          bytes += snapshot.type.size() + snapshot.name.size();
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorResourceSnapshot>) {
          bytes += snapshot.kind.size() + snapshot.name.size() +
                   snapshot.state.size() + snapshot.status.size();
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorDbcRecordSnapshot>) {
          bytes += snapshot.path.size() +
                   snapshot.fields.size() * sizeof(InspectorDbcFieldSnapshot);
          for (const auto& field : snapshot.fields) {
            if (const auto* text = std::get_if<std::string>(&field.value)) {
              bytes += text->size();
            }
          }
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorPacketSnapshot>) {
          bytes += snapshot.name.size();
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorRenderSnapshot>) {
          bytes += snapshot.backend.size() + snapshot.status.size();
        } else {
          bytes += snapshot.label.size() + snapshot.primitive.size();
        }
      },
      record.value);
  return bytes;
}

bool RecordMatches(const InspectorRecord& record,
                   const InspectorFilter& filter) {
  if (filter.domain && record.identity.domain != *filter.domain) {
    return false;
  }
  if (!filter.include_stale && record.stale) {
    return false;
  }
  if (filter.query.empty()) {
    return true;
  }
  const bool value_matches = std::visit(
      [&filter](const auto& snapshot) {
        using Snapshot = std::decay_t<decltype(snapshot)>;
        if constexpr (std::is_same_v<Snapshot, InspectorObjectSnapshot>) {
          return ContainsCaseInsensitive(snapshot.type, filter.query) ||
                 ContainsCaseInsensitive(snapshot.name, filter.query);
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorResourceSnapshot>) {
          return ContainsCaseInsensitive(snapshot.kind, filter.query) ||
                 ContainsCaseInsensitive(snapshot.name, filter.query) ||
                 ContainsCaseInsensitive(snapshot.state, filter.query) ||
                 ContainsCaseInsensitive(snapshot.status, filter.query);
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorDbcRecordSnapshot>) {
          if (ContainsCaseInsensitive(snapshot.path, filter.query)) {
            return true;
          }
          return std::any_of(
              snapshot.fields.begin(), snapshot.fields.end(),
              [&filter](const InspectorDbcFieldSnapshot& field) {
                const auto* text = std::get_if<std::string>(&field.value);
                return text != nullptr &&
                       ContainsCaseInsensitive(*text, filter.query);
              });
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorPacketSnapshot>) {
          return ContainsCaseInsensitive(snapshot.name, filter.query);
        } else if constexpr (std::is_same_v<Snapshot,
                                            InspectorRenderSnapshot>) {
          return ContainsCaseInsensitive(snapshot.backend, filter.query) ||
                 ContainsCaseInsensitive(snapshot.status, filter.query);
        } else {
          return ContainsCaseInsensitive(snapshot.label, filter.query) ||
                 ContainsCaseInsensitive(snapshot.primitive, filter.query);
        }
      },
      record.value);
  if (value_matches) {
    return true;
  }
  const std::string identity = std::to_string(record.identity.value);
  return ContainsCaseInsensitive(identity, filter.query);
}

int CompareRecords(const InspectorRecord& lhs, const InspectorRecord& rhs,
                   const InspectorSortKey key) noexcept {
  switch (key) {
    case InspectorSortKey::kIdentity:
      return (lhs.identity.value > rhs.identity.value) -
             (lhs.identity.value < rhs.identity.value);
    case InspectorSortKey::kGeneration:
      return (lhs.identity.generation > rhs.identity.generation) -
             (lhs.identity.generation < rhs.identity.generation);
    case InspectorSortKey::kType:
      return RecordType(lhs.value).compare(RecordType(rhs.value));
    case InspectorSortKey::kName:
      return RecordName(lhs.value).compare(RecordName(rhs.value));
    case InspectorSortKey::kCount:
      return (RecordCount(lhs.value) > RecordCount(rhs.value)) -
             (RecordCount(lhs.value) < RecordCount(rhs.value));
    case InspectorSortKey::kBytes:
      return (RecordBytes(lhs.value) > RecordBytes(rhs.value)) -
             (RecordBytes(lhs.value) < RecordBytes(rhs.value));
    case InspectorSortKey::kDuration:
      return (RecordDuration(lhs.value) > RecordDuration(rhs.value)) -
             (RecordDuration(lhs.value) < RecordDuration(rhs.value));
  }
  return 0;
}

std::string FormatScaled(const std::uint64_t value, const double divisor,
                         const char* unit) {
  std::array<char, 64> text{};
  const int size = std::snprintf(text.data(), text.size(), "%.2f %s",
                                 static_cast<double>(value) / divisor, unit);
  return size > 0 ? std::string(text.data()) : std::string{};
}

}

InspectorUiModel::InspectorUiModel(const InspectorStorageLimits limits)
    : limits_(limits),
      providers_(DomainIndex(InspectorDomain::kCount)),
      generations_(DomainIndex(InspectorDomain::kCount), 0) {}

void InspectorUiModel::SetProvider(const InspectorDomain domain,
                                   InspectorSnapshotProvider provider) {
  if (IsValidDomain(domain)) {
    providers_[DomainIndex(domain)] = std::move(provider);
  }
}

InspectorPublishResult InspectorUiModel::Refresh(const InspectorDomain domain) {
  if (!IsValidDomain(domain) || !providers_[DomainIndex(domain)]) {
    return {.status = InspectorPublishStatus::kUnavailable};
  }
  auto snapshot = providers_[DomainIndex(domain)]();
  if (!snapshot) {
    return {.status = InspectorPublishStatus::kUnavailable};
  }
  if (snapshot->domain != domain) {
    return {.status = InspectorPublishStatus::kInvalidSnapshot};
  }
  return Publish(std::move(*snapshot));
}

InspectorPublishResult InspectorUiModel::Publish(InspectorSnapshot snapshot) {
  if (!IsValidDomain(snapshot.domain)) {
    return {.status = InspectorPublishStatus::kInvalidSnapshot};
  }
  const std::size_t domain_index = DomainIndex(snapshot.domain);
  if (snapshot.generation < generations_[domain_index]) {
    return {.status = InspectorPublishStatus::kStaleGeneration};
  }

  std::vector<InspectorRecordIdentity> identities;
  identities.reserve(snapshot.records.size());
  for (const auto& record : snapshot.records) {
    if (record.identity.domain != snapshot.domain ||
        record.identity.generation != snapshot.generation ||
        ValueDomain(record.value) != snapshot.domain) {
      return {.status = InspectorPublishStatus::kInvalidSnapshot};
    }
    identities.push_back(record.identity);
  }
  std::sort(identities.begin(), identities.end(), IdentityLess);
  if (std::adjacent_find(identities.begin(), identities.end()) !=
      identities.end()) {
    return {.status = InspectorPublishStatus::kInvalidSnapshot};
  }

  for (auto& stored : records_) {
    if (stored.record.identity.domain == snapshot.domain) {
      stored.record.stale = true;
    }
  }

  InspectorPublishResult result;
  for (auto& record : snapshot.records) {
    if (RecordBytesUsed(record) > limits_.max_record_bytes ||
        result.accepted_records >= limits_.max_records) {
      ++result.dropped_records;
      continue;
    }
    const auto existing = std::find_if(
        records_.begin(), records_.end(), [&record](const StoredRecord& stored) {
          return stored.record.identity == record.identity;
        });
    if (existing != records_.end()) {
      records_.erase(existing);
    }
    record.stale = false;
    records_.push_back({.record = std::move(record),
                        .ordinal = next_ordinal_++});
    ++result.accepted_records;
  }
  generations_[domain_index] = snapshot.generation;
  result.evicted_records = EnforceLimits();
  return result;
}

void InspectorUiModel::SetFilter(InspectorFilter filter) {
  filter_ = std::move(filter);
}

void InspectorUiModel::SetSort(const InspectorSort sort) noexcept {
  sort_ = sort;
}

std::vector<InspectorRecord> InspectorUiModel::Records() const {
  struct VisibleRecord {
    InspectorRecord record;
    std::uint64_t ordinal;
  };
  std::vector<VisibleRecord> visible;
  visible.reserve(records_.size());
  for (const auto& stored : records_) {
    if (RecordMatches(stored.record, filter_)) {
      visible.push_back({stored.record, stored.ordinal});
    }
  }
  std::stable_sort(
      visible.begin(), visible.end(), [this](const VisibleRecord& lhs,
                                             const VisibleRecord& rhs) {
        const int comparison = CompareRecords(lhs.record, rhs.record, sort_.key);
        if (comparison != 0) {
          return sort_.direction == InspectorSortDirection::kAscending
                     ? comparison < 0
                     : comparison > 0;
        }
        return lhs.ordinal < rhs.ordinal;
      });
  std::vector<InspectorRecord> result;
  result.reserve(visible.size());
  for (auto& record : visible) {
    result.push_back(std::move(record.record));
  }
  return result;
}

std::optional<InspectorRecord> InspectorUiModel::Find(
    const InspectorRecordIdentity identity) const {
  const auto found = std::find_if(
      records_.begin(), records_.end(), [identity](const StoredRecord& stored) {
        return stored.record.identity == identity;
      });
  return found == records_.end()
             ? std::nullopt
             : std::optional<InspectorRecord>(found->record);
}

bool InspectorUiModel::Select(const InspectorRecordIdentity identity) {
  if (!Find(identity)) {
    return false;
  }
  selection_ = identity;
  return true;
}

void InspectorUiModel::ClearSelection() noexcept {
  selection_.reset();
}

std::optional<InspectorRecord> InspectorUiModel::Selected() const {
  return selection_ ? Find(*selection_) : std::nullopt;
}

std::uint64_t InspectorUiModel::generation(
    const InspectorDomain domain) const noexcept {
  return IsValidDomain(domain) ? generations_[DomainIndex(domain)] : 0;
}

std::size_t InspectorUiModel::EnforceLimits() {
  std::size_t evicted = 0;
  const auto erase_oldest = [this, &evicted](const bool stale_only,
                                              const bool keep_selection) {
    auto found = records_.end();
    for (auto candidate = records_.begin(); candidate != records_.end();
         ++candidate) {
      if (stale_only && !candidate->record.stale) {
        continue;
      }
      if (keep_selection && selection_ &&
          candidate->record.identity == *selection_) {
        continue;
      }
      if (found == records_.end() || candidate->ordinal < found->ordinal) {
        found = candidate;
      }
    }
    if (found == records_.end()) {
      return false;
    }
    if (selection_ && found->record.identity == *selection_) {
      selection_.reset();
    }
    records_.erase(found);
    ++evicted;
    return true;
  };

  auto stale_count = [this] {
    return static_cast<std::size_t>(std::count_if(
        records_.begin(), records_.end(),
        [](const StoredRecord& stored) { return stored.record.stale; }));
  };
  while (stale_count() > limits_.max_stale_records &&
         (erase_oldest(true, true) || erase_oldest(true, false))) {
  }
  while (records_.size() > limits_.max_records) {
    if (!erase_oldest(true, true) && !erase_oldest(false, true) &&
        !erase_oldest(true, false) && !erase_oldest(false, false)) {
      break;
    }
  }
  return evicted;
}

std::string FormatInspectorBytes(const std::uint64_t bytes) {
  constexpr std::uint64_t kKibibyte = 1024;
  constexpr std::uint64_t kMebibyte = kKibibyte * 1024;
  constexpr std::uint64_t kGibibyte = kMebibyte * 1024;
  if (bytes >= kGibibyte) {
    return FormatScaled(bytes, static_cast<double>(kGibibyte), "GiB");
  }
  if (bytes >= kMebibyte) {
    return FormatScaled(bytes, static_cast<double>(kMebibyte), "MiB");
  }
  if (bytes >= kKibibyte) {
    return FormatScaled(bytes, static_cast<double>(kKibibyte), "KiB");
  }
  return std::to_string(bytes) + " B";
}

std::string FormatInspectorDuration(const std::uint64_t nanoseconds) {
  constexpr std::uint64_t kMicrosecond = 1'000;
  constexpr std::uint64_t kMillisecond = 1'000'000;
  constexpr std::uint64_t kSecond = 1'000'000'000;
  if (nanoseconds >= kSecond) {
    return FormatScaled(nanoseconds, static_cast<double>(kSecond), "s");
  }
  if (nanoseconds >= kMillisecond) {
    return FormatScaled(nanoseconds, static_cast<double>(kMillisecond), "ms");
  }
  if (nanoseconds >= kMicrosecond) {
    return FormatScaled(nanoseconds, static_cast<double>(kMicrosecond), "us");
  }
  return std::to_string(nanoseconds) + " ns";
}

std::string FormatInspectorCount(const std::uint64_t count) {
  std::string result = std::to_string(count);
  for (std::ptrdiff_t offset = static_cast<std::ptrdiff_t>(result.size()) - 3;
       offset > 0; offset -= 3) {
    result.insert(static_cast<std::size_t>(offset), 1, ',');
  }
  return result;
}

}
