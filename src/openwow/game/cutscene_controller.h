
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class CutsceneType : uint8_t {
    NpcCameraZoom   = 0,
    FlightPathCam   = 1,
    VehicleCam      = 2,
    QuestScene      = 3,
    BossIntro       = 4,
};

struct CutsceneWaypoint {
    float camera_x = 0.0f;
    float camera_y = 0.0f;
    float camera_z = 0.0f;
    float target_x = 0.0f;
    float target_y = 0.0f;
    float target_z = 0.0f;
    float fov = 70.0f;
    float duration_to_next = 1.0f;
};

struct CutsceneData {
    CutsceneType type = CutsceneType::NpcCameraZoom;
    uint64_t source_guid = 0;
    std::string description;
    std::vector<CutsceneWaypoint> waypoints;
    float total_duration = 0.0f;
    bool lockout_player = false;
    bool fade_in = false;
    bool fade_out = false;
};

struct CutsceneCameraState {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    float fov = 70.0f;
    bool active = false;
};

class CutsceneController {
 public:

    void StartCutscene(const CutsceneData& data);

    void StopCutscene();

    void Update(float dt);

    [[nodiscard]] bool IsActive() const;

    [[nodiscard]] CutsceneType GetType() const;

    [[nodiscard]] bool IsPlayerLocked() const;

    [[nodiscard]] float GetCurrentTime() const;

    [[nodiscard]] float GetTotalDuration() const;

    [[nodiscard]] float GetProgress() const;

    [[nodiscard]] uint64_t GetSourceGuid() const;

    [[nodiscard]] CutsceneCameraState GetCamera() const;

    [[nodiscard]] std::optional<CutsceneData> GetData() const;

    void StartNpcZoom(uint64_t npc_guid,
                      float cam_x, float cam_y, float cam_z,
                      float npc_x, float npc_y, float npc_z,
                      float duration = 3.0f, float fov = 40.0f);

    void StartFlightCam(const std::vector<CutsceneWaypoint>& path,
                        bool lock_player = true);

    void StartVehicleCam(uint64_t vehicle_guid,
                         const std::vector<CutsceneWaypoint>& path);

    void Reset();

 private:

    static float ComputeTotalDuration(const std::vector<CutsceneWaypoint>& wps);

    static CutsceneCameraState InterpolateWaypoints(
        const std::vector<CutsceneWaypoint>& wps, float t);

    bool active_ = false;
    CutsceneData data_{};
    float current_time_ = 0.0f;
};

}
