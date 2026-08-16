#pragma once

#include "openwow/game/battlenet_api.h"

#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game {

using CreepEventCallbackFn = BNetVariant (*)(
    void* context, std::span<const BNetVariant> arguments);

struct CreepEventBinding {
  CreepEventCallbackFn callback{nullptr};
  void* context{nullptr};
};

class Creep final {
public:
  Creep() = default;

  void RegisterEventBinding(std::string_view name,
                            CreepEventCallbackFn callback,
                            void* context);

  void UnregisterEventBinding(std::string_view name,
                              CreepEventCallbackFn callback,
                              void* context);

  [[nodiscard]] BNetVariant DispatchEventBinding(
      std::string_view name,
      std::span<const BNetVariant> arguments = {}) const;

  using EnumerateVisitorFn = void (*)(void* user_context,
                                      std::string_view name,
                                      CreepEventCallbackFn callback,
                                      void* callback_context);

  void EnumerateEventBindings(EnumerateVisitorFn visitor,
                              void* user_context) const;

  [[nodiscard]] std::size_t GetEventBindingCount() const;
  [[nodiscard]] bool HasEventBinding(std::string_view name) const;

private:
  struct Slot {
    std::uint32_t hash{0};
    std::string name;
    CreepEventBinding binding{};

    [[nodiscard]] bool occupied() const noexcept { return hash != 0; }
    void Clear() {
      hash = 0;
      name.clear();
      binding = {};
    }
  };

  static std::uint32_t Hash(std::string_view name) noexcept;
  [[nodiscard]] std::size_t FindSlot(std::string_view name,
                                     std::uint32_t hash) const;
  [[nodiscard]] std::size_t FindInsertionSlot(std::string_view name,
                                              std::uint32_t hash) const;
  void GrowIfNeeded();
  void Rehash(std::size_t new_capacity);
  void InsertWithoutGrowth(Slot slot);
  void EraseAndCloseCluster(std::size_t index);

  mutable std::shared_mutex mutex_;
  std::vector<Slot> slots_;
  std::size_t count_{0};
};

}
