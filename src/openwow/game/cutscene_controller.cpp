
#include "openwow/game/cutscene_controller.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace openwow::game {

namespace {
float Lerp(float a, float b, float t) { return a + (b - a) * t; }
}

void CutsceneController::StartCutscene(const CutsceneData& data) {
    data_ = data;
    if (data_.total_duration <= 0.0f) {
        data_.total_duration = ComputeTotalDuration(data_.waypoints);
    }
    current_time_ = 0.0f;
    active_ = true;
}

void CutsceneController::StopCutscene() {
    active_ = false;
    current_time_ = 0.0f;
    data_ = CutsceneData{};
}

void CutsceneController::Update(float dt) {
    if (!active_) return;
    current_time_ += dt;
    if (current_time_ >= data_.total_duration && data_.total_duration > 0.0f) {
        active_ = false;
    }
}

bool CutsceneController::IsActive() const {
    return active_;
}

CutsceneType CutsceneController::GetType() const {
    return data_.type;
}

bool CutsceneController::IsPlayerLocked() const {
    return active_ && data_.lockout_player;
}

float CutsceneController::GetCurrentTime() const {
    return current_time_;
}

float CutsceneController::GetTotalDuration() const {
    return data_.total_duration;
}

float CutsceneController::GetProgress() const {
    if (data_.total_duration <= 0.0f) return 0.0f;
    return std::clamp(current_time_ / data_.total_duration, 0.0f, 1.0f);
}

uint64_t CutsceneController::GetSourceGuid() const {
    return data_.source_guid;
}

CutsceneCameraState CutsceneController::GetCamera() const {
    if (!active_ || data_.waypoints.empty()) {
        return CutsceneCameraState{};
    }
    auto cam = InterpolateWaypoints(data_.waypoints, current_time_);
    cam.active = true;
    return cam;
}

std::optional<CutsceneData> CutsceneController::GetData() const {
    if (!active_) return std::nullopt;
    return data_;
}

void CutsceneController::StartNpcZoom(uint64_t npc_guid,
                                       float cam_x, float cam_y, float cam_z,
                                       float npc_x, float npc_y, float npc_z,
                                       float duration, float fov) {
    CutsceneData data;
    data.type = CutsceneType::NpcCameraZoom;
    data.source_guid = npc_guid;
    data.description = "NPC focus zoom";
    data.lockout_player = false;

    CutsceneWaypoint start;
    start.camera_x = cam_x;
    start.camera_y = cam_y;
    start.camera_z = cam_z;
    start.target_x = npc_x;
    start.target_y = npc_y;
    start.target_z = npc_z;
    start.fov = 70.0f;
    start.duration_to_next = duration * 0.4f;

    CutsceneWaypoint hold;
    hold.camera_x = cam_x + (npc_x - cam_x) * 0.5f;
    hold.camera_y = cam_y + (npc_y - cam_y) * 0.5f;
    hold.camera_z = cam_z + (npc_z - cam_z) * 0.3f;
    hold.target_x = npc_x;
    hold.target_y = npc_y;
    hold.target_z = npc_z;
    hold.fov = fov;
    hold.duration_to_next = duration * 0.4f;

    CutsceneWaypoint end;
    end.camera_x = cam_x;
    end.camera_y = cam_y;
    end.camera_z = cam_z;
    end.target_x = npc_x;
    end.target_y = npc_y;
    end.target_z = npc_z;
    end.fov = 70.0f;
    end.duration_to_next = duration * 0.2f;

    data.waypoints = {start, hold, end};
    data.total_duration = duration;

    StartCutscene(data);
}

void CutsceneController::StartFlightCam(
    const std::vector<CutsceneWaypoint>& path, bool lock_player) {
    CutsceneData data;
    data.type = CutsceneType::FlightPathCam;
    data.description = "Flight path camera";
    data.waypoints = path;
    data.lockout_player = lock_player;
    data.total_duration = ComputeTotalDuration(path);
    StartCutscene(data);
}

void CutsceneController::StartVehicleCam(
    uint64_t vehicle_guid, const std::vector<CutsceneWaypoint>& path) {
    CutsceneData data;
    data.type = CutsceneType::VehicleCam;
    data.source_guid = vehicle_guid;
    data.description = "Vehicle scripted camera";
    data.waypoints = path;
    data.lockout_player = true;
    data.total_duration = ComputeTotalDuration(path);
    StartCutscene(data);
}

void CutsceneController::Reset() {
    active_ = false;
    current_time_ = 0.0f;
    data_ = CutsceneData{};
}

float CutsceneController::ComputeTotalDuration(
    const std::vector<CutsceneWaypoint>& wps) {
    float total = 0.0f;
    for (const auto& wp : wps) {
        total += wp.duration_to_next;
    }
    return total;
}

CutsceneCameraState CutsceneController::InterpolateWaypoints(
    const std::vector<CutsceneWaypoint>& wps, float t) {
    if (wps.empty()) return {};

    if (wps.size() == 1) {
        const auto& w = wps[0];
        return {w.camera_x, w.camera_y, w.camera_z,
                w.target_x, w.target_y, w.target_z,
                w.fov, false};
    }

    float accumulated = 0.0f;
    for (size_t i = 0; i + 1 < wps.size(); ++i) {
        float seg_dur = wps[i].duration_to_next;
        if (t <= accumulated + seg_dur || i + 2 >= wps.size()) {
            float frac = (seg_dur > 0.0f) ?
                std::clamp((t - accumulated) / seg_dur, 0.0f, 1.0f) : 0.0f;

            const auto& a = wps[i];
            const auto& b = wps[i + 1];

            CutsceneCameraState cs;
            cs.x   = Lerp(a.camera_x, b.camera_x, frac);
            cs.y   = Lerp(a.camera_y, b.camera_y, frac);
            cs.z   = Lerp(a.camera_z, b.camera_z, frac);
            cs.tx  = Lerp(a.target_x, b.target_x, frac);
            cs.ty  = Lerp(a.target_y, b.target_y, frac);
            cs.tz  = Lerp(a.target_z, b.target_z, frac);
            cs.fov = Lerp(a.fov, b.fov, frac);
            cs.active = true;
            return cs;
        }
        accumulated += seg_dur;
    }

    const auto& w = wps.back();
    return {w.camera_x, w.camera_y, w.camera_z,
            w.target_x, w.target_y, w.target_z,
            w.fov, true};
}

}
