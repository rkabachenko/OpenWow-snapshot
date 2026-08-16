
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <vector>

namespace openwow::game {

enum class TaintEventType : int {
  kGlobalTaint = 1,
  kExecutionTaint = 2,
  kActionBlocked = 3,
  kCombatBlocked = 4,
};

struct TaintStackFrame {
  std::uint8_t bytes[104];
};
static_assert(sizeof(TaintStackFrame) == 104);

struct TaintTraceFrame {
  std::string display_name;
  std::string source_token;
  std::int32_t line_number = 0;
  std::string context_prefix;
};

struct TaintStackFrameArray {
  std::uint32_t capacity = 0;
  std::uint32_t count = 0;
  TaintStackFrame *frames = nullptr;
  std::uint32_t growth_quantum = 0;
};
static_assert(sizeof(TaintStackFrameArray) == 16 || sizeof(TaintStackFrameArray) == 24, "TaintStackFrameArray: expected 16 (32-bit) or 24 (64-bit)");

struct TaintEventRecord {
  TaintEventType type = TaintEventType::kGlobalTaint;
  std::string variable_name;
  std::string source_name;
  std::vector<TaintTraceFrame> frames;
};

class TaintEventRegistry {
public:
  static TaintEventRegistry &Get();

  TaintEventRecord &FindOrCreateEvent(const std::string &variable_name);
  [[nodiscard]] const TaintEventRecord *FindEvent(const std::string &variable_name) const;

  void Reset();
  void Clear();
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] bool empty() const;
  [[nodiscard]] std::size_t bucket_count() const;
  [[nodiscard]] std::int32_t bucket_mask() const;
  [[nodiscard]] std::vector<std::string> SnapshotBucketTraversalForTests() const;

private:
  struct TaintEventEntry {
    std::uint32_t hash = 0;
    TaintEventRecord record{};
  };

  using BucketList = std::list<TaintEventEntry>;

  [[nodiscard]] TaintEventRecord *FindEntry(std::uint32_t hash, const char *variable_name);
  [[nodiscard]] const TaintEventRecord *FindEntry(std::uint32_t hash,
                                                  const char *variable_name) const;
  void EnsureBucketStorage(std::uint32_t incoming_hash);
  [[nodiscard]] bool MaybeGrowAndRehash(std::uint32_t bucket_index);
  void InitializeBuckets(std::size_t bucket_count);
  void Rehash(std::size_t bucket_count);
  [[nodiscard]] BucketList &BucketForHash(std::uint32_t hash);
  [[nodiscard]] const BucketList &BucketForHash(std::uint32_t hash) const;

  std::vector<BucketList> buckets_;
  std::size_t entry_count_ = 0;
  std::uint32_t probe_counter_ = 0;
  std::int32_t bucket_mask_ = -1;
};

std::vector<TaintEventRecord> ProcessMacroTaintEvent(TaintEventType type,
                                                     std::string variable_name,
                                                     std::string source_name,
                                                     std::vector<TaintTraceFrame> frames,
                                                     bool taint_log_enabled);

std::vector<std::string> BuildTaintTraceLogLines(const TaintEventRecord &taint_event);
int DisplayTaintTrace(const TaintEventRecord &taint_event);

int DisplayTaintTrace(const void *taint_event);

void *NearestUnitData_Realloc(void *array_header, int new_capacity);

[[nodiscard]] TaintStackFrame *ResizeTaintStackFrameArray(TaintStackFrameArray &array,
                                                          std::uint32_t new_capacity);

void *TaintStackFrame_Realloc(void *array_header, int new_capacity);

void ResetWorldTaintEventRegistry();

}
