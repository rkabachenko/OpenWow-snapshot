#include "openwow/game/actions/held_cursor/held_cursor.h"

#include <type_traits>
#include <utility>

namespace openwow::game::actions::held_cursor {
namespace {

template <typename T>
constexpr Kind KindOf() {
  if constexpr (std::is_same_v<T, Empty>) return Kind::None;
  if constexpr (std::is_same_v<T, LiveItem>) return Kind::LiveItem;
  if constexpr (std::is_same_v<T, PlayerMoney>) return Kind::PlayerMoney;
  if constexpr (std::is_same_v<T, Spell>) return Kind::Spell;
  if constexpr (std::is_same_v<T, PetAction>) return Kind::PetAction;
  if constexpr (std::is_same_v<T, MerchantItem>) return Kind::MerchantItem;
  if constexpr (std::is_same_v<T, ActionBarItem>) return Kind::ActionBarItem;
  if constexpr (std::is_same_v<T, Macro>) return Kind::Macro;
  if constexpr (std::is_same_v<T, AmmoItem>) return Kind::AmmoItem;
  if constexpr (std::is_same_v<T, StablePet>) return Kind::StablePet;
  if constexpr (std::is_same_v<T, GuildBankItem>) return Kind::GuildBankItem;
  if constexpr (std::is_same_v<T, GuildBankMoney>) {
    return Kind::GuildBankMoney;
  }
  return Kind::EquipmentSet;
}

template <typename T>
constexpr Grid GridOf(const T& payload) {
  if constexpr (requires { payload.grid; }) {
    return payload.grid;
  }
  return Grid::None;
}

Sound ClearSound(const Kind kind) {
  switch (kind) {
    case Kind::PlayerMoney:
    case Kind::GuildBankMoney:
      return Sound::Coins;
    case Kind::Spell:
    case Kind::PetAction:
    case Kind::MerchantItem:
    case Kind::ActionBarItem:
    case Kind::Macro:
    case Kind::AmmoItem:
    case Kind::StablePet:
    case Kind::EquipmentSet:
      return Sound::CursorDropObject;
    case Kind::None:
    case Kind::LiveItem:
    case Kind::GuildBankItem:
      return Sound::None;
  }
  return Sound::None;
}

}

void HeldCursor::Clear(const ClearOptions options) {
  source_.CancelItemTargeting();
  presentation_.ClearHeldOverlay();

  if (const auto* item = get_if<LiveItem>();
      item != nullptr && options.release_source_lease) {
    source_.ReleaseLiveItemLease(*item);
  } else if (const auto* guild_item = get_if<GuildBankItem>();
             guild_item != nullptr && options.release_source_lease) {
    source_.UnlockGuildBankItem(*guild_item);
  } else if (get_if<GuildBankMoney>() != nullptr &&
             options.publish_money_owner_update) {
    source_.PublishGuildBankMoneyOwnerUpdate();
  }

  const auto old_kind = kind();
  const auto old_grid = grid();
  const auto sound = ClearSound(old_kind);
  if (sound != Sound::None) {
    presentation_.PlaySound(sound);
  }
  if (old_grid != Grid::None) {
    presentation_.HideGrid(old_grid);
  }

  payload_ = Empty{};
  presentation_.PublishCursorUpdate();
}

template <typename T>
void HeldCursor::Hold(T payload, Presentation presentation,
                      const bool publish_final_update) {
  if (!empty()) {
    Clear();
  }
  if constexpr (std::is_same_v<T, LiveItem>) {
    source_.AcquireLiveItemLease(payload);
  }
  if constexpr (requires { payload.grid = presentation.grid; }) {
    payload.grid = presentation.grid;
  }
  payload_ = std::move(payload);
  presentation_.PresentHeldOverlay(std::move(presentation.texture_path),
                                   presentation.texture_mode);
  if (presentation.sound != Sound::None) {
    presentation_.PlaySound(presentation.sound);
  }
  if (presentation.grid != Grid::None) {
    presentation_.ShowGrid(presentation.grid);
  }
  if (publish_final_update) {
    presentation_.PublishCursorUpdate();
  }
}

void HeldCursor::HoldLiveItem(LiveItem item, Presentation presentation) {
  Hold(std::move(item), std::move(presentation), true);
}

void HeldCursor::HoldPlayerMoney(const std::uint32_t amount,
                                 Presentation presentation) {
  if (amount == 0) {
    if (kind() == Kind::PlayerMoney) {
      Clear();
    }
    return;
  }
  Hold(PlayerMoney{amount}, std::move(presentation), false);
}

void HeldCursor::HoldSpell(Spell spell, Presentation presentation) {
  Hold(std::move(spell), std::move(presentation), false);
}

void HeldCursor::HoldPetAction(PetAction action, Presentation presentation) {
  Hold(std::move(action), std::move(presentation), false);
}

void HeldCursor::HoldMerchantItem(MerchantItem item,
                                  Presentation presentation) {
  Hold(std::move(item), std::move(presentation), true);
}

void HeldCursor::HoldActionBarItem(ActionBarItem item,
                                   Presentation presentation) {
  Hold(std::move(item), std::move(presentation), true);
}

void HeldCursor::HoldMacro(Macro macro, Presentation presentation) {
  Hold(std::move(macro), std::move(presentation), false);
}

void HeldCursor::HoldAmmoItem(AmmoItem item, Presentation presentation) {
  Hold(std::move(item), std::move(presentation), true);
}

void HeldCursor::HoldStablePet(StablePet pet, Presentation presentation) {
  Hold(std::move(pet), std::move(presentation), false);
}

void HeldCursor::HoldGuildBankItem(GuildBankItem item,
                                   Presentation presentation) {
  Hold(std::move(item), std::move(presentation), true);
}

void HeldCursor::HoldGuildBankMoney(const std::uint32_t amount,
                                    Presentation presentation) {
  if (amount == 0) {
    if (kind() == Kind::GuildBankMoney) {
      Clear();
    }
    return;
  }
  Hold(GuildBankMoney{amount}, std::move(presentation), false);
}

void HeldCursor::HoldEquipmentSet(EquipmentSet set,
                                  Presentation presentation) {
  Hold(std::move(set), std::move(presentation), false);
}

void HeldCursor::PlaySound(const Sound sound) {
  if (sound != Sound::None) {
    presentation_.PlaySound(sound);
  }
}

Kind HeldCursor::kind() const noexcept {
  return std::visit(
      []<typename T>(const T&) { return KindOf<T>(); }, payload_);
}

bool HeldCursor::empty() const noexcept {
  return std::holds_alternative<Empty>(payload_);
}

Grid HeldCursor::grid() const noexcept {
  return std::visit(
      []<typename T>(const T& payload) { return GridOf(payload); }, payload_);
}

}
