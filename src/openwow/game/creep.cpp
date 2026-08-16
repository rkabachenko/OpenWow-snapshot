#include "openwow/game/creep.h"

#include <mutex>
#include <tuple>
#include <utility>

namespace openwow::game {
namespace {

std::string_view RetailEventName(std::string_view name) noexcept {
  const std::size_t terminator = name.find('\0');
  return terminator == std::string_view::npos ? name
                                               : name.substr(0, terminator);
}

}

std::uint32_t Creep::Hash(std::string_view name) noexcept {
  std::uint32_t hash = 0x811c9dc5u;
  for (const unsigned char byte : name) {
    hash = (hash ^ byte) * 0x01000193u;
  }

  return name.empty() ? hash : (hash | 0x80000000u);
}

std::size_t Creep::FindSlot(std::string_view name, std::uint32_t hash) const {
  if (slots_.empty()) {
    return slots_.size();
  }
  const std::size_t mask = slots_.size() - 1;
  std::size_t index = hash & mask;
  while (slots_[index].occupied()) {
    if (slots_[index].hash == hash && slots_[index].name == name) {
      return index;
    }
    index = (index + 1) & mask;
  }
  return slots_.size();
}

std::size_t Creep::FindInsertionSlot(std::string_view name,
                                     std::uint32_t hash) const {
  const std::size_t mask = slots_.size() - 1;
  std::size_t index = hash & mask;
  while (slots_[index].occupied()) {
    if (slots_[index].hash == hash && slots_[index].name == name) {
      return index;
    }
    index = (index + 1) & mask;
  }
  return index;
}

void Creep::GrowIfNeeded() {

  if (slots_.size() - slots_.size() / 4 > count_) {
    return;
  }
  const std::size_t requested = slots_.empty() ? 8 : slots_.size() * 2;
  if (requested < slots_.size()) {
    return;
  }
  Rehash(requested);
}

void Creep::Rehash(std::size_t new_capacity) {
  std::vector<Slot> old = std::move(slots_);
  slots_.assign(new_capacity, Slot{});
  count_ = 0;
  for (auto& slot : old) {
    if (slot.occupied()) {
      InsertWithoutGrowth(std::move(slot));
    }
  }
}

void Creep::InsertWithoutGrowth(Slot slot) {
  const std::size_t index = FindInsertionSlot(slot.name, slot.hash);
  if (!slots_[index].occupied()) {
    ++count_;
  }
  slots_[index] = std::move(slot);
}

void Creep::EraseAndCloseCluster(std::size_t index) {
  const std::size_t mask = slots_.size() - 1;
  slots_[index].Clear();
  --count_;

  if (slots_.size() >= 9 && count_ < ((slots_.size() * 3) >> 3u)) {
    Rehash(slots_.size() / 2);
    return;
  }

  index = (index + 1) & mask;
  while (slots_[index].occupied()) {
    Slot displaced = std::move(slots_[index]);
    slots_[index].Clear();
    --count_;
    InsertWithoutGrowth(std::move(displaced));
    index = (index + 1) & mask;
  }
}

void Creep::RegisterEventBinding(std::string_view name,
                                 CreepEventCallbackFn callback,
                                 void* context) {
  name = RetailEventName(name);
  std::unique_lock lock(mutex_);
  GrowIfNeeded();
  const std::uint32_t hash = Hash(name);
  const std::size_t index = FindInsertionSlot(name, hash);
  if (!slots_[index].occupied()) {
    slots_[index].hash = hash;
    slots_[index].name.assign(name);
    ++count_;
  }
  slots_[index].binding = {callback, context};
}

void Creep::UnregisterEventBinding(std::string_view name,
                                   CreepEventCallbackFn callback,
                                   void* context) {
  name = RetailEventName(name);
  std::unique_lock lock(mutex_);
  const std::size_t index = FindSlot(name, Hash(name));
  if (index == slots_.size()) {
    return;
  }
  const CreepEventBinding& binding = slots_[index].binding;
  if (binding.callback == callback && binding.context == context) {
    EraseAndCloseCluster(index);
  }
}

BNetVariant Creep::DispatchEventBinding(
    std::string_view name, std::span<const BNetVariant> arguments) const {
  name = RetailEventName(name);
  CreepEventBinding binding;
  {
    std::shared_lock lock(mutex_);
    const std::size_t index = FindSlot(name, Hash(name));
    if (index == slots_.size()) {
      return {};
    }
    binding = slots_[index].binding;
  }

  return binding.callback ? binding.callback(binding.context, arguments)
                          : BNetVariant{};
}

void Creep::EnumerateEventBindings(EnumerateVisitorFn visitor,
                                   void* user_context) const {
  if (!visitor) {
    return;
  }

  std::vector<std::tuple<std::string, CreepEventCallbackFn, void*>> snapshot;
  {
    std::shared_lock lock(mutex_);
    snapshot.reserve(count_);
    for (const Slot& slot : slots_) {
      if (slot.occupied()) {
        snapshot.emplace_back(slot.name, slot.binding.callback,
                              slot.binding.context);
      }
    }
  }
  for (const auto& [name, callback, callback_context] : snapshot) {
    visitor(user_context, name, callback, callback_context);
  }
}

std::size_t Creep::GetEventBindingCount() const {
  std::shared_lock lock(mutex_);
  return count_;
}

bool Creep::HasEventBinding(std::string_view name) const {
  name = RetailEventName(name);
  std::shared_lock lock(mutex_);
  return FindSlot(name, Hash(name)) != slots_.size();
}

}
