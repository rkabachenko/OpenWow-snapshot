
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class TitlePositionType : uint8_t {
    Prefix = 0,
    Suffix = 1,
};

struct TitleEntryInfo {
    uint32_t          titleId  = 0;
    std::string       name;
    TitlePositionType position = TitlePositionType::Prefix;
    bool              isEarned = false;
};

class TitleDisplaySystem {
 public:

    void SetAvailableTitles(const std::vector<TitleEntryInfo>& titles);

    [[nodiscard]] const std::vector<TitleEntryInfo>& GetAvailableTitles() const;

    [[nodiscard]] std::vector<TitleEntryInfo> GetEarnedTitles() const;

    [[nodiscard]] uint32_t GetEarnedCount() const;

    [[nodiscard]] uint32_t GetTotalCount() const;

    void SetActiveTitle(uint32_t titleId);

    void ClearActiveTitle();

    [[nodiscard]] std::optional<uint32_t> GetActiveTitleId() const;

    [[nodiscard]] std::optional<TitleEntryInfo> GetActiveTitle() const;

    [[nodiscard]] std::string FormatPlayerName(const std::string& playerName) const;

    [[nodiscard]] bool HasTitle(uint32_t titleId) const;

    void AddTitle(const TitleEntryInfo& info);

    [[nodiscard]] std::vector<TitleEntryInfo> GetPrefixTitles() const;

    [[nodiscard]] std::vector<TitleEntryInfo> GetSuffixTitles() const;

    [[nodiscard]] std::vector<TitleEntryInfo> Search(const std::string& query) const;

    void Open();
    void Close();
    [[nodiscard]] bool IsOpen() const;

    void Clear();

 private:
    std::vector<TitleEntryInfo>   titles_;
    std::optional<uint32_t>       activeTitleId_;
    bool                          open_ = false;
};

}
