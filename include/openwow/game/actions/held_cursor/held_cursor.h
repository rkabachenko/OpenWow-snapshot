#pragma once

#include "openwow/game/inventory/model/item_instance.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace openwow::game::actions::held_cursor {

enum class Grid : std::uint8_t {
  None,
  ActionBar,
  PetBar,
};

enum class Sound : std::uint8_t {
  None,
  CursorGrabObject,
  CursorDropObject,
  Coins,
  SpellbookPickup,
  SpellbookDrop,
};

enum class TextureMode : std::uint8_t {
  None,
  HeldTexture,
  CopiedPath,
  DirectPath,
};

struct Presentation {
  std::string texture_path;
  TextureMode texture_mode{TextureMode::DirectPath};
  Sound sound{Sound::None};
  Grid grid{Grid::None};
};

struct Empty {};

struct LiveItem {
  ItemInstance item;
  std::uint64_t source_container_guid{0};
  std::uint8_t source_bag{0};
  std::uint8_t source_slot{0};
  std::uint32_t auxiliary_value{0};
  Grid grid{Grid::None};
};

struct PlayerMoney {
  std::uint32_t amount{0};
};

struct Spell {
  std::uint32_t spell_id{0};
  std::uint32_t spellbook_slot{0};
  bool from_pet_spellbook{false};
  Grid grid{Grid::None};
};

struct PetAction {
  std::uint32_t packed_action{0};
  std::uint32_t source_slot{0};
  Grid grid{Grid::None};
};

struct MerchantItem {
  std::uint32_t item_entry{0};
  std::uint32_t display_id{0};
  std::uint32_t zero_based_slot{0};
  std::uint32_t auxiliary_value{0};
  Grid grid{Grid::None};
};

struct ActionBarItem {
  std::uint32_t item_entry{0};
  std::uint32_t display_id{0};
  std::uint32_t payload{0};
  std::uint32_t auxiliary_value{0};
  Grid grid{Grid::None};
};

struct Macro {
  std::uint32_t stable_id{0};
  Grid grid{Grid::None};
};

struct AmmoItem {
  std::uint32_t item_entry{0};
  std::uint32_t count{0};
  Grid grid{Grid::None};
};

struct StablePet {
  int stable_index{0};
  Grid grid{Grid::None};
};

struct GuildBankItem {
  std::uint32_t item_entry{0};
  std::uint32_t display_id{0};
  std::uint32_t linear_slot{0};
  std::uint32_t split_count{0};
  Grid grid{Grid::None};
};

struct GuildBankMoney {
  std::uint32_t amount{0};
};

struct EquipmentSet {
  std::uint32_t stable_id{0};
  Grid grid{Grid::None};
};

using Payload = std::variant<
    Empty, LiveItem, PlayerMoney, Spell, PetAction, MerchantItem,
    ActionBarItem, Macro, AmmoItem, StablePet, GuildBankItem, GuildBankMoney,
    EquipmentSet>;

enum class Kind : std::uint8_t {
  None,
  LiveItem,
  PlayerMoney,
  Spell,
  PetAction,
  MerchantItem,
  ActionBarItem,
  Macro,
  AmmoItem,
  StablePet,
  GuildBankItem,
  GuildBankMoney,
  EquipmentSet,
};

struct ClearOptions {
  bool release_source_lease{true};
  bool publish_money_owner_update{true};
};

class SourcePort {
 public:
  virtual ~SourcePort() = default;

  virtual void CancelItemTargeting() = 0;
  virtual void AcquireLiveItemLease(const LiveItem& item) = 0;
  virtual void ReleaseLiveItemLease(const LiveItem& item) = 0;
  virtual void UnlockGuildBankItem(const GuildBankItem& item) = 0;
  virtual void PublishGuildBankMoneyOwnerUpdate() = 0;
};

class PresentationPort {
 public:
  virtual ~PresentationPort() = default;

  virtual void ClearHeldOverlay() = 0;
  virtual void PresentHeldOverlay(std::string texture_path,
                                  TextureMode mode) = 0;
  virtual void PlaySound(Sound sound) = 0;
  virtual void ShowGrid(Grid grid) = 0;
  virtual void HideGrid(Grid grid) = 0;
  virtual void PublishCursorUpdate() = 0;
};

class HeldCursor final {
 public:
  HeldCursor(SourcePort& source, PresentationPort& presentation)
      : source_(source), presentation_(presentation) {}

  HeldCursor(const HeldCursor&) = delete;
  HeldCursor& operator=(const HeldCursor&) = delete;

  void Clear(ClearOptions options = {});

  void HoldLiveItem(LiveItem item, Presentation presentation);
  void HoldPlayerMoney(std::uint32_t amount, Presentation presentation);
  void HoldSpell(Spell spell, Presentation presentation);
  void HoldPetAction(PetAction action, Presentation presentation);
  void HoldMerchantItem(MerchantItem item, Presentation presentation);
  void HoldActionBarItem(ActionBarItem item, Presentation presentation);
  void HoldMacro(Macro macro, Presentation presentation);
  void HoldAmmoItem(AmmoItem item, Presentation presentation);
  void HoldStablePet(StablePet pet, Presentation presentation);
  void HoldGuildBankItem(GuildBankItem item, Presentation presentation);
  void HoldGuildBankMoney(std::uint32_t amount, Presentation presentation);
  void HoldEquipmentSet(EquipmentSet set, Presentation presentation);
  void PlaySound(Sound sound);

  [[nodiscard]] Kind kind() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] Grid grid() const noexcept;
  [[nodiscard]] const Payload& payload() const noexcept { return payload_; }

  template <typename T>
  [[nodiscard]] const T* get_if() const noexcept {
    return std::get_if<T>(&payload_);
  }

  [[nodiscard]] const LiveItem* live_item() const noexcept {
    return get_if<LiveItem>();
  }

 private:
  template <typename T>
  void Hold(T payload, Presentation presentation, bool publish_final_update);

  SourcePort& source_;
  PresentationPort& presentation_;
  Payload payload_{Empty{}};
};

}
