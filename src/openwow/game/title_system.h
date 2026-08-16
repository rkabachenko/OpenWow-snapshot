
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

struct TitleEntry {
    uint32_t    title_id   = 0;
    std::string name_male;
    std::string name_female;
    uint32_t    mask_index = 0;
};

enum class TitlePosition : uint8_t {
    Prefix = 0,
    Suffix = 1,
};

struct TitleDisplayEntry {
    uint32_t    titleId  = 0;
    std::string name;
    TitlePosition position = TitlePosition::Prefix;
    uint32_t    bitIndex = 0;
};

class TitleSystem {
public:
    static TitleSystem& Get();

    void AddTitle(uint32_t titleId, const std::string& maleName,
                  const std::string& femaleName, uint32_t maskIndex);
    std::optional<TitleEntry> GetTitleEntry(uint32_t titleId) const;

    std::optional<TitleEntry> GetTitleEntryByMaskIndex(uint32_t maskIndex) const;

    void SetKnownTitles(const std::vector<uint64_t>& bitfield);

    bool HasTitle(uint32_t titleId) const;
    std::vector<uint32_t> GetKnownTitles() const;
    uint32_t GetKnownTitleCount() const;

    void SetCurrentTitle(uint32_t titleId);
    uint32_t GetCurrentTitle() const;
    std::string GetCurrentTitleName(bool isFemale) const;

    using TitleCallback = std::function<void(const TitleEntry&)>;
    void ForEachKnown(const TitleCallback& cb) const;

    void ClearAll();

    void AddKnownTitle(const TitleDisplayEntry& entry);
    [[nodiscard]] bool HasDisplayTitle(uint32_t titleId) const;
    [[nodiscard]] std::vector<TitleDisplayEntry> GetKnownDisplayTitles() const;
    void SelectTitle(uint32_t titleId);
    void ClearSelectedTitle();
    [[nodiscard]] std::optional<TitleDisplayEntry> GetSelectedTitle() const;
    [[nodiscard]] std::string GetDisplayName(const std::string& playerName) const;
    [[nodiscard]] size_t GetTitleCount() const;
    [[nodiscard]] std::vector<TitleDisplayEntry> GetTitlesByPosition(TitlePosition pos) const;
    void Reset();

private:
    TitleSystem() = default;

    mutable std::mutex mutex_;

    std::unordered_map<uint32_t, TitleEntry> title_db_;

    std::vector<uint64_t> known_bits_;

    uint32_t current_title_ = 0;

    std::unordered_map<uint32_t, TitleDisplayEntry> display_titles_;
    std::optional<uint32_t> selected_display_title_;
};

}
