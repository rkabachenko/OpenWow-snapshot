
#include "openwow/game/tracking_system.h"

#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <array>

namespace openwow::game {

namespace {

struct LuaTrackingRecord {
    std::uint32_t type;
    std::uint32_t value;
    const char* nameKey;
    const char* iconToken;
    std::uint32_t classMask;
};

constexpr std::uint32_t kLuaTrackingTypeTrivialQuests = 3;
constexpr char kMinimapTrackedInfoCVarName[] = "minimapTrackedInfo";
constexpr char kTrackingTexturePrefix[] = "Interface\\Minimap\\Tracking\\";
constexpr char kTrackingTextureNone[] = "Interface\\Minimap\\Tracking\\None";

constexpr std::array<LuaTrackingRecord, 15> kLuaTrackingRecords{{
    {1, 0x001000u, "MINIMAP_TRACKING_REPAIR",             "Repair",         0x00000000u},
    {1, 0x000200u, "MINIMAP_TRACKING_VENDOR_FOOD",        "Food",           0x00000000u},
    {1, 0x000400u, "MINIMAP_TRACKING_VENDOR_POISON",      "Poisons",        0x00000010u},
    {1, 0x000100u, "MINIMAP_TRACKING_VENDOR_AMMO",        "Ammunition",     0x0000001Au},
    {1, 0x000800u, "MINIMAP_TRACKING_VENDOR_REAGENT",     "Reagents",       0x00000000u},
    {1, 0x010000u, "MINIMAP_TRACKING_INNKEEPER",          "Innkeeper",      0x00000000u},
    {1, 0x002000u, "MINIMAP_TRACKING_FLIGHTMASTER",       "FlightMaster",   0x00000000u},
    {1, 0x400000u, "MINIMAP_TRACKING_STABLEMASTER",       "StableMaster",   0x00000008u},
    {1, 0x100000u, "MINIMAP_TRACKING_BATTLEMASTER",       "BattleMaster",   0x00000000u},
    {1, 0x000020u, "MINIMAP_TRACKING_TRAINER_CLASS",      "Class",          0x00000000u},
    {1, 0x000040u, "MINIMAP_TRACKING_TRAINER_PROFESSION", "Profession",     0x00000000u},
    {1, 0x200000u, "MINIMAP_TRACKING_AUCTIONEER",         "Auctioneer",     0x00000000u},
    {1, 0x020000u, "MINIMAP_TRACKING_BANKER",             "Banker",         0x00000000u},
    {2, 0x000013u, "MINIMAP_TRACKING_MAILBOX",            "Mailbox",        0x00000000u},
    {3, 0x000000u, "MINIMAP_TRACKING_TRIVIAL_QUESTS",     "TrivialQuests",  0x00000000u},
}};

std::uint32_t BuildPlayerClassMask(std::uint8_t classId) {
    if (classId == 0 || classId >= 32) {
        return 0;
    }
    return 1u << classId;
}

std::string BuildTrackingTexturePath(const char* iconToken) {
    return std::string{kTrackingTexturePrefix} + iconToken;
}

void SortTrackingEntries(std::vector<TrackingEntry>& entries) {
    std::stable_sort(entries.begin(), entries.end(),
                     [](const TrackingEntry& lhs, const TrackingEntry& rhs) {
                         return lhs.spellId < rhs.spellId;
                     });
}

}

TrackingSystem& TrackingSystem::Get() {
    static TrackingSystem instance;
    return instance;
}

void TrackingSystem::AddAvailableTracking(const TrackingEntry& entry) {
    std::lock_guard lock(mutex_);
    for (auto& e : available_) {
        if (e.spellId != 0 && entry.spellId != 0 && e.spellId == entry.spellId) {
            const bool was_active = e.isActive;
            e = entry;
            e.isActive = was_active || entry.isActive;
            SortTrackingEntries(available_);
            return;
        }
    }

    if (entry.category != TrackingCategory::Unknown) {
        for (auto& e : available_) {
            if (e.category != entry.category) {
                continue;
            }
            const bool was_active = e.isActive;
            if (selectedSpellTrackingSpellId_ == e.spellId) {
                selectedSpellTrackingSpellId_ = entry.spellId;
            }
            e = entry;
            e.isActive = was_active || entry.isActive;
            SortTrackingEntries(available_);
            return;
        }
    }

    available_.push_back(entry);
    SortTrackingEntries(available_);
}

std::vector<TrackingEntry> TrackingSystem::GetAvailableTracking() const {
    std::lock_guard lock(mutex_);
    return available_;
}

void TrackingSystem::RemoveAvailableTrackingSpell(const std::uint32_t spell_id) {
    if (spell_id == 0) {
        return;
    }

    std::lock_guard lock(mutex_);
    available_.erase(std::remove_if(available_.begin(), available_.end(),
                                    [spell_id](const TrackingEntry& entry) {
                                        return entry.spellId == spell_id;
                                    }),
                     available_.end());
    if (selectedSpellTrackingSpellId_ == spell_id) {
        selectedSpellTrackingSpellId_ = 0;
    }
}

void TrackingSystem::SetActive(TrackingCategory category, bool active) {
    std::lock_guard lock(mutex_);
    for (auto& e : available_) {
        if (e.category == category) {
            if (active && !canTrackMultiple_) {

                for (auto& o : available_) o.isActive = false;
            }
            e.isActive = active;
            if (active) {
                selectedSpellTrackingSpellId_ = e.spellId;
            }
            if (!active && selectedSpellTrackingSpellId_ == e.spellId) {
                selectedSpellTrackingSpellId_ = 0;
            }
            return;
        }
    }
}

bool TrackingSystem::SelectSpellTracking(TrackingCategory category) {
    std::lock_guard lock(mutex_);
    for (auto& e : available_) {
        if (e.category != category) {
            continue;
        }
        if (!canTrackMultiple_) {
            for (auto& other : available_) {
                other.isActive = false;
            }
        }
        e.isActive = true;
        selectedSpellTrackingSpellId_ = e.spellId;
        return true;
    }
    return false;
}

bool TrackingSystem::SelectSpellTrackingSpell(const std::uint32_t spell_id) {
    if (spell_id == 0) {
        return false;
    }

    bool selected = false;
    {
        std::lock_guard lock(mutex_);
        for (auto& e : available_) {
            if (e.spellId != spell_id) {
                continue;
            }
            if (!canTrackMultiple_) {
                for (auto& other : available_) {
                    other.isActive = false;
                }
            }
            e.isActive = true;
            selectedSpellTrackingSpellId_ = e.spellId;
            selected = true;
            break;
        }
    }

    if (selected) {

        ui::game::ScriptEventDispatch::Get().FireEvent(
            ui::game::events::MINIMAP_UPDATE_TRACKING);
    }
    return selected;
}

bool TrackingSystem::IsActive(TrackingCategory category) const {
    std::lock_guard lock(mutex_);
    for (const auto& e : available_) {
        if (e.category == category) return e.isActive;
    }
    return false;
}

std::vector<TrackingEntry> TrackingSystem::GetActiveTracking() const {
    std::lock_guard lock(mutex_);
    std::vector<TrackingEntry> result;
    for (const auto& e : available_) {
        if (e.isActive) result.push_back(e);
    }
    return result;
}

uint32_t TrackingSystem::GetActiveTrackingCount() const {
    std::lock_guard lock(mutex_);
    uint32_t count = 0;
    for (const auto& e : available_) {
        if (e.isActive) ++count;
    }
    return count;
}

bool TrackingSystem::CanTrackMultiple() const {
    std::lock_guard lock(mutex_);
    return canTrackMultiple_;
}

void TrackingSystem::SetCanTrackMultiple(bool allowed) {
    std::lock_guard lock(mutex_);
    canTrackMultiple_ = allowed;
}

void TrackingSystem::ToggleTracking(TrackingCategory category) {

    std::lock_guard lock(mutex_);
    for (auto& e : available_) {
        if (e.category == category) {
            bool newState = !e.isActive;
            if (newState && !canTrackMultiple_) {
                for (auto& o : available_) o.isActive = false;
            }
            e.isActive = newState;
            if (newState) {
                selectedSpellTrackingSpellId_ = e.spellId;
            } else if (selectedSpellTrackingSpellId_ == e.spellId) {
                selectedSpellTrackingSpellId_ = 0;
            }
            return;
        }
    }
}

std::string TrackingSystem::GetCategoryName(TrackingCategory category) {
    switch (category) {
        case TrackingCategory::Humanoids:    return "Humanoids";
        case TrackingCategory::Beasts:       return "Beasts";
        case TrackingCategory::Demons:       return "Demons";
        case TrackingCategory::Undead:       return "Undead";
        case TrackingCategory::Elementals:   return "Elementals";
        case TrackingCategory::Giants:       return "Giants";
        case TrackingCategory::Dragonkin:    return "Dragonkin";
        case TrackingCategory::Herbs:        return "Herbs";
        case TrackingCategory::Minerals:     return "Minerals";
        case TrackingCategory::Treasure:     return "Treasure";
        case TrackingCategory::Fish:         return "Fish";
        case TrackingCategory::Mailbox:      return "Mailbox";
        case TrackingCategory::Auctioneer:   return "Auctioneer";
        case TrackingCategory::Banker:       return "Banker";
        case TrackingCategory::Innkeeper:    return "Innkeeper";
        case TrackingCategory::Repair:       return "Repair";
        case TrackingCategory::Trainer:      return "Trainer";
        case TrackingCategory::ClassTrainer: return "Class Trainer";
        case TrackingCategory::FlightMaster: return "Flight Master";
        case TrackingCategory::Stablemaster: return "Stablemaster";
        case TrackingCategory::Unknown:      return "Unknown";
    }
    return "Unknown";
}

std::optional<TrackingEntry> TrackingSystem::GetTrackingForCategory(
    TrackingCategory category) const {
    std::lock_guard lock(mutex_);
    for (const auto& e : available_) {
        if (e.category == category) return e;
    }
    return std::nullopt;
}

std::uint32_t TrackingSystem::GetLuaTrackingTypeCount() const {
    EnsureCVarBinding();

    std::lock_guard lock(mutex_);
    return static_cast<std::uint32_t>(GetAvailableLuaTrackingIndicesLocked().size());
}

std::optional<LuaTrackingInfo> TrackingSystem::GetLuaTrackingInfo(
    std::size_t oneBasedIndex) const {
    EnsureCVarBinding();

    std::lock_guard lock(mutex_);
    const auto availableIndices = GetAvailableLuaTrackingIndicesLocked();
    if (oneBasedIndex == 0 || oneBasedIndex > availableIndices.size()) {
        return std::nullopt;
    }

    const auto tableIndex = availableIndices[oneBasedIndex - 1];
    const auto& record = kLuaTrackingRecords[tableIndex];
    return LuaTrackingInfo{
        Localization::Get().GetString(record.nameKey),
        BuildTrackingTexturePath(record.iconToken),
        activeLuaTrackingIndex_.has_value() && *activeLuaTrackingIndex_ == tableIndex,
    };
}

bool TrackingSystem::SetLuaTrackingSelection(ObjectManager& objects,
                                             std::size_t oneBasedIndex) {
    EnsureCVarBinding();

    std::optional<std::size_t> tableIndex;
    {
        std::lock_guard lock(mutex_);
        const auto availableIndices = GetAvailableLuaTrackingIndicesLocked();
        if (oneBasedIndex == 0 || oneBasedIndex > availableIndices.size()) {
            return false;
        }
        tableIndex = availableIndices[oneBasedIndex - 1];
    }

    if (SetLuaTrackingSelectionByTableIndex(tableIndex, true)) {
        RefreshTrivialQuestOverlayModels(objects);
    }
    return true;
}

void TrackingSystem::ClearLuaTrackingSelection(ObjectManager& objects) {
    EnsureCVarBinding();
    if (SetLuaTrackingSelectionByTableIndex(std::nullopt, true)) {
        RefreshTrivialQuestOverlayModels(objects);
    }
}

std::string TrackingSystem::GetTrackingTexturePath() const {
    EnsureCVarBinding();

    std::lock_guard lock(mutex_);
    if (!activeLuaTrackingIndex_.has_value()) {
        return kTrackingTextureNone;
    }

    return BuildTrackingTexturePath(
        kLuaTrackingRecords[*activeLuaTrackingIndex_].iconToken);
}

std::string TrackingSystem::GetCurrentTrackingTexturePath() const {
    EnsureCVarBinding();

    std::lock_guard lock(mutex_);
    if (activeLuaTrackingIndex_.has_value()) {
        return BuildTrackingTexturePath(
            kLuaTrackingRecords[*activeLuaTrackingIndex_].iconToken);
    }

    return GetSelectedSpellTrackingTexturePathLocked();
}

bool TrackingSystem::IsTrivialQuestTrackingActive() const {
    EnsureCVarBinding();

    std::lock_guard lock(mutex_);
    if (!activeLuaTrackingIndex_.has_value()) {
        return false;
    }

    return kLuaTrackingRecords[*activeLuaTrackingIndex_].type ==
           kLuaTrackingTypeTrivialQuests;
}

std::optional<ActiveLuaTrackingSelection>
TrackingSystem::GetActiveLuaTrackingSelection() const {
    EnsureCVarBinding();

    std::lock_guard lock(mutex_);
    if (!activeLuaTrackingIndex_.has_value()) {
        return std::nullopt;
    }

    const auto& record = kLuaTrackingRecords[*activeLuaTrackingIndex_];
    return ActiveLuaTrackingSelection{record.type, record.value};
}

bool TrackingSystem::ApplyTrackedInfoCVarValue(ObjectManager& objects,
                                               std::string_view value) {
    EnsureCVarBinding();

    const bool changed = ApplyTrackedInfoCVarValueStateOnly(value);
    if (changed) {
        RefreshTrivialQuestOverlayModels(objects);
    }
    return changed;
}

bool TrackingSystem::ApplyTrackedInfoCVarValueStateOnly(std::string_view value) {

    if (value.empty()) {
        return SetLuaTrackingSelectionByTableIndex(std::nullopt, false);
    }

    std::optional<std::size_t> matchedIndex;
    {
        std::lock_guard lock(mutex_);
        const auto availableIndices = GetAvailableLuaTrackingIndicesLocked();
        for (const std::size_t tableIndex : availableIndices) {
            if (text::EqualsIgnoreCaseAscii(
                    value, kLuaTrackingRecords[tableIndex].nameKey)) {
                matchedIndex = tableIndex;
                break;
            }
        }
    }

    if (!matchedIndex.has_value()) {
        return false;
    }

    return SetLuaTrackingSelectionByTableIndex(matchedIndex, false);
}

void TrackingSystem::SetActivePlayerClassProvider(
    ActivePlayerClassProvider provider) {
    std::lock_guard lock(mutex_);
    activePlayerClassProvider_ = std::move(provider);
}

void TrackingSystem::ClearActive() {
    std::lock_guard lock(mutex_);
    for (auto& e : available_) e.isActive = false;
    selectedSpellTrackingSpellId_ = 0;
}

void TrackingSystem::Reset() {
    std::lock_guard lock(mutex_);
    available_.clear();
    canTrackMultiple_ = false;
    activeLuaTrackingIndex_.reset();
    selectedSpellTrackingSpellId_ = 0;
}

void TrackingSystem::EnsureCVarBinding() const {
    bool registerBinding = false;
    {
        std::lock_guard lock(mutex_);
        if (!cvarBindingRegistered_) {
            cvarBindingRegistered_ = true;
            registerBinding = true;
        }
    }

    if (!registerBinding) {
        return;
    }

    auto& cvars = ui::game::CVarSystem::Instance();
    cvars.AddCallback(
        kMinimapTrackedInfoCVarName,
        [this](const std::string&, const std::string& newValue) {
        const_cast<TrackingSystem*>(this)->ApplyTrackedInfoCVarValueStateOnly(newValue);
        });
    static_cast<void>(
        const_cast<TrackingSystem*>(this)->ApplyTrackedInfoCVarValueStateOnly(
            cvars.GetCVar(kMinimapTrackedInfoCVarName)));
}

std::uint32_t TrackingSystem::GetActivePlayerClassMask() const {
    const auto provider = activePlayerClassProvider_;
    const std::uint8_t classId = provider ? provider() : 0;
    return BuildPlayerClassMask(classId);
}

std::vector<std::size_t> TrackingSystem::GetAvailableLuaTrackingIndicesLocked() const {
    std::vector<std::size_t> indices;
    indices.reserve(kLuaTrackingRecords.size());

    const std::uint32_t classMask = GetActivePlayerClassMask();
    for (std::size_t i = 0; i < kLuaTrackingRecords.size(); ++i) {
        const auto& record = kLuaTrackingRecords[i];
        if (record.classMask == 0 || (record.classMask & classMask) != 0) {
            indices.push_back(i);
        }
    }

    return indices;
}

bool TrackingSystem::SetLuaTrackingSelectionByTableIndex(
    std::optional<std::size_t> tableIndex, bool syncCVar) {

    bool wasTrivial;
    std::string cvarValue;
    {
        std::lock_guard lock(mutex_);
        wasTrivial = IsTrivialQuestTrackingActiveLocked();
        activeLuaTrackingIndex_ = tableIndex;
        if (tableIndex.has_value()) {
            cvarValue = kLuaTrackingRecords[*tableIndex].nameKey;
        }
    }

    if (syncCVar) {
        ui::game::CVarSystem::Instance().SetCVar(kMinimapTrackedInfoCVarName,
                                                 cvarValue,
                                                 true);
    }

    bool isTrivial;
    {
        std::lock_guard lock(mutex_);
        isTrivial = IsTrivialQuestTrackingActiveLocked();
    }

    ui::game::ScriptEventDispatch::Get().FireEvent(
        ui::game::events::MINIMAP_UPDATE_TRACKING);

    return wasTrivial != isTrivial;
}

std::string TrackingSystem::GetSelectedSpellTrackingTexturePathLocked() const {
    if (selectedSpellTrackingSpellId_ == 0) {
        return kTrackingTextureNone;
    }

    for (const auto& entry : available_) {
        if (entry.spellId == selectedSpellTrackingSpellId_ &&
            !entry.iconPath.empty()) {
            return entry.iconPath;
        }
    }

    return kTrackingTextureNone;
}

bool TrackingSystem::IsTrivialQuestTrackingActiveLocked() const {
    if (!activeLuaTrackingIndex_.has_value()) {
        return false;
    }
    return kLuaTrackingRecords[*activeLuaTrackingIndex_].type ==
           kLuaTrackingTypeTrivialQuests;
}

void TrackingSystem::RefreshTrivialQuestOverlayModels(ObjectManager& objects) {
    objects.EnumVisibleObjectsMutable([](WorldObject &object) {
        const auto type = static_cast<std::uint32_t>(object.GetOverlayDisplayType());
        if (type >= 2 && type <= 4) {
            object.UpdateOverlayModel();
        }
    });
}

}
