#pragma once

#include "openwow/game/object_guid.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace openwow::game {

enum class CTMAction : uint8_t {
    None      = 0,
    Move      = 1,
    Attack    = 2,
    Loot      = 3,
    Interact  = 4,
    FaceTo    = 5,
    FollowUnit = 6,
};

struct CTMWaypoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline constexpr float kCtmMoveArrivalRadius = 0.5f;
inline constexpr float kCtmAttackArrivalRadius = 3.0f;
inline constexpr float kCtmArrivalRadiusSpeedDivisor = 7.0f;

class ClickToMoveSystem {
public:
    struct Destination {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct CompletedAction {
        CTMAction action = CTMAction::None;
        ObjectGuid target;
        Destination destination{0.0f, 0.0f, 0.0f};
    };

    ClickToMoveSystem() = default;

    void MoveTo(float x, float y, float z);
    void AttackUnit(ObjectGuid guid, float x, float y, float z);

    void LootTarget(ObjectGuid guid, float x, float y, float z,
                    float arrival_threshold = 1.0f);
    void InteractWith(ObjectGuid guid, float x, float y, float z,
                      float arrival_threshold = 1.0f);
    void FollowUnit(ObjectGuid guid);
    void FaceTo(float x, float y, float z);
    void Stop();
    void CancelInteraction(ObjectGuid target = ObjectGuid{});

    [[nodiscard]] CTMAction GetAction() const;
    [[nodiscard]] bool IsActive() const;

    [[nodiscard]] bool CameraFollowsUnitDuringDrive() const;

    [[nodiscard]] std::uint32_t GetActionGeneration() const;
    [[nodiscard]] Destination GetDestination() const;
    [[nodiscard]] ObjectGuid GetTarget() const;
    void UpdateDestination(float x, float y, float z);
    [[nodiscard]] std::optional<CompletedAction> ConsumeCompletedAction();

    void SetPath(std::vector<CTMWaypoint> waypoints);
    [[nodiscard]] const std::vector<CTMWaypoint>& GetPath() const;
    [[nodiscard]] uint32_t GetCurrentWaypoint() const;

    [[nodiscard]] float GetDistanceToDestination(float px, float py, float pz) const;
    [[nodiscard]] bool  IsAtDestination(float px, float py, float pz,
                                        float threshold = 1.0f) const;
    void  SetArrivalThreshold(float threshold);
    [[nodiscard]] float GetArrivalThreshold() const;

    void SetVerticalDistanceIncluded(bool included);
    [[nodiscard]] bool IsVerticalDistanceIncluded() const;

    void SetEnabled(bool enabled);
    [[nodiscard]] bool IsEnabled() const;

    void Update(float dt, float playerX, float playerY, float playerZ);

    void Reset();

private:
    [[nodiscard]] bool ReachedDestination(float player_x, float player_y,
                                          float player_z) const;
    void ClearActiveState();
    void StartAction(CTMAction action, float x, float y, float z,
                     ObjectGuid guid = ObjectGuid{});

    bool       enabled_   = true;
    CTMAction  action_    = CTMAction::None;
    float      dest_x_    = 0.0f;
    float      dest_y_    = 0.0f;
    float      dest_z_    = 0.0f;
    ObjectGuid target_;
    float      arrival_threshold_ = 1.0f;
    bool       vertical_distance_included_ = false;
    std::optional<CompletedAction> completed_action_;

    std::vector<CTMWaypoint> path_;
    uint32_t   action_generation_ = 0;
    uint32_t current_waypoint_ = 0;
};

}
