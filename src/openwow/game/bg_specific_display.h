#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class BGSpecificType : uint8_t {
    WarsongGulch        = 0,
    ArathiBasin         = 1,
    AlteracValley       = 2,
    EyeOfTheStorm       = 3,
    StrandOfTheAncients = 4,
    IsleOfConquest      = 5,
};

enum class WSGFlagState : uint8_t {
    NotCarried     = 0,
    CarriedByAlly  = 1,
    CarriedByEnemy = 2,
    Dropped        = 3,
};

struct WSGDisplay {
    WSGFlagState allianceFlag = WSGFlagState::NotCarried;
    WSGFlagState hordeFlag = WSGFlagState::NotCarried;
    std::string allianceFlagCarrier;
    std::string hordeFlagCarrier;
    uint8_t allianceCaptures = 0;
    uint8_t hordeCaptures = 0;
    static constexpr uint8_t kMaxCaptures = 3;
};

enum class ABBaseState : uint8_t {
    Neutral            = 0,
    AllianceControlled = 1,
    HordeControlled    = 2,
    AllianceContested  = 3,
    HordeContested     = 4,
};

struct ABDisplay {
    std::array<ABBaseState, 5> bases = {
        ABBaseState::Neutral, ABBaseState::Neutral, ABBaseState::Neutral,
        ABBaseState::Neutral, ABBaseState::Neutral};
    uint16_t allianceResources = 0;
    uint16_t hordeResources = 0;
    static constexpr uint16_t kMaxResources = 1600;
    static const std::array<std::string, 5> kBaseNames;
};

struct AVDisplay {
    uint16_t allianceReinforcements = 0;
    uint16_t hordeReinforcements = 0;
    uint8_t allianceTowersControlled = 0;
    uint8_t hordeTowersControlled = 0;
    static constexpr uint16_t kStartingReinforcements = 600;
};

enum class EotSTowerState : uint8_t {
    Neutral            = 0,
    AllianceControlled = 1,
    HordeControlled    = 2,
    AllianceContested  = 3,
    HordeContested     = 4,
};

enum class EotSFlagState : uint8_t {
    OnGround   = 0,
    CarriedByAlliance = 1,
    CarriedByHorde    = 2,
    Respawning = 3,
};

struct EotSDisplay {
    std::array<EotSTowerState, 4> towers = {
        EotSTowerState::Neutral, EotSTowerState::Neutral,
        EotSTowerState::Neutral, EotSTowerState::Neutral};
    EotSFlagState flagState = EotSFlagState::OnGround;
    std::string flagCarrierName;
    float flagX = 0.0f;
    float flagY = 0.0f;
    uint16_t allianceScore = 0;
    uint16_t hordeScore = 0;
    static constexpr uint16_t kMaxScore = 1600;
    static const std::array<std::string, 4> kTowerNames;
};

enum class SotAGateState : uint8_t {
    Intact      = 0,
    Damaged     = 1,
    Destroyed   = 2,
};

struct SotADisplay {

    std::array<SotAGateState, 5> gates = {
        SotAGateState::Intact, SotAGateState::Intact, SotAGateState::Intact,
        SotAGateState::Intact, SotAGateState::Intact};
    uint8_t round = 1;
    uint8_t attackingFaction = 0;
    uint32_t roundTimerMs = 0;
    uint8_t demolishersAlive = 0;
    uint8_t demolishersMax = 4;
    bool relicCaptured = false;
    uint32_t round1TimeMs = 0;
    static const std::array<std::string, 5> kGateNames;
};

enum class IoCNodeState : uint8_t {
    Neutral            = 0,
    AllianceControlled = 1,
    HordeControlled    = 2,
    AllianceContested  = 3,
    HordeContested     = 4,
};

enum class IoCGateState : uint8_t {
    Intact    = 0,
    Damaged   = 1,
    Destroyed = 2,
};

struct IoCDisplay {

    std::array<IoCNodeState, 7> nodes = {};

    std::array<IoCGateState, 6> gates = {};
    uint16_t allianceReinforcements = 0;
    uint16_t hordeReinforcements = 0;
    float allianceBossHpPct = 100.0f;
    float hordeBossHpPct = 100.0f;
    uint8_t workshopVehiclesAlive = 0;
    static constexpr uint16_t kStartingReinforcements = 300;
    static const std::array<std::string, 7> kNodeNames;
    static const std::array<std::string, 6> kGateNames;
};

class BGSpecificDisplay {
 public:
    void SetBGType(BGSpecificType type);
    [[nodiscard]] BGSpecificType GetBGType() const;

    void SetWSGData(const WSGDisplay& data);
    [[nodiscard]] std::optional<WSGDisplay> GetWSGData() const;

    void SetABData(const ABDisplay& data);
    [[nodiscard]] std::optional<ABDisplay> GetABData() const;

    void SetAVData(const AVDisplay& data);
    [[nodiscard]] std::optional<AVDisplay> GetAVData() const;

    void SetEotSData(const EotSDisplay& data);
    [[nodiscard]] std::optional<EotSDisplay> GetEotSData() const;

    void SetSotAData(const SotADisplay& data);
    [[nodiscard]] std::optional<SotADisplay> GetSotAData() const;

    void SetIoCData(const IoCDisplay& data);
    [[nodiscard]] std::optional<IoCDisplay> GetIoCData() const;

    [[nodiscard]] std::string GetStatusText() const;
    [[nodiscard]] bool IsActive() const;

    void Reset();

 private:
    BGSpecificType bgType_ = BGSpecificType::WarsongGulch;
    bool active_ = false;
    std::optional<WSGDisplay> wsgData_;
    std::optional<ABDisplay> abData_;
    std::optional<AVDisplay> avData_;
    std::optional<EotSDisplay> eotsData_;
    std::optional<SotADisplay> sotaData_;
    std::optional<IoCDisplay> iocData_;
};

}
