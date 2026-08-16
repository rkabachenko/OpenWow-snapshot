
#include "openwow/game/player_unit_field_event_callbacks.h"

#include "openwow/game/descriptor_callback_registry.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/script_event_helpers.h"
#include "openwow/game/update_fields.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <cstdint>
#include <cstring>

namespace openwow::game {

namespace {

std::uint32_t LowGuid(const ObjectGuid guid) {
    return static_cast<std::uint32_t>(guid.GetRawValue() & 0xFFFFFFFFull);
}

std::uint32_t HighGuidPart(const ObjectGuid guid) {
    return static_cast<std::uint32_t>(guid.GetRawValue() >> 32);
}

DescriptorCallbackBinding UnitFieldCallbackBinding() {
    static int key;
    return {reinterpret_cast<std::uintptr_t>(&key), 0};
}

DescriptorCallbackBinding InventoryCallbackBinding() {
    static int key;
    return {reinterpret_cast<std::uintptr_t>(&key), 0};
}

DescriptorCallbackBinding QuestLogCallbackBinding() {
    static int key;
    return {reinterpret_cast<std::uintptr_t>(&key), 0};
}

}

static const char* s_unit_field_event_names[kUnitFieldEventSlotCount] = {

     "UNIT_PET",
     nullptr,
     "UNIT_PET",
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     "UNIT_TARGET",
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     "UNIT_DISPLAYPOWER",
     "UNIT_HEALTH",
     "UNIT_MANA",
     "UNIT_RAGE",
     "UNIT_FOCUS",
     "UNIT_ENERGY",
     "UNIT_HAPPINESS",
     nullptr,
     "UNIT_RUNIC_POWER",
     "UNIT_MAXHEALTH",
     "UNIT_MAXMANA",
     "UNIT_MAXRAGE",
     "UNIT_MAXFOCUS",
     "UNIT_MAXENERGY",
     "UNIT_MAXHAPPINESS",
     nullptr,
     "UNIT_MAXRUNIC_POWER",
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     "UNIT_LEVEL",
     "UNIT_FACTION",
     nullptr,
     nullptr,
     nullptr,
     "UNIT_FLAGS",
     "UNIT_FLAGS",
     nullptr,
     "UNIT_ATTACK_SPEED",
     "UNIT_ATTACK_SPEED",
     "UNIT_RANGEDDAMAGE",
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     "UNIT_DAMAGE",
     "UNIT_DAMAGE",
     "UNIT_DAMAGE",
     "UNIT_DAMAGE",
     nullptr,
     nullptr,
     nullptr,
     "UNIT_PET_EXPERIENCE",
     "UNIT_PET_EXPERIENCE",
     "UNIT_DYNAMIC_FLAGS",
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     "UNIT_STATS",
     "UNIT_STATS",
     "UNIT_STATS",
     "UNIT_STATS",
     "UNIT_STATS",
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     "UNIT_RESISTANCES",
     nullptr,
     nullptr,
     nullptr,
     "UNIT_ATTACK_POWER",
     "UNIT_ATTACK_POWER",
     "UNIT_ATTACK_POWER",
     "UNIT_RANGED_ATTACK_POWER",
     "UNIT_RANGED_ATTACK_POWER",
     "UNIT_RANGED_ATTACK_POWER",
     "UNIT_RANGEDDAMAGE",
     "UNIT_RANGEDDAMAGE",
     "UNIT_MANA",
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     "UNIT_MANA",
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     nullptr,
     "UNIT_STATS",
     nullptr,
     nullptr,
};

static std::uint32_t GetFieldCallbackSize(std::uint32_t index) {
    switch (index) {
        case 0:
        case 2:
        case 6:
        case 12:
            return 8;
        case 125:
        case 132:
            return 28;
        default:
            return 4;
    }
}

int PlayerUnitFieldChangedCallback(std::uint32_t guid_low,
                                   std::uint32_t guid_high,
                                   std::uint32_t byte_offset) {
    (void)guid_high;

    const std::uint32_t field_index = byte_offset >> 2;

    std::uint64_t guid = static_cast<std::uint64_t>(guid_high) << 32
                       | static_cast<std::uint64_t>(guid_low);

    if (field_index < kUnitFieldEventSlotCount
        && s_unit_field_event_names[field_index] != nullptr) {
        ScriptEvents_FireUnitEvent(guid, field_index);
    }

    return 1;
}

int PlayerInventoryChangedCallback(std::uint32_t guid_low,
                                   std::uint32_t guid_high) {
    std::uint64_t guid = static_cast<std::uint64_t>(guid_high) << 32
                       | static_cast<std::uint64_t>(guid_low);

    ScriptEvents_FireUnitEvent(guid, kEventInventoryChanged);
    return 1;
}

int PlayerQuestLogChangedCallback(std::uint32_t guid_low,
                                  std::uint32_t guid_high) {
    std::uint64_t guid = static_cast<std::uint64_t>(guid_high) << 32
                       | static_cast<std::uint64_t>(guid_low);

    ScriptEvents_FireUnitEvent(guid, kEventQuestLogChanged);
    return 1;
}

void Player_RegisterUnitFieldEventCallbacks(void* player_obj) {
    auto* object = static_cast<CGObject_C*>(player_obj);
    if (object == nullptr || !object->IsUnit()) {
        return;
    }

    Player_UnregisterUnitFieldEventCallbacks(player_obj);

    auto& registry = DescriptorCallbackRegistry::Get();
    const ObjectGuid guid = object->GetGuid();

    for (std::uint32_t index = 0; index < kUnitFieldEventSlotCount; ++index) {
        if (s_unit_field_event_names[index] == nullptr) {
            continue;
        }

        const auto relative_offset =
            static_cast<std::uint16_t>(index * sizeof(std::uint32_t));
        (void)registry.RegisterObjectSectionCallback(
            guid, TypeID::kUnit, relative_offset,
            static_cast<std::uint16_t>(GetFieldCallbackSize(index)),
            [relative_offset](const DescriptorFieldChangeView& view) {
                PlayerUnitFieldChangedCallback(
                    LowGuid(view.guid),
                    HighGuidPart(view.guid),
                    relative_offset);
            },
            UnitFieldCallbackBinding());
    }

    if (!object->IsPlayer() || !object->IsActivePlayer()) {
        return;
    }

    (void)registry.RegisterObjectSectionCallback(
        guid, TypeID::kPlayer, kPlayerInventoryOffset, kPlayerInventorySize,
        [](const DescriptorFieldChangeView& view) {
            PlayerInventoryChangedCallback(LowGuid(view.guid), HighGuidPart(view.guid));
        },
        InventoryCallbackBinding());

    for (std::uint32_t slot = 0; slot < kPlayerQuestLogSlotCount; ++slot) {
        const auto offset = static_cast<std::uint16_t>(
            kPlayerQuestLogOffset + slot * kPlayerQuestLogSlotSize);
        (void)registry.RegisterObjectSectionCallback(
            guid, TypeID::kPlayer, offset, kPlayerQuestLogSlotSize,
            [](const DescriptorFieldChangeView& view) {
                PlayerQuestLogChangedCallback(LowGuid(view.guid), HighGuidPart(view.guid));
            },
            QuestLogCallbackBinding());
    }
}

void Player_UnregisterUnitFieldEventCallbacks(void* player_obj) {
    auto* object = static_cast<CGObject_C*>(player_obj);
    if (object == nullptr || !object->IsUnit()) {
        return;
    }

    auto& registry = DescriptorCallbackRegistry::Get();
    const ObjectGuid guid = object->GetGuid();

    for (std::uint32_t index = 0; index < kUnitFieldEventSlotCount; ++index) {
        if (s_unit_field_event_names[index] == nullptr) {
            continue;
        }

        registry.UnregisterObjectSectionCallback(
            guid, TypeID::kUnit,
            static_cast<std::uint16_t>(index * sizeof(std::uint32_t)),
            UnitFieldCallbackBinding());
    }

    if (!object->IsPlayer()) {
        return;
    }

    registry.UnregisterObjectSectionCallback(
        guid, TypeID::kPlayer, kPlayerInventoryOffset,
        InventoryCallbackBinding());
    for (std::uint32_t slot = 0; slot < kPlayerQuestLogSlotCount; ++slot) {
        const auto offset = static_cast<std::uint16_t>(
            kPlayerQuestLogOffset + slot * kPlayerQuestLogSlotSize);
        registry.UnregisterObjectSectionCallback(
            guid, TypeID::kPlayer, offset, QuestLogCallbackBinding());
    }
}

const char* GetUnitFieldEventName(std::uint32_t field_index) {
    if (field_index >= kUnitFieldEventSlotCount) return nullptr;
    return s_unit_field_event_names[field_index];
}

}
