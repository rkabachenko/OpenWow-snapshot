
#include "openwow/game/unit_descriptor_callbacks.h"

#include "openwow/game/character_component_backend.h"
#include "openwow/game/descriptor_callback_registry.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/object_effect_system.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/objects/unit/unit_descriptor_view.h"
#include "openwow/game/objects/unit/unit_spell_visual_runtime.h"
#include "openwow/game/object_types.h"
#include "openwow/game/player_descriptor_callbacks.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/spell_action.h"
#include "openwow/game/spell_book.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/world_session.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace openwow::game {

void CGUnit_C::OnLevelChanged(const WorldSession& session) {

  AddHardcodedOneShotEffect(session, *this, HardcodedEffectId::kUnitLevelUp);

  if (IsPlayer()) {

    Casts().OnPlayerSpellCompleted(*this);
  } else {

    Presentation().RefreshDisplayInfoScale(false);
  }

  if (Presentation().HasNameplateFrame()) {
    Presentation().NotifyNameplateLevelChanged();
  }
}

void CGUnit_C::OnVirtualItemDisplayChanged(std::uint32_t weapon_slot) {
  if (weapon_slot > 2u) {
    return;
  }

  Presentation().AddCharacterVisualRefreshFlags(
      kCharacterModelFlagForceEquipmentRefresh);
}

void CGUnit_C::OnUnitFlags2Changed(WorldSession &session,
                                   std::uint32_t old_flags) {
  if (GetFieldCount() == 0)
    return;

  std::uint32_t new_flags = UnitDescriptorView(*this).UnitFlags();
  std::uint32_t changed = old_flags ^ new_flags;

  if (changed & 0x200000) {
    OnVirtualItemDisplayChanged(0);
    OnVirtualItemDisplayChanged(1);
    ui::game::ScriptEventDispatch::Get().FireUnitFlags(GetGuid().GetRawValue());
  }

  if (changed & 0x180) {
    Interaction().RefreshFactionDependentState(session, false);
  }

  if (changed & 0x800) {
    const ObjectGuid summoned_by = State().GetSummonedBy();
    const ObjectGuid owner_guid = summoned_by.IsEmpty() ? State().GetCreatedBy() : summoned_by;
    const auto* const objects = object_manager();
    if (objects != nullptr && owner_guid == objects->GetActivePlayerGuid()) {
      const bool attack_active = (new_flags & 0x800u) != 0;
      session.pet().SetAttackCommandActive(attack_active);
      ui::game::ScriptEventDispatch::Get().FireEvent(
          attack_active ? ui::game::events::PET_ATTACK_START : ui::game::events::PET_ATTACK_STOP);
    }
  }

  if (changed & 0x4000000) {
    ui::game::ScriptEventDispatch::Get().FireUnitFlags(GetGuid().GetRawValue());
  }

  if (changed & 0x8000000) {
    ui::game::ScriptEventDispatch::Get().FireUnitFlags(GetGuid().GetRawValue());
  }

  if ((changed & 0x2000000) && (new_flags & 0x2000000)) {
    session.NotifyTrackedGuidInvalidated(GetGuid().GetRawValue());
  }

  if ((changed & 0x400u) != 0u) {
    const auto* const objects = object_manager();
    const bool is_active_player =
        objects != nullptr && GetGuid() == objects->GetActivePlayerGuid();
    if (!is_active_player && State().GetHealth() > 0u) {
      Animation().RefreshSelectedStandAnimation(session, 0u, ~0u);
    }
  }
}

void CGUnit_C::HandleVisFlagsUpdate(WorldSession &session,
                                    const std::uint8_t new_vis_flags) {
  const std::uint8_t old_vis_flags = State().GetVisFlags();
  const std::uint8_t changed = new_vis_flags ^ old_vis_flags;

  if (changed & 0x02) {
    Animation().RefreshSelectedStandAnimation(session, 0u, ~0u);

    if (IsActivePlayer()) {
      auto &dispatch = ui::game::ScriptEventDispatch::Get();

      dispatch.FireEvent(ui::game::events::UPDATE_STEALTH);

      dispatch.FireEvent(ui::game::events::SPELL_UPDATE_USABLE);
      if (ui::game::detail::RefreshAllActionSlotValidation(session)) {
        dispatch.FireActionbarUpdateUsable();
      }
      dispatch.FirePetBarUpdateUsable();
    }
  }

}

int CGUnit_C::OnPowerValueChanged(ObjectManager &objects, WorldSession &session,
                                  const std::uint64_t guid,
                                  const int field_offset) {
  static_cast<void>(field_offset);
  const auto *const unit = objects.GetUnit(ObjectGuid(guid));
  if (unit == nullptr) {
    return 1;
  }

  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  dispatch.FireUnitPowerSpecific(guid, static_cast<std::uint8_t>(
                                           unit->State().GetPowerType()));

  if (unit->IsActivePlayer()) {
    if (ui::game::detail::RefreshAllActionSlotValidation(session)) {
      dispatch.FireActionbarUpdateUsable();
    }
    dispatch.FirePetBarUpdateUsable();
  }
  return 1;
}

int CGUnit_C::OnMaxPowerChanged(ObjectManager &objects, WorldSession &session,
                                const std::uint64_t guid,
                                const int field_offset) {
  static_cast<void>(field_offset);
  const auto *const unit = objects.GetUnit(ObjectGuid(guid));
  if (unit == nullptr) {
    return 1;
  }

  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  dispatch.FireUnitMaxPowerSpecific(
      guid, static_cast<std::uint8_t>(unit->State().GetPowerType()));

  if (unit->IsActivePlayer()) {
    if (ui::game::detail::RefreshAllActionSlotValidation(session)) {
      dispatch.FireActionbarUpdateUsable();
    }
    dispatch.FirePetBarUpdateUsable();
  }
  return 1;
}

int CGUnit_C::OnPowerTypeChanged(ObjectManager &objects, WorldSession &session,
                                 const std::uint64_t guid) {
  const auto *const unit = objects.GetUnit(ObjectGuid(guid));
  if (unit == nullptr) {
    return 1;
  }

  ui::game::ScriptEventDispatch::Get().FireUnitDisplayPower(guid);

  if (unit->IsActivePlayer()) {
    auto &dispatch = ui::game::ScriptEventDispatch::Get();
    if (ui::game::detail::RefreshAllActionSlotValidation(session)) {
      dispatch.FireActionbarUpdateUsable();
    }
    dispatch.FirePetBarUpdateUsable();
  }
  return 1;
}

int CGUnit_C::OnCastingSpellChanged(const std::uint64_t guid,
                                    const int field_offset,
                                    const int old_value,
                                    const std::uint32_t *new_value) {
  static_cast<void>(guid);
  static_cast<void>(field_offset);
  static_cast<void>(old_value);
  static_cast<void>(new_value);
  return 1;
}

int CGUnit_C::OnShapeshiftFormChanged(const std::uint64_t guid) {
  static_cast<void>(guid);
  return 1;
}

int OnNpcFlagsChanged(ObjectManager &objects, WorldSession &session,
                      const std::uint64_t guid, int, int,
                      const std::uint32_t *new_value) {
  if (new_value != nullptr) {
    if (auto *const unit = objects.GetMutableUnit(ObjectGuid(guid));
        unit != nullptr) {
      unit->Interaction().OnNPCInteractionFlagsChanged(session, *new_value);
    }
  }
  return 1;
}

int CGUnit_C::OnModelScaleChanged(ObjectManager &objects,
                                  const std::uint64_t guid) {
  if (auto *const unit = objects.GetMutableUnit(ObjectGuid(guid));
      unit != nullptr) {
    unit->Presentation().SetCachedHoverHeight(
        unit->GetFloat(UNIT_FIELD_HOVERHEIGHT));
  }
  return 1;
}

int OnFactionTemplateChanged(ObjectManager &objects, WorldSession &session,
                             const std::uint64_t guid) {
  if (const auto *const unit = objects.GetUnit(ObjectGuid(guid));
      unit != nullptr) {
    unit->Interaction().RefreshFactionDependentState(session, false);
  }
  return 1;
}

int CGUnit_C::OnVisFlagsChanged(ObjectManager &objects, WorldSession &session,
                                const std::uint64_t guid, int, int,
                                const std::uint8_t *new_value) {
  if (new_value != nullptr) {
    if (auto *const unit = objects.GetMutableUnit(ObjectGuid(guid));
        unit != nullptr) {
      unit->HandleVisFlagsUpdate(session, *new_value);
    }
  }
  return 1;
}

int CGUnit_C::OnChannelObjectOrSpellChanged(
    ObjectManager& objects, WorldSession& session, const std::uint64_t guid,
    const std::uint32_t* const new_value) {
  if (new_value == nullptr) {
    return 1;
  }

  auto* const unit = objects.GetMutableUnit(ObjectGuid(guid));
  if (unit == nullptr) {
    return 1;
  }

  const auto channel_target = ObjectGuid::FromHalves(new_value[0], new_value[1]);
  const auto channel_spell = new_value[2];

  if (channel_spell != 0u) {
    if (auto* const target = objects.GetMutableUnit(channel_target);
        target != nullptr) {
      static constexpr std::array<std::uint32_t, 4> kTargetKitTypes = {
          2u, 1u, 5u, 6u};
      for (const auto kit_type : kTargetKitTypes) {
        target->SpellVisuals().DestroyByKitType(
            session, channel_spell, kit_type, 0u,
            false, false);
      }
    }
  }

  unit->SpellVisuals().ResetChainChannelNode();
  unit->Casts().FinalizeTrackedSpell(*unit, session, channel_spell);
  unit->SpellVisuals().RefreshDescriptorChannelVisual(session);
  return 1;
}

int CGUnit_C::OnMountDisplayChanged(ObjectManager &objects,
                                    const WorldSession &session,
                                    const std::uint64_t guid) {
  if (auto *const unit = objects.GetMutableUnit(ObjectGuid(guid));
      unit != nullptr) {
    unit->Mount().ApplyDisplayChange(*unit, session,
                                     unit->Mount().DisplayId(*unit));
  }
  return 1;
}

int CGUnit_C::OnObjectScaleChanged(ObjectManager &objects,
                                   const std::uint64_t guid, int, int,
                                   const float *new_value) {
  if (new_value == nullptr) {
    return 1;
  }
  if (auto *const object = objects.GetMutable(ObjectGuid(guid));
      object != nullptr) {
    object->SetDisplayScale(*new_value, nullptr);
  }
  if (auto *const unit = objects.GetMutableUnit(ObjectGuid(guid));
      unit != nullptr) {
    unit->Presentation().HandleScaleChange(*new_value);
  }
  return 1;
}

int CGUnit_C::DescriptorCallback_LevelChanged(
    ObjectManager &objects, const WorldSession &session,
    const std::uint64_t guid, const std::uint32_t *new_value) {
  auto *const unit = objects.GetMutableUnit(ObjectGuid(guid));
  if (unit == nullptr) {
    return 1;
  }
  const std::uint32_t new_level = new_value != nullptr ? *new_value : 0u;
  if (unit->State().GetLevel() != new_level) {
    unit->OnLevelChanged(session);
  }
  return 1;
}

int OnHealthFieldChanged(ObjectManager &objects, WorldSession &session,
                         const std::uint64_t guid, int, const int old_value,
                         const std::int32_t *new_value) {
  if (new_value == nullptr) {
    return 1;
  }
  auto *const unit = objects.GetMutableUnit(ObjectGuid(guid));
  if (unit == nullptr || (old_value > 0) == (*new_value > 0)) {
    return 1;
  }
  if (*new_value <= 0) {
    if (unit->IsActiveMover()) {
      if (auto *const input = GetInputControlSingleton(); input != nullptr) {
        input->ProcessMovementNow(core::GameClock::GetTickCount32(), true);
      }
    }
    unit->Interaction().HandleDeathStateTransition(session);
    unit->Animation().PlayDeadTransitionAnimation(session, false);
  } else {
    unit->Interaction().HandleAliveStateTransition(session, false);
  }
  return 1;
}

int OnDynamicFlagsChanged(ObjectManager &objects, WorldSession &session,
                          const std::uint64_t guid, int, const int old_value,
                          const std::uint32_t *new_value) {
  if (new_value == nullptr) {
    return 1;
  }
  auto *const unit = objects.GetMutableUnit(ObjectGuid(guid));
  if (unit == nullptr) {
    return 1;
  }
  const auto old_flags = static_cast<std::uint8_t>(old_value);
  const auto new_flags = static_cast<std::uint8_t>(*new_value);
  const auto raw_guid = unit->GetGuid().GetRawValue();
  if ((old_flags & kUnitDynFlagLootable) != 0u &&
      (new_flags & kUnitDynFlagLootable) == 0u) {
    unit->SpellVisuals().DestroyLootEffects(session);
  } else if ((old_flags & kUnitDynFlagLootable) == 0u &&
             (new_flags & kUnitDynFlagLootable) != 0u) {
    TutorialSystem::Instance().TriggerTutorial(6u);
  }
  if (((old_flags ^ new_flags) & kUnitDynFlagDead) != 0u) {
    auto &dispatch = ui::game::ScriptEventDispatch::Get();
    dispatch.FireUnitHealth(raw_guid);
    dispatch.FireUnitPowerSpecific(raw_guid, unit->State().GetPowerType());
    if ((new_flags & kUnitDynFlagDead) != 0u) {
      unit->Interaction().HandleDeathStateTransition(session);
    } else {
      unit->Interaction().HandleAliveStateTransition(session, true);
    }
  }
  if (static_cast<std::int32_t>(unit->State().GetHealth()) <= 0 &&
      (new_flags & kUnitDynFlagLootable) != 0u) {
    if (unit->Loot().CorpseReadyTick() != 0u) {
      unit->SpellVisuals().CleanupForLootEffect();
    }
    unit->SpellVisuals().CreateFromCreatureInfo();
  }
  if (((old_flags ^ new_flags) & kUnitDynFlagTapped) != 0u) {
    ui::game::ScriptEventDispatch::Get().FireUnitFaction(raw_guid);
  }
  return 1;
}

static void OnCreatureEntryResolved_Finalize(ObjectManager& objects,
                                             std::uint64_t guid_raw) {
  openwow::ui::game::GameUI_OnUnitDespawnCleanup(guid_raw);

  openwow::ui::game::ScriptEventDispatch::Get().FireUnitName(guid_raw);
  objects.NotifyCreatureEntryResolved(ObjectGuid(guid_raw));
}

static void OnCreatureEntryResolved_Async(ObjectManager& objects,
                                          std::uint64_t guid_raw,
                                          std::uint32_t entry) {
  const auto *tmpl = objects.query_cache().GetCreatureTemplate(entry);
  if (tmpl == nullptr) {
    return;
  }

  auto *unit = objects.GetMutableUnit(ObjectGuid(guid_raw));
  if (unit != nullptr) {

    (void)unit->Presentation().InitDisplayCollisionBounds(true, false);
  }

  OnCreatureEntryResolved_Finalize(objects, guid_raw);
}

void RegisterUnitDescriptorCallbacks(WorldSession& session) {
  auto& registry = DescriptorCallbackRegistry::Get();

  static std::vector<DescriptorCallbackRegistry::Handle> s_registered_handles;
  for (const auto handle : s_registered_handles) {
    (void)registry.Unregister(handle);
  }
  s_registered_handles.clear();
  const auto record = [](DescriptorCallbackRegistry::Handle handle) {
    s_registered_handles.push_back(handle);
  };

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x00, 16,
      [&session](const DescriptorFieldChangeView& change) {
        if (!change.is_create) {
          OnComboTargetDescriptorChanged(session, change);
        }
      }));

  for (std::uint16_t off = 0xC8; off < 0xD4; off += 4) {
    record(registry.RegisterTypeSectionCallback(
        TypeID::kUnit, off, 4,
        [](const DescriptorFieldChangeView& v) {
          auto* const objects = v.object.object_manager();
          auto* unit = objects != nullptr ? objects->GetMutableUnit(v.guid)
                                          : nullptr;
          if (unit != nullptr) {
            const std::uint32_t slot = (v.offset_bytes - 0xC8) / 4;
            unit->OnVirtualItemDisplayChanged(slot);
          }
        }));
  }

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0xC0, 4,
      [&session](const DescriptorFieldChangeView& v) {
        const auto guid_raw = v.guid.GetRawValue();
        auto* const objects = v.object.object_manager();
        auto* unit = objects != nullptr ? objects->GetMutableUnit(v.guid)
                                        : nullptr;
        if (unit != nullptr) {
          unit->OnLevelChanged(session);
        }
        auto& dispatch = openwow::ui::game::ScriptEventDispatch::Get();
        dispatch.FireUnitLevel(guid_raw);
      }));

  {
    constexpr std::uint16_t kVisibleItemSectionByteBase = 0x21C;
    constexpr std::uint16_t kVisibleItemStrideBytes = 8;
    constexpr std::uint32_t kVisibleItemSlotCount = 19;
    constexpr std::uint32_t kRangedEquipSlot = 17;

    constexpr std::int32_t kSheatheStateRanged = 2;
    record(registry.RegisterTypeSectionCallback(
        TypeID::kPlayer, kVisibleItemSectionByteBase,
        kVisibleItemStrideBytes * kVisibleItemSlotCount,
        [&session](const DescriptorFieldChangeView& v) {
          const std::uint32_t slot =
              (v.offset_bytes - kVisibleItemSectionByteBase) /
              kVisibleItemStrideBytes;
          if (slot != kRangedEquipSlot) {
            return;
          }
          auto* const objects = v.object.object_manager();
          auto* unit =
              objects != nullptr ? objects->GetMutableUnit(v.guid) : nullptr;
          if (unit == nullptr ||
              unit->Animation().GetCachedSheatheState() !=
                  kSheatheStateRanged) {
            return;
          }
          unit->Animation().ChangeSheatheStateAndNotifyServer(
              kSheatheStateRanged, false, true);
          unit->Animation().RefreshSelectedStandAnimation(session, 0u, ~0u);
        }));
  }

  record(registry.RegisterTypeSectionCallback(
      TypeID::kObject, 0x0C, 4,
      [](const DescriptorFieldChangeView& v) {
        const auto guid_raw = v.guid.GetRawValue();

        std::uint32_t new_entry = 0;
        if (!v.new_words.empty()) {
          new_entry = v.new_words[0];
        }
        if (new_entry == 0) {
          return;
        }

        auto* const objects = v.object.object_manager();
        if (objects == nullptr) {
          return;
        }

        const auto *tmpl = objects->query_cache().GetCreatureTemplate(new_entry);

        if (tmpl != nullptr) {

          OnCreatureEntryResolved_Finalize(*objects, guid_raw);
        } else {

          QueryCache::QueryRequestOptions opts;
          opts.context = guid_raw;
          opts.callback = [objects, guid_raw, new_entry](bool success) {
            if (success) {
              OnCreatureEntryResolved_Async(*objects, guid_raw, new_entry);
            }
          };
          (void)objects->query_cache().GetOrRequestCreatureTemplate(
              new_entry, std::move(opts));
        }
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x48, 4,
      [&session](const DescriptorFieldChangeView& v) {
        const auto guid = v.guid.GetRawValue();
        std::int32_t new_health = 0;
        if (!v.new_words.empty()) {
          std::memcpy(&new_health, &v.new_words[0], sizeof(new_health));
        }
        std::int32_t old_health = 0;
        if (!v.old_words.empty()) {
          std::memcpy(&old_health, &v.old_words[0], sizeof(old_health));
        }
        auto* const objects = v.object.object_manager();
        if (objects != nullptr) {
          OnHealthFieldChanged(*objects, session, guid, 0, old_health,
                               &new_health);
        }
      }));

  for (std::uint16_t off = 0x4C; off <= 0x64; off += 4) {
    record(registry.RegisterTypeSectionCallback(
        TypeID::kUnit, off, 4,
        [off, &session](const DescriptorFieldChangeView& v) {
          auto* const objects = v.object.object_manager();
          if (objects != nullptr) {
            CGUnit_C::OnPowerValueChanged(*objects, session,
                                          v.guid.GetRawValue(),
                                          static_cast<int>(off));
          }
        }));
  }

  for (std::uint16_t off = 0x6C; off <= 0x84; off += 4) {
    record(registry.RegisterTypeSectionCallback(
        TypeID::kUnit, off, 4,
        [off, &session](const DescriptorFieldChangeView& v) {
          auto* const objects = v.object.object_manager();
          if (objects != nullptr) {
            CGUnit_C::OnMaxPowerChanged(*objects, session,
                                        v.guid.GetRawValue(),
                                        static_cast<int>(off));
          }
        }));
  }

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x47, 1,
      [&session](const DescriptorFieldChangeView& v) {
        auto* const objects = v.object.object_manager();
        if (objects != nullptr) {
          CGUnit_C::OnPowerTypeChanged(*objects, session,
                                       v.guid.GetRawValue());
        }
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0xDC, 4,
      [](const DescriptorFieldChangeView& v) {
        const auto guid_raw = v.guid.GetRawValue();
        openwow::ui::game::ScriptEventDispatch::Get().FireUnitAura(guid_raw);
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0xD4, 4,
      [&session](const DescriptorFieldChangeView& v) {
        auto* const objects = v.object.object_manager();
        auto* unit = objects != nullptr ? objects->GetMutableUnit(v.guid)
                                        : nullptr;
        if (unit != nullptr) {
          std::uint32_t old_flags = 0;
          if (!v.old_words.empty()) {
            old_flags = v.old_words[0];
          }
          unit->OnUnitFlags2Changed(session, old_flags);
        }
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0xD8, 4,
      [](const DescriptorFieldChangeView& v) {
        const auto guid = v.guid.GetRawValue();
        std::uint32_t new_val = 0;
        if (!v.new_words.empty()) {
          new_val = v.new_words[0];
        }
        std::int32_t old_val = 0;
        if (!v.old_words.empty()) {
          old_val = static_cast<std::int32_t>(v.old_words[0]);
        }
        CGUnit_C::OnCastingSpellChanged(guid, 0, old_val, &new_val);
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x112, 1,
      [&session](const DescriptorFieldChangeView& v) {
        const auto guid = v.guid.GetRawValue();
        std::uint8_t new_byte = 0;
        if (!v.new_words.empty()) {
          new_byte = static_cast<std::uint8_t>((v.new_words[0] >> 16) & 0xFF);
        }
        std::int32_t old_val = 0;
        if (!v.old_words.empty()) {
          old_val = static_cast<std::int32_t>(v.old_words[0]);
        }
        auto* const objects = v.object.object_manager();
        if (objects != nullptr) {
          CGUnit_C::OnVisFlagsChanged(*objects, session, guid, 0, old_val,
                                      &new_byte);
        }
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x1D1, 1,
      [&session](const DescriptorFieldChangeView& v) {

        auto* const objects = v.object.object_manager();
        auto* unit = objects != nullptr ? objects->GetMutableUnit(v.guid)
                                        : nullptr;
        if (unit != nullptr) {
          unit->Interaction().RefreshFactionDependentState(session, false);
        }

        auto& dispatch = openwow::ui::game::ScriptEventDispatch::Get();
        dispatch.FireUnitFlags(v.guid.GetRawValue());
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x113, 1,
      [](const DescriptorFieldChangeView& v) {
        auto* const objects = v.object.object_manager();
        if (objects != nullptr) {
          CGUnit_C::HandleUnitFunc3(*objects, v.guid.GetRawValue());
        }
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0xFC, 4,
      [&session](const DescriptorFieldChangeView& v) {
        auto* const objects = v.object.object_manager();
        if (objects != nullptr) {
          CGUnit_C::OnMountDisplayChanged(*objects, session,
                                          v.guid.GetRawValue());
        }
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0xC4, 4,
      [&session](const DescriptorFieldChangeView& v) {
        auto* const objects = v.object.object_manager();
        if (objects != nullptr) {
          OnFactionTemplateChanged(*objects, session, v.guid.GetRawValue());
        }
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x18, 16,
      [&session](const DescriptorFieldChangeView& v) {

        const auto unit_guid = v.guid;
        const auto active_player = CGObject_C::GetActivePlayerGuid();

        ObjectGuid new_charmedby;
        if (v.new_words.size() >= 2) {
          new_charmedby = ObjectGuid(
              static_cast<std::uint64_t>(v.new_words[1]) << 32 | v.new_words[0]);
        }

        if (new_charmedby == active_player) {

          SpellAction_CancelPeriodicClientSpell(unit_guid, 0);
        } else {

          ObjectGuid old_charmedby;
          if (v.old_words.size() >= 2) {
            old_charmedby = ObjectGuid(
                static_cast<std::uint64_t>(v.old_words[1]) << 32 | v.old_words[0]);
          }

          if (old_charmedby == active_player) {
            auto* const objects = v.object.object_manager();
            auto* unit = objects != nullptr ? objects->GetMutableUnit(unit_guid)
                                            : nullptr;
            if (unit != nullptr) {
              unit->Auras().ReschedulePeriodicClientAuras(*unit, session);
            }
          }
        }
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0xF4, 4,
      [&session](const DescriptorFieldChangeView& v) {
        auto* const objects = v.object.object_manager();
        if (objects != nullptr) {
          CGUnit_C::HandleUnitFunc4(*objects, session,
                                    v.guid.GetRawValue());
        }
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x110, 1,
      [&session](const DescriptorFieldChangeView& v) {
        auto* const objects = v.object.object_manager();
        auto* const unit =
            objects != nullptr ? objects->GetMutableUnit(v.guid) : nullptr;
        if (unit == nullptr || v.old_words.empty()) {
          return;
        }
        const auto previous =
            static_cast<std::uint8_t>(v.old_words.front() & 0xffu);
        const auto current = unit->Animation().GetStandState();
        if (previous != current) {
          unit->Animation().ApplyValuesUpdateSessionEffects(
              session, previous, true, false);
        }
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x1D3, 1,
      [](const DescriptorFieldChangeView& v) {
        CGUnit_C::OnShapeshiftFormChanged(v.guid.GetRawValue());
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x130, 4,
      [&session](const DescriptorFieldChangeView& v) {
        const auto guid = v.guid.GetRawValue();
        std::uint32_t new_val = 0;
        if (!v.new_words.empty()) {
          new_val = v.new_words[0];
        }
        std::int32_t old_val = 0;
        if (!v.old_words.empty()) {
          old_val = static_cast<std::int32_t>(v.old_words[0]);
        }
        auto* const objects = v.object.object_manager();
        if (objects != nullptr) {
          OnNpcFlagsChanged(*objects, session, guid, 0, old_val, &new_val);
        }
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x134, 4,
      [&session](const DescriptorFieldChangeView& v) {
        auto* const objects = v.object.object_manager();
        auto* const unit =
            objects != nullptr ? objects->GetMutableUnit(v.guid) : nullptr;
        if (unit == nullptr) {
          return;
        }
        const std::uint32_t previous =
            v.old_words.empty() ? 0u : v.old_words.front();
        if (previous == unit->Animation().GetEmoteState()) {
          return;
        }
        unit->Animation().ApplyValuesUpdateSessionEffects(
            session, unit->Animation().GetStandState(), false, true);
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x118, 4,
      [](const DescriptorFieldChangeView& v) {
        const auto guid_raw = v.guid.GetRawValue();
        auto& dispatch = openwow::ui::game::ScriptEventDispatch::Get();
        dispatch.FireUnitName(guid_raw);
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x124, 4,
      [&session](const DescriptorFieldChangeView& v) {
        const auto guid = v.guid.GetRawValue();
        std::uint32_t new_val = 0;
        if (!v.new_words.empty()) {
          new_val = v.new_words[0];
        }
        std::int32_t old_val = 0;
        if (!v.old_words.empty()) {
          old_val = static_cast<std::int32_t>(v.old_words[0]);
        }
        auto* const objects = v.object.object_manager();
        if (objects != nullptr) {
          OnDynamicFlagsChanged(*objects, session, guid, 0, old_val, &new_val);
        }
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x38, 12,
      [&session](const DescriptorFieldChangeView& v) {
        const auto guid = v.guid.GetRawValue();
        const std::uint32_t* new_ptr =
            v.new_words.empty() ? nullptr : v.new_words.data();
        auto* const objects = v.object.object_manager();
        if (objects != nullptr) {
          CGUnit_C::OnChannelObjectOrSpellChanged(*objects, session, guid,
                                                  new_ptr);
        }
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x114, 4,
      [](const DescriptorFieldChangeView& v) {
        const auto guid_raw = v.guid.GetRawValue();
        auto& dispatch = openwow::ui::game::ScriptEventDispatch::Get();
        dispatch.FireUnitName(guid_raw);

        dispatch.FireUnitPet(guid_raw);
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kObject, 0x10, 4,
      [](const DescriptorFieldChangeView& v) {
        const auto guid = v.guid.GetRawValue();
        float new_scale = 1.0f;
        if (!v.new_words.empty()) {
          std::memcpy(&new_scale, &v.new_words[0], sizeof(new_scale));
        }
        std::int32_t old_val = 0;
        if (!v.old_words.empty()) {
          old_val = static_cast<std::int32_t>(v.old_words[0]);
        }
        auto* const objects = v.object.object_manager();
        if (objects != nullptr) {
          CGUnit_C::OnObjectScaleChanged(*objects, guid, 0, old_val,
                                         &new_scale);
        }
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x1D0, 1,
      [](const DescriptorFieldChangeView& v) {

        auto* const objects = v.object.object_manager();
        auto* unit = objects != nullptr ? objects->GetMutableUnit(v.guid)
                                        : nullptr;
        if (unit != nullptr) {
          unit->Animation().ChangeSheatheStateAndNotifyServer(
              unit->State().GetSheathState(), true, true);
        }

      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x30, 8,
      [](const DescriptorFieldChangeView& v) {
        if (auto* const objects = v.object.object_manager(); objects != nullptr) {
          CGUnit_C::OnTargetFieldChanged(*objects, v.guid.GetRawValue());
        }
      }));

  record(registry.RegisterTypeSectionCallback(
      TypeID::kUnit, 0x230, 4,
      [](const DescriptorFieldChangeView& v) {
        auto* const objects = v.object.object_manager();
        if (objects != nullptr) {
          CGUnit_C::OnModelScaleChanged(*objects, v.guid.GetRawValue());
        }
      }));
}

}
