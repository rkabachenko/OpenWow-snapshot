#pragma once

#include "openwow/game/actions/held_cursor/held_cursor.h"

namespace openwow::game {
class GuildSystem;
class ItemLockRegistry;
class SpellTargeting;
}

namespace openwow::ui::game {
class ScriptEventDispatch;

class WorldHeldCursorSource final
    : public openwow::game::actions::held_cursor::SourcePort {
 public:
  WorldHeldCursorSource(openwow::game::GuildSystem& guild,
                        ScriptEventDispatch& events)
      : guild_(guild), events_(events) {}

  void BindSpellTargeting(openwow::game::SpellTargeting* spell_targeting);
  void BindItemLocks(openwow::game::ItemLockRegistry* item_locks);
  void CancelItemTargeting() override;
  void AcquireLiveItemLease(
      const openwow::game::actions::held_cursor::LiveItem& item) override;
  void ReleaseLiveItemLease(
      const openwow::game::actions::held_cursor::LiveItem& item) override;
  void UnlockGuildBankItem(
      const openwow::game::actions::held_cursor::GuildBankItem& item) override;
  void PublishGuildBankMoneyOwnerUpdate() override;

 private:
  openwow::game::SpellTargeting* spell_targeting_{nullptr};
  openwow::game::ItemLockRegistry* item_locks_{nullptr};
  openwow::game::GuildSystem& guild_;
  ScriptEventDispatch& events_;
};

}
