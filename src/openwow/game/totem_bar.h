
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class TotemElement : uint32_t {
    Fire  = 0,
    Earth = 1,
    Water = 2,
    Air   = 3,
};

inline constexpr uint32_t kTotemElementCount = 4;

struct TotemSlotInfo {
    TotemElement element   = TotemElement::Fire;
    uint32_t    spellId    = 0;
    std::string name;
    float       duration   = 0.0f;
    float       remaining  = 0.0f;
    bool        hasTotem   = false;
    ObjectGuid  guid;
};

struct TotemSetEntry {
    uint32_t                  setId = 0;
    std::string               name;
    std::array<uint32_t, 4>   spells{};
};

class TotemBar {
public:

    void SetTotem(TotemElement element, uint32_t spellId,
                  const std::string& name, float duration, ObjectGuid guid);

    void DestroyTotem(TotemElement element);

    [[nodiscard]] std::optional<TotemSlotInfo> GetTotem(TotemElement element) const;

    [[nodiscard]] bool HasTotem(TotemElement element) const;

    [[nodiscard]] std::vector<TotemSlotInfo> GetAllTotems() const;

    [[nodiscard]] uint32_t GetActiveTotemCount() const;

    [[nodiscard]] float GetRemainingTime(TotemElement element) const;

    [[nodiscard]] float GetProgress(TotemElement element) const;

    void Update(float dt);

    void DestroyAllTotems();

    void AddSet(const TotemSetEntry& entry);
    [[nodiscard]] std::optional<TotemSetEntry> GetSet(uint32_t setId) const;
    [[nodiscard]] std::vector<TotemSetEntry> GetSets() const;

    void SaveSet(uint32_t setId, const std::string& name);

    [[nodiscard]] uint32_t GetSetCount() const;

    [[nodiscard]] static std::string  GetElementName(TotemElement element);
    [[nodiscard]] static uint32_t     GetElementColor(TotemElement element);

    void Reset();

private:
    std::array<TotemSlotInfo, kTotemElementCount> slots_{};
    std::vector<TotemSetEntry> sets_;
};

}
