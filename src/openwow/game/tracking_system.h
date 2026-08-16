
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game {

class ObjectManager;

enum class TrackingCategory : uint32_t {
    Humanoids    = 0,
    Beasts       = 1,
    Demons       = 2,
    Undead       = 3,
    Elementals   = 4,
    Giants       = 5,
    Dragonkin    = 6,
    Herbs        = 7,
    Minerals     = 8,
    Treasure     = 9,
    Fish         = 10,
    Mailbox      = 11,
    Auctioneer   = 12,
    Banker       = 13,
    Innkeeper    = 14,
    Repair       = 15,
    Trainer      = 16,
    ClassTrainer = 17,
    FlightMaster = 18,
    Stablemaster = 19,
    Unknown      = 0xFFFFFFFFu,
};

struct TrackingEntry {
    TrackingCategory category  = TrackingCategory::Humanoids;
    uint32_t         spellId   = 0;
    std::string      name;
    bool             isActive  = false;
    std::string      iconPath;
};

struct LuaTrackingInfo {
    std::string name;
    std::string texturePath;
    bool        active = false;
};

struct ActiveLuaTrackingSelection {
    std::uint32_t type = 0;
    std::uint32_t value = 0;
};

class TrackingSystem {
public:
    using ActivePlayerClassProvider = std::function<std::uint8_t()>;

    static TrackingSystem& Get();

    void AddAvailableTracking(const TrackingEntry& entry);
    [[nodiscard]] std::vector<TrackingEntry> GetAvailableTracking() const;
    void RemoveAvailableTrackingSpell(std::uint32_t spell_id);

    void SetActive(TrackingCategory category, bool active);
    bool SelectSpellTracking(TrackingCategory category);
    bool SelectSpellTrackingSpell(std::uint32_t spell_id);
    [[nodiscard]] bool IsActive(TrackingCategory category) const;
    [[nodiscard]] std::vector<TrackingEntry> GetActiveTracking() const;
    [[nodiscard]] uint32_t GetActiveTrackingCount() const;

    [[nodiscard]] bool CanTrackMultiple() const;
    void SetCanTrackMultiple(bool allowed);

    void ToggleTracking(TrackingCategory category);

    [[nodiscard]] static std::string GetCategoryName(TrackingCategory category);
    [[nodiscard]] std::optional<TrackingEntry> GetTrackingForCategory(TrackingCategory category) const;

    [[nodiscard]] std::uint32_t GetLuaTrackingTypeCount() const;
    [[nodiscard]] std::optional<LuaTrackingInfo> GetLuaTrackingInfo(
        std::size_t oneBasedIndex) const;
    bool SetLuaTrackingSelection(ObjectManager& objects,
                                 std::size_t oneBasedIndex);
    void ClearLuaTrackingSelection(ObjectManager& objects);
    [[nodiscard]] std::string GetTrackingTexturePath() const;
    [[nodiscard]] std::string GetCurrentTrackingTexturePath() const;
    [[nodiscard]] bool IsTrivialQuestTrackingActive() const;
    [[nodiscard]] std::optional<ActiveLuaTrackingSelection>
    GetActiveLuaTrackingSelection() const;
    bool ApplyTrackedInfoCVarValue(ObjectManager& objects,
                                   std::string_view value);
    void SetActivePlayerClassProvider(ActivePlayerClassProvider provider);

    void ClearActive();
    void Reset();

private:
    TrackingSystem() = default;

    void EnsureCVarBinding() const;
    [[nodiscard]] std::uint32_t GetActivePlayerClassMask() const;
    [[nodiscard]] std::vector<std::size_t> GetAvailableLuaTrackingIndicesLocked() const;
    bool SetLuaTrackingSelectionByTableIndex(std::optional<std::size_t> tableIndex,
                                             bool syncCVar);
    bool ApplyTrackedInfoCVarValueStateOnly(std::string_view value);
    [[nodiscard]] std::string GetSelectedSpellTrackingTexturePathLocked() const;
    [[nodiscard]] bool IsTrivialQuestTrackingActiveLocked() const;
    void RefreshTrivialQuestOverlayModels(ObjectManager& objects);

    std::vector<TrackingEntry> available_;
    bool canTrackMultiple_ = false;
    mutable bool cvarBindingRegistered_ = false;
    ActivePlayerClassProvider activePlayerClassProvider_;
    std::optional<std::size_t> activeLuaTrackingIndex_;
    std::uint32_t selectedSpellTrackingSpellId_ = 0;

    mutable std::mutex mutex_;
};

}
