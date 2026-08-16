#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace openwow::game {

class ItemDefinitions;
class CharacterMapRuntime;
class InteractionSender;
class PlayerInventoryReplica;
class QueryCache;
class SessionHandler;
class ItemUseRequirementSources;
namespace actions::held_cursor {
class HeldCursor;
}

class InventoryCommands {
 public:
  using ProtectionGate = std::function<bool()>;

  struct Changes {
    std::vector<std::uint64_t> mouseover_items;

    std::vector<std::uint32_t> bind_confirmations;

    std::vector<std::uint32_t> swap_bind_confirmations;
  };

  struct ItemPlacementSource {
    std::uint64_t item_guid = 0;
    std::uint64_t source_container_guid = 0;
    std::uint32_t source_slot = 0;
    std::uint32_t item_entry = 0;
  };

  InventoryCommands(
      PlayerInventoryReplica& inventory, ItemDefinitions& item_definitions,
      CharacterMapRuntime& map_runtime, InteractionSender& interaction,
      QueryCache& query_cache, const ItemUseRequirementSources& requirements,
      const SessionHandler& session)
      : inventory_(inventory),
        item_definitions_(item_definitions),
        map_runtime_(map_runtime),
        interaction_(interaction),
        query_cache_(query_cache),
        requirements_(requirements),
        session_(session) {}
  ~InventoryCommands();

  void SetProtectionGate(ProtectionGate gate);
  void BindHeldCursor(actions::held_cursor::HeldCursor* cursor) noexcept;
  void PlaceHeldItem(bool skip_bind_check);

  [[nodiscard]] bool PlaceHeldItemInSlot(std::uint32_t dest_slot);

  void RequestAutoEquip(ItemPlacementSource source);

  void RequestEquipInSlot(ItemPlacementSource source, std::uint32_t dest_slot);
  void ResolvePending(std::uint32_t index, bool accepted);
  [[nodiscard]] Changes TakeChanges() { return std::exchange(changes_, {}); }
  void Reset();

 private:

  enum class PendingKind { kAutoEquip, kSwapToSlot, kEquipItemSlot };

  struct PendingEquip {
    PendingKind kind = PendingKind::kAutoEquip;
    std::uint64_t item_guid = 0;
    std::uint64_t source_container_guid = 0;
    std::uint32_t source_slot = 0;
    std::uint32_t item_entry = 0;
    std::uint64_t inventory_revision = 0;
    std::uint32_t dest_slot = 0;
  };

  struct AsyncLifetime {
    std::atomic<std::uint64_t> generation{1};
  };

  [[nodiscard]] bool CanMutate() const;
  [[nodiscard]] std::uint32_t StorePending(PendingEquip pending);
  void SendAutoEquip(const PendingEquip& pending);
  void SendSwapToSlot(const PendingEquip& pending);
  void SendEquipItemToSlot(const PendingEquip& pending);

  [[nodiscard]] bool DispatchOrDefer(const ItemPlacementSource& source,
                                     PendingKind kind,
                                     std::uint32_t dest_slot);

  PlayerInventoryReplica& inventory_;
  ItemDefinitions& item_definitions_;
  CharacterMapRuntime& map_runtime_;
  InteractionSender& interaction_;
  QueryCache& query_cache_;
  const ItemUseRequirementSources& requirements_;
  const SessionHandler& session_;
  actions::held_cursor::HeldCursor* held_cursor_ = nullptr;
  std::shared_ptr<AsyncLifetime> async_lifetime_ =
      std::make_shared<AsyncLifetime>();
  std::vector<PendingEquip> pending_;
  ProtectionGate protection_gate_;
  Changes changes_;
};

}
