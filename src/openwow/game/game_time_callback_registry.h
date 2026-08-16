#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <unordered_map>

namespace openwow::game {

struct GameTimeCallbackMoment {
  std::int32_t minute = 0;
  std::int32_t hour = 0;
  std::int32_t weekday = 0;
  std::int32_t day = 0;
  std::int32_t month = 0;
  std::int32_t year = 0;
  std::int32_t top_bits = 0;
};

using GameTimeCallbackFn = bool (*)(const GameTimeCallbackMoment& current_time,
                                    void* context);

class GameTimeCallbackRegistry {
 public:
  using Handle = std::uint64_t;
  static constexpr Handle kInvalidHandle = 0;

  [[nodiscard]] Handle Register(const GameTimeCallbackMoment& target_time,
                                GameTimeCallbackFn callback,
                                void* context);
  bool Unregister(Handle handle);

  void Dispatch(const GameTimeCallbackMoment& current_time);
  void Clear();

  [[nodiscard]] bool IsRegistered(Handle handle) const;
  [[nodiscard]] std::size_t RegisteredCount() const;
  [[nodiscard]] std::size_t BucketEntryCount(std::int32_t minute_key) const;

 private:
  struct Entry {
    std::int32_t minute_key = 0;
    GameTimeCallbackFn callback = nullptr;
    void* context = nullptr;
  };

  [[nodiscard]] static std::optional<std::int32_t> ComputeRegistrationKey(
      const GameTimeCallbackMoment& target_time);
  [[nodiscard]] static std::int32_t ComputeDispatchKey(
      const GameTimeCallbackMoment& current_time);

  Handle next_handle_ = 1;
  std::unordered_map<Handle, Entry> entries_;
  std::unordered_map<std::int32_t, std::list<Handle>> buckets_;
};

}
