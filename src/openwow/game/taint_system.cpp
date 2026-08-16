
#include "openwow/game/taint_system.h"
#include "openwow/core/storm_string.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "openwow/foundation/compiler/printf_format.h"

namespace openwow::game {

namespace {

constexpr std::size_t kInitialTaintEventBucketCount = 4;
constexpr std::size_t kMaxTaintEventBucketCount = 0x2000;
constexpr std::uint32_t kTaintEventProbeDecay = 3;
constexpr std::uint32_t kTaintEventRehashProbeThreshold = 13;

[[nodiscard]] OPENWOW_PRINTF_FORMAT(2, 3) std::string FormatBounded(
    const std::size_t buffer_size, const char *format, ...) {
  std::vector<char> buffer(buffer_size, '\0');
  std::va_list args;
  va_start(args, format);
  std::vsnprintf(buffer.data(), buffer.size(), format, args);
  va_end(args);
  return std::string(buffer.data());
}

[[nodiscard]] TaintStackFrame *ResizeTaintFrameStorage(TaintStackFrame *old_frames,
                                                       std::uint32_t new_capacity,
                                                       std::uint32_t preserved_count) {
  void *resized_storage = std::realloc(old_frames, sizeof(TaintStackFrame) * new_capacity);
  if (resized_storage != nullptr) {
    return static_cast<TaintStackFrame *>(resized_storage);
  }

  void *replacement_storage = std::malloc(sizeof(TaintStackFrame) * new_capacity);
  if (old_frames != nullptr) {
    if (replacement_storage != nullptr) {
      const auto copy_count = (new_capacity < preserved_count) ? new_capacity : preserved_count;
      if (copy_count > 0) {
        std::memcpy(replacement_storage, old_frames, sizeof(TaintStackFrame) * copy_count);
      }
    }
    std::free(old_frames);
  }

  return static_cast<TaintStackFrame *>(replacement_storage);
}

[[nodiscard]] std::string FormatTaintTraceFrameLocation(const TaintTraceFrame &frame) {

  std::string function_display = frame.display_name;
  std::string source_token = frame.source_token;

  if (!frame.context_prefix.empty()) {
    if (!function_display.empty()) {
      function_display =
          FormatBounded(1024, "%s:%s", frame.context_prefix.c_str(), function_display.c_str());
    } else if (!source_token.empty() && source_token.front() == '*') {
      const char *suffix = (source_token.size() > 1) ? source_token.c_str() + 2 : "";
      function_display = FormatBounded(1024, "%s:%s", frame.context_prefix.c_str(), suffix);
      source_token.clear();
    } else {
      function_display.clear();
    }
  }

  if (!source_token.empty() && source_token.front() == '@') {
    source_token.erase(source_token.begin());
  }

  if (source_token.empty() || source_token.front() == '=') {
    if (function_display.empty()) {
      return {};
    }
    return FormatBounded(1024, "%s()", function_display.c_str());
  }

  if (!function_display.empty()) {
    return FormatBounded(1024, "%s:%d %s()", source_token.c_str(), frame.line_number,
                         function_display.c_str());
  }

  return FormatBounded(1024, "%s:%d", source_token.c_str(), frame.line_number);
}

void LogTaintLine(const std::string &line) {
  diagnostics::Log(diagnostics::LogLevel::kWarn, line);
}

[[nodiscard]] std::uint32_t HashTaintEventName(const char *variable_name) {
  return openwow::core::SStrHashCI(variable_name);
}

[[nodiscard]] bool TaintEventNamesEqualNoCase(const char *left, const char *right) {
  return left != nullptr && right != nullptr &&
         openwow::core::SStrCmpNoCase(left, right, 0x7FFFFFFFu) == 0;
}

struct MacroTaintScratchState {
  int type_code = 0;
  TaintEventRecord record{};
};

MacroTaintScratchState &GetMacroTaintScratchState() {
  static MacroTaintScratchState state;
  return state;
}

}

TaintEventRegistry &TaintEventRegistry::Get() {
  static TaintEventRegistry instance;
  return instance;
}

TaintEventRecord &TaintEventRegistry::FindOrCreateEvent(const std::string &variable_name) {
  const std::uint32_t hash = HashTaintEventName(variable_name.c_str());
  if (TaintEventRecord *existing = FindEntry(hash, variable_name.c_str())) {
    return *existing;
  }

  EnsureBucketStorage(hash);

  BucketList &bucket = BucketForHash(hash);

  TaintEventEntry &entry = bucket.emplace_front();
  entry.hash = hash;
  entry.record.variable_name = variable_name;
  ++entry_count_;
  return entry.record;
}

const TaintEventRecord *TaintEventRegistry::FindEvent(const std::string &variable_name) const {
  if (bucket_mask_ < 0 || buckets_.empty()) {
    return nullptr;
  }

  return FindEntry(HashTaintEventName(variable_name.c_str()), variable_name.c_str());
}

void TaintEventRegistry::Reset() {
  entry_count_ = 0;
  probe_counter_ = 0;
  bucket_mask_ = -1;

  buckets_ = {};
}

void TaintEventRegistry::Clear() {
  entry_count_ = 0;
  probe_counter_ = 0;

  for (BucketList &bucket : buckets_) {
    bucket.clear();
  }
}

std::size_t TaintEventRegistry::size() const {
  return entry_count_;
}

bool TaintEventRegistry::empty() const {
  return entry_count_ == 0;
}

std::size_t TaintEventRegistry::bucket_count() const {
  return buckets_.size();
}

std::int32_t TaintEventRegistry::bucket_mask() const {
  return bucket_mask_;
}

std::vector<std::string> TaintEventRegistry::SnapshotBucketTraversalForTests() const {
  std::vector<std::string> names;
  names.reserve(entry_count_);
  for (const BucketList &bucket : buckets_) {
    for (const TaintEventEntry &entry : bucket) {
      names.push_back(entry.record.variable_name);
    }
  }
  return names;
}

TaintEventRecord *TaintEventRegistry::FindEntry(const std::uint32_t hash,
                                                const char *variable_name) {
  if (bucket_mask_ < 0 || buckets_.empty()) {
    return nullptr;
  }

  for (TaintEventEntry &entry : BucketForHash(hash)) {
    if (entry.hash == hash &&
        TaintEventNamesEqualNoCase(entry.record.variable_name.c_str(), variable_name)) {
      return &entry.record;
    }
  }

  return nullptr;
}

const TaintEventRecord *TaintEventRegistry::FindEntry(const std::uint32_t hash,
                                                      const char *variable_name) const {
  if (bucket_mask_ < 0 || buckets_.empty()) {
    return nullptr;
  }

  for (const TaintEventEntry &entry : BucketForHash(hash)) {
    if (entry.hash == hash &&
        TaintEventNamesEqualNoCase(entry.record.variable_name.c_str(), variable_name)) {
      return &entry.record;
    }
  }

  return nullptr;
}

void TaintEventRegistry::EnsureBucketStorage(const std::uint32_t incoming_hash) {
  if (bucket_mask_ < 0 || buckets_.empty()) {

    InitializeBuckets(kInitialTaintEventBucketCount);
    return;
  }

  const auto bucket_index = static_cast<std::uint32_t>(bucket_mask_) & incoming_hash;
  (void)MaybeGrowAndRehash(bucket_index);
}

bool TaintEventRegistry::MaybeGrowAndRehash(const std::uint32_t bucket_index) {
  if (bucket_mask_ < 0 || bucket_index >= buckets_.size()) {
    return false;
  }

  if (static_cast<std::size_t>(bucket_mask_) >= (kMaxTaintEventBucketCount - 1)) {
    return false;
  }

  if (probe_counter_ <= kTaintEventProbeDecay) {
    probe_counter_ = 0;
  } else {
    probe_counter_ -= kTaintEventProbeDecay;
  }

  for (const TaintEventEntry &entry : buckets_[bucket_index]) {
    (void)entry;
    ++probe_counter_;
    if (probe_counter_ > kTaintEventRehashProbeThreshold) {
      probe_counter_ = 0;
      Rehash(buckets_.size() * 2u);
      return true;
    }
  }

  return false;
}

void TaintEventRegistry::InitializeBuckets(const std::size_t bucket_count) {
  buckets_.clear();
  buckets_.resize(bucket_count);
  probe_counter_ = 0;
  bucket_mask_ = static_cast<std::int32_t>(bucket_count - 1);
}

void TaintEventRegistry::Rehash(const std::size_t bucket_count) {
  std::vector<BucketList> rehashed_buckets(bucket_count);
  const std::uint32_t new_mask = static_cast<std::uint32_t>(bucket_count - 1);

  for (BucketList &bucket : buckets_) {
    for (auto it = bucket.begin(); it != bucket.end();) {
      auto current = it++;
      BucketList &target = rehashed_buckets[new_mask & current->hash];
      target.splice(target.end(), bucket, current);
    }
  }

  buckets_ = std::move(rehashed_buckets);
  bucket_mask_ = static_cast<std::int32_t>(new_mask);
}

TaintEventRegistry::BucketList &TaintEventRegistry::BucketForHash(const std::uint32_t hash) {
  return buckets_[static_cast<std::uint32_t>(bucket_mask_) & hash];
}

const TaintEventRegistry::BucketList &
TaintEventRegistry::BucketForHash(const std::uint32_t hash) const {
  return buckets_[static_cast<std::uint32_t>(bucket_mask_) & hash];
}

std::vector<TaintEventRecord> ProcessMacroTaintEvent(const TaintEventType type,
                                                     std::string variable_name,
                                                     std::string source_name,
                                                     std::vector<TaintTraceFrame> frames,
                                                     const bool taint_log_enabled) {
  auto &registry = TaintEventRegistry::Get();
  auto &scratch = GetMacroTaintScratchState();
  std::vector<TaintEventRecord> emitted_records;

  const bool blocked_event =
      type == TaintEventType::kActionBlocked || type == TaintEventType::kCombatBlocked;
  if (taint_log_enabled && blocked_event &&
      scratch.type_code == static_cast<int>(TaintEventType::kExecutionTaint) &&
      scratch.record.source_name == source_name) {
    if (const TaintEventRecord *global_taint = registry.FindEvent(scratch.record.variable_name);
        global_taint != nullptr) {
      emitted_records.push_back(*global_taint);
    }
    emitted_records.push_back(scratch.record);
  }

  TaintEventRecord *target_record = nullptr;
  if (type == TaintEventType::kGlobalTaint) {
    target_record = &registry.FindOrCreateEvent(variable_name);
  } else {
    target_record = &scratch.record;
    scratch.type_code = static_cast<int>(type);
  }

  target_record->type = type;
  target_record->variable_name = std::move(variable_name);
  target_record->source_name = std::move(source_name);
  target_record->frames = std::move(frames);

  if (!taint_log_enabled ||
      (type != TaintEventType::kGlobalTaint && type != TaintEventType::kExecutionTaint)) {
    emitted_records.push_back(*target_record);
  }

  return emitted_records;
}

int DisplayTaintTrace(const TaintEventRecord &taint_event) {
  const auto lines = BuildTaintTraceLogLines(taint_event);
  for (const auto &line : lines) {
    LogTaintLine(line);
  }
  return static_cast<int>(lines.size());
}

std::vector<std::string> BuildTaintTraceLogLines(const TaintEventRecord &taint_event) {
  std::vector<std::string> lines;

  std::string source_location = "UNKNOWN";
  if (!taint_event.frames.empty()) {
    source_location = FormatTaintTraceFrameLocation(taint_event.frames.front());
  }

  switch (taint_event.type) {
  case TaintEventType::kGlobalTaint:
    lines.push_back(FormatBounded(4096, "Global variable %s tainted by %s - %s",
                                        taint_event.variable_name.c_str(),
                                        taint_event.source_name.c_str(), source_location.c_str()));
    break;
  case TaintEventType::kExecutionTaint:
    lines.push_back(FormatBounded(4096, "Execution tainted by %s while reading %s - %s",
                                        taint_event.source_name.c_str(),
                                        taint_event.variable_name.c_str(), source_location.c_str()));
    break;
  case TaintEventType::kActionBlocked:
    lines.push_back(FormatBounded(4096, "An action was blocked because of taint from %s - %s",
                                        taint_event.source_name.c_str(),
                                        source_location.c_str()));
    break;
  case TaintEventType::kCombatBlocked:
    lines.push_back(
        FormatBounded(4096, "An action was blocked in combat because of taint from %s - %s",
                      taint_event.source_name.c_str(), source_location.c_str()));
    break;
  default:
    break;
  }

  for (std::size_t index = 1; index < taint_event.frames.size(); ++index) {
    const std::string location = FormatTaintTraceFrameLocation(taint_event.frames[index]);
    if (!location.empty()) {
      lines.push_back("    " + location);
    }
  }

  return lines;
}

int DisplayTaintTrace(const void *taint_event) {
  if (taint_event == nullptr) {
    return 0;
  }
  return DisplayTaintTrace(*static_cast<const TaintEventRecord *>(taint_event));
}

void *NearestUnitData_Realloc(void * , int ) {

  return nullptr;
}

TaintStackFrame *ResizeTaintStackFrameArray(TaintStackFrameArray &array,
                                            std::uint32_t new_capacity) {
  array.capacity = new_capacity;
  array.frames = ResizeTaintFrameStorage(array.frames, new_capacity, array.count);
  return array.frames;
}

void *TaintStackFrame_Realloc(void *array_header, int new_capacity) {
  auto &array = *static_cast<TaintStackFrameArray *>(array_header);
  return ResizeTaintStackFrameArray(array, static_cast<std::uint32_t>(new_capacity));
}

void ResetWorldTaintEventRegistry() {
  GetMacroTaintScratchState().type_code = 0;
  TaintEventRegistry::Get().Clear();
}

}
