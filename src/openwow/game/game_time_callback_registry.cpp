#include "openwow/game/game_time_callback_registry.h"

#include <vector>

namespace openwow::game {

std::optional<std::int32_t> GameTimeCallbackRegistry::ComputeRegistrationKey(
    const GameTimeCallbackMoment& target_time) {
  if (target_time.minute < 0 || target_time.hour < 0) {
    return std::nullopt;
  }

  return static_cast<std::int32_t>(
      static_cast<std::uint32_t>(target_time.minute) +
      static_cast<std::uint32_t>(target_time.hour) * 60u);
}

std::int32_t GameTimeCallbackRegistry::ComputeDispatchKey(
    const GameTimeCallbackMoment& current_time) {
  if (current_time.minute < 0 || current_time.hour < 0) {
    return 0;
  }

  return static_cast<std::int32_t>(
      static_cast<std::uint32_t>(current_time.minute) +
      static_cast<std::uint32_t>(current_time.hour) * 60u);
}

GameTimeCallbackRegistry::Handle GameTimeCallbackRegistry::Register(
    const GameTimeCallbackMoment& target_time,
    GameTimeCallbackFn callback,
    void* context) {
  if (callback == nullptr) {
    return kInvalidHandle;
  }

  const auto minute_key = ComputeRegistrationKey(target_time);
  if (!minute_key.has_value()) {
    return kInvalidHandle;
  }

  const Handle handle = next_handle_++;
  entries_.emplace(handle, Entry{
      .minute_key = *minute_key,
      .callback = callback,
      .context = context,
  });
  buckets_[*minute_key].push_front(handle);
  return handle;
}

bool GameTimeCallbackRegistry::Unregister(const Handle handle) {
  const auto entry_it = entries_.find(handle);
  if (entry_it == entries_.end()) {
    return false;
  }

  const auto bucket_it = buckets_.find(entry_it->second.minute_key);
  if (bucket_it != buckets_.end()) {
    bucket_it->second.remove(handle);
    if (bucket_it->second.empty()) {
      buckets_.erase(bucket_it);
    }
  }

  entries_.erase(entry_it);
  return true;
}

void GameTimeCallbackRegistry::Dispatch(
    const GameTimeCallbackMoment& current_time) {
  const auto bucket_it = buckets_.find(ComputeDispatchKey(current_time));
  if (bucket_it == buckets_.end()) {
    return;
  }

  const std::vector<Handle> snapshot(bucket_it->second.begin(),
                                     bucket_it->second.end());
  for (const Handle handle : snapshot) {
    const auto entry_it = entries_.find(handle);
    if (entry_it == entries_.end()) {
      continue;
    }

    GameTimeCallbackFn callback = entry_it->second.callback;
    void* context = entry_it->second.context;
    callback(current_time, context);
  }
}

void GameTimeCallbackRegistry::Clear() {
  entries_.clear();
  buckets_.clear();
  next_handle_ = 1;
}

bool GameTimeCallbackRegistry::IsRegistered(const Handle handle) const {
  return entries_.contains(handle);
}

std::size_t GameTimeCallbackRegistry::RegisteredCount() const {
  return entries_.size();
}

std::size_t GameTimeCallbackRegistry::BucketEntryCount(
    const std::int32_t minute_key) const {
  const auto bucket_it = buckets_.find(minute_key);
  if (bucket_it == buckets_.end()) {
    return 0;
  }

  return bucket_it->second.size();
}

}
