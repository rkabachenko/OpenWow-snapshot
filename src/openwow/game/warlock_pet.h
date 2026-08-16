#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class DemonType : uint32_t {
    Imp        = 0,
    Voidwalker = 1,
    Succubus   = 2,
    Felhunter  = 3,
    Felguard   = 4,
    Infernal   = 5,
    Doomguard  = 6,
};

struct ActiveDemon {
    DemonType   type = DemonType::Imp;
    ObjectGuid  guid;
    std::string name;
};

class WarlockPetManager {
 public:
    void SetActiveDemon(DemonType type, ObjectGuid guid, const std::string& name);
    [[nodiscard]] std::optional<ActiveDemon> GetActiveDemon() const;
    [[nodiscard]] bool HasDemon() const;
    void DismissDemon();

    [[nodiscard]] DemonType GetDemonType() const;
    [[nodiscard]] std::string GetDemonName() const;

    [[nodiscard]] static uint32_t GetDemonDisplayId(DemonType type);

    [[nodiscard]] std::vector<DemonType> GetAvailableDemons() const;
    void AddAvailableDemon(DemonType type);
    void RemoveAvailableDemon(DemonType type);

    [[nodiscard]] static std::string GetDemonTypeName(DemonType type);

    [[nodiscard]] bool IsSacrificed() const;
    void SetSacrificed(bool sacrificed);

    [[nodiscard]] bool HasSoulLink() const;
    void SetSoulLink(bool active);

    [[nodiscard]] uint32_t GetSoulShardCount() const;
    void SetSoulShardCount(uint32_t count);

    [[nodiscard]] static uint32_t GetSummonCost(DemonType type);

    void Reset();

 private:
    std::optional<ActiveDemon> active_demon_;
    std::vector<DemonType>     available_demons_;
    bool                       is_sacrificed_ = false;
    bool                       has_soul_link_ = false;
    uint32_t                   soul_shard_count_ = 0;
};

}
