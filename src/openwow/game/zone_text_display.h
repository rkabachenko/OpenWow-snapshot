
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class ZoneTextType : std::uint8_t {
    ZoneName = 0,
    SubzoneName,
    PvPStatus,
    InstanceName,
};

struct ZoneTextEntry {
    std::string text;
    ZoneTextType type{ZoneTextType::ZoneName};
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
    float duration{4.0f};
    float elapsed{0.0f};
    float fadeStart{3.0f};
};

class ZoneTextDisplay {
 public:
    ZoneTextDisplay() = default;

    void ShowZoneText(const std::string& zoneName,
                      float r = 1.0f, float g = 0.82f, float b = 0.0f);

    void ShowSubzoneText(const std::string& subzone);

    void ShowPvPText(const std::string& text,
                     float r = 1.0f, float g = 0.0f, float b = 0.0f);

    void ShowInstanceText(const std::string& name);

    [[nodiscard]] std::vector<ZoneTextEntry> GetActiveTexts() const;

    void Update(float dt);

    [[nodiscard]] std::optional<ZoneTextEntry> GetCurrentZoneText() const;

    [[nodiscard]] std::optional<ZoneTextEntry> GetCurrentSubzoneText() const;

    [[nodiscard]] bool IsShowing() const;

    [[nodiscard]] float GetFadeAlpha(ZoneTextType type) const;

    void ClearAll();

    void Reset();

    static constexpr float kDefaultDuration  = 4.0f;
    static constexpr float kDefaultFadeStart = 3.0f;

 private:

    ZoneTextEntry* FindEntry(ZoneTextType type);

    [[nodiscard]] const ZoneTextEntry* FindEntry(ZoneTextType type) const;

    std::vector<ZoneTextEntry> entries_;
};

}
