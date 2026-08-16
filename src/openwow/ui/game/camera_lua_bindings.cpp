#include "openwow/ui/game/camera_lua_bindings.h"

extern "C" {
#include <lua.hpp>
}

#include "openwow/game/barber_shop.h"
#include "openwow/game/camera_view_presets.h"
#include "openwow/core/decimal_parse.h"
#include "openwow/input/input_manager.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/world/camera/world_camera.h"

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <string>
#include <utility>

namespace openwow::ui::game::detail {

namespace {

constexpr float kDegreesToRadians = 0.017453292f;
constexpr int kMinimumLuaViewIndex = 1;
constexpr int kMaximumLuaViewIndex = 5;

bool IsValidLuaViewIndex(const int view_index) {
  return view_index >= kMinimumLuaViewIndex && view_index <= kMaximumLuaViewIndex;
}

int GetViewBlendStyle() {
  return openwow::ui::game::CVarSystem::Instance().GetCVarInt("cameraViewBlendStyle");
}

int GetCurrentLuaViewIndex() {
  return openwow::ui::game::CVarSystem::Instance().GetCVarInt("cameraView");
}

float ReadCameraValue(CVarSystem& cvars, const std::string& name,
                      const char* fallback) {
  return cvars.Exists(name)
             ? cvars.GetCVarFloat(name)
             : static_cast<float>(
                   openwow::core::ParseFloatLikeSub76FB80(fallback));
}

}

float ReadOptionalCameraZoomIncrement(lua_State *state) {
  if (lua_isnumber(state, 1) == 0) {
    return 1.0f;
  }

  return static_cast<float>(lua_tonumber(state, 1));
}

void ZoomActiveCamera(openwow::world::WorldCamera& camera,
                      const bool zoom_in, float amount) {
  SyncCameraMotionSettings(camera);
  amount *= openwow::game::BarberShop::Get().GetWorldCameraZoomStepScale();

  const std::uint32_t timestamp =
      openwow::input::InputManager::Get().GetLastMessageTimestamp();
  camera.QueueScriptedZoomStep(zoom_in, timestamp, amount);
}

void SyncCameraMotionSettings(openwow::world::WorldCamera& camera) {
  auto& cvars = CVarSystem::Instance();
  const float minimum_distance =
      openwow::game::BarberShop::Get().GetWorldCameraMinZoomDistance();
  camera.SetMotionSettings({
      .minimum_distance = minimum_distance,
      .maximum_distance =
          std::max(minimum_distance,
                   std::min(cvars.GetCVarFloat("cameraDistanceMax") *
                                cvars.GetCVarFloat(
                                    "cameraDistanceMaxFactor"),
                            50.0f)),
      .distance_speed = cvars.GetCVarFloat("cameraDistanceMoveSpeed"),
      .yaw_speed_degrees = cvars.GetCVarFloat("cameraYawMoveSpeed"),
      .pitch_speed_degrees = cvars.GetCVarFloat("cameraPitchMoveSpeed"),
  });
  SyncCameraSmoothingSettings(camera);
}

void SyncCameraSmoothingSettings(openwow::world::WorldCamera& camera) {

  auto& cvars = CVarSystem::Instance();
  namespace camera_cvars = openwow::game::camera;

  openwow::world::CameraSmoothingSettings settings{};
  settings.smooth_yaw_enabled = cvars.GetCVarInt("cameraSmoothYaw") != 0;
  settings.smooth_pitch_enabled = cvars.GetCVarInt("cameraSmoothPitch") != 0;
  settings.custom_view_smoothing =
      cvars.GetCVarInt("cameraCustomViewSmoothing") != 0;
  settings.yaw_smooth_min_radians =
      cvars.GetCVarFloat("cameraYawSmoothMin") * kDegreesToRadians;
  settings.yaw_smooth_max_radians =
      cvars.GetCVarFloat("cameraYawSmoothMax") * kDegreesToRadians;
  settings.pitch_smooth_min_radians =
      cvars.GetCVarFloat("cameraPitchSmoothMin") * kDegreesToRadians;
  settings.pitch_smooth_max_radians =
      cvars.GetCVarFloat("cameraPitchSmoothMax") * kDegreesToRadians;
  settings.yaw_smooth_speed_degrees =
      cvars.GetCVarFloat("cameraYawSmoothSpeed");
  settings.pitch_smooth_speed_degrees =
      cvars.GetCVarFloat("cameraPitchSmoothSpeed");
  settings.distance_smooth_speed =
      cvars.GetCVarFloat("cameraDistanceSmoothSpeed");
  settings.smooth_time_min_seconds = cvars.GetCVarFloat("cameraSmoothTimeMin");
  settings.smooth_time_max_seconds = cvars.GetCVarFloat("cameraSmoothTimeMax");

  const auto clamp_style = [](const int value) {
    return static_cast<std::size_t>(std::clamp(
        value, 0,
        static_cast<int>(
            camera_cvars::kRetailCameraSmoothStyleSuffixes.size()) -
            1));
  };
  const auto read_style_rows =
      [&](const std::size_t style,
          std::array<openwow::world::CameraSmoothingDelayFactor,
                     openwow::world::kCameraSmoothingEventCount>& events,
          std::array<openwow::world::CameraSmoothingDelayFactor,
                     openwow::world::kCameraSmoothingAxisCount>& view_data) {
        const std::string style_suffix =
            camera_cvars::kRetailCameraSmoothStyleSuffixes[style];
        for (std::size_t event = 0;
             event < camera_cvars::kRetailCameraSmoothEventSuffixes.size();
             ++event) {
          const auto& defaults =
              camera_cvars::kRetailCameraSmoothEventDefaults
                  [style *
                       camera_cvars::kRetailCameraSmoothEventSuffixes.size() +
                   event];
          const std::string base =
              "cameraSmooth" + style_suffix +
              camera_cvars::kRetailCameraSmoothEventSuffixes[event];
          events[event] = {
              ReadCameraValue(cvars, base + "Delay", defaults.delay),
              ReadCameraValue(cvars, base + "Factor", defaults.factor)};
        }
        for (std::size_t axis = 0;
             axis < camera_cvars::kRetailCameraSmoothAxisSuffixes.size();
             ++axis) {
          const auto& defaults =
              camera_cvars::kRetailCameraSmoothViewDataDefaults
                  [style *
                       camera_cvars::kRetailCameraSmoothAxisSuffixes.size() +
                   axis];
          const std::string base =
              "cameraSmoothViewData" + style_suffix +
              camera_cvars::kRetailCameraSmoothAxisSuffixes[axis];
          view_data[axis] = {
              ReadCameraValue(cvars, base + "Delay", defaults.delay),
              ReadCameraValue(cvars, base + "Factor", defaults.factor)};
        }
      };

  read_style_rows(clamp_style(cvars.GetCVarInt("cameraSmoothStyle")),
                  settings.events, settings.view_data);

  read_style_rows(clamp_style(cvars.GetCVarInt("cameraSmoothTrackingStyle")),
                  settings.tracking_events, settings.tracking_view_data);

  camera.SetSmoothingSettings(std::move(settings));

  camera.SetAutoInteractEnabled(cvars.GetCVarInt("autoInteract") != 0);
}

void SyncCameraViewPreset(openwow::world::WorldCamera& camera,
                          const int requested_view) {
  const int view = std::clamp(requested_view, 0, 7);
  auto& cvars = CVarSystem::Instance();
  const std::string suffix =
      openwow::game::camera::kRetailCameraViewSuffixes[
          static_cast<std::size_t>(view)];
  const auto& defaults = openwow::game::camera::kRetailCameraViewDefaults[
      static_cast<std::size_t>(view)];
  camera.SetViewPreset(
      view,
      {
          .distance = ReadCameraValue(
              cvars, "cameraDistance" + suffix, defaults.distance),
          .pitch_radians =
              ReadCameraValue(cvars, "cameraPitch" + suffix,
                              defaults.pitch_degrees) *
              kDegreesToRadians,
          .relative_yaw_radians =
              ReadCameraValue(cvars, "cameraYaw" + suffix,
                              defaults.yaw_degrees) *
              kDegreesToRadians,
      });
}

void SyncCameraViewPresets(openwow::world::WorldCamera& camera) {
  for (int view = 0; view < 8; ++view) {
    SyncCameraViewPreset(camera, view);
  }
}

void SaveCameraViewPreset(openwow::world::WorldCamera& camera,
                          const int view_index) {
  if (!IsValidLuaViewIndex(view_index)) {
    return;
  }

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  const std::string suffix =
      openwow::game::camera::kRetailCameraViewSuffixes[
          static_cast<std::size_t>(view_index)];
  cvars.SetCVar("cameraDistance" + suffix,
                std::to_string(camera.target_distance()));
  cvars.SetCVar("cameraPitch" + suffix,
                std::to_string(camera.target_pitch() / kDegreesToRadians));

  cvars.SetCVar("cameraYaw" + suffix,
                std::to_string(camera.target_orbit_yaw() / kDegreesToRadians));
  cvars.SetCVar("cameraView", std::to_string(view_index));
  SyncCameraViewPreset(camera, view_index);
}

void SetCameraViewPreset(openwow::world::WorldCamera& camera,
                         const int view_index) {
  if (!IsValidLuaViewIndex(view_index)) {
    return;
  }

  const int blend_style = GetViewBlendStyle();
  if (blend_style < 0 || blend_style > 2) {
    return;
  }
  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  const std::string suffix =
      openwow::game::camera::kRetailCameraViewSuffixes[
          static_cast<std::size_t>(view_index)];
  const float distance = cvars.GetCVarFloat("cameraDistance" + suffix);
  const float pitch =
      cvars.GetCVarFloat("cameraPitch" + suffix) * kDegreesToRadians;
  const float yaw =
      cvars.GetCVarFloat("cameraYaw" + suffix) * kDegreesToRadians;
  camera.SetViewPreset(view_index,
                       {.distance = distance,
                        .pitch_radians = pitch,
                        .relative_yaw_radians = yaw});

  camera.SetActiveView(view_index);
  camera.SetTargetDistance(distance);
  camera.SetTargetPitch(pitch);

  camera.SetTargetOrbitYaw(yaw);
  if (blend_style == 2) {
    camera.SetDistance(distance);
    camera.SetPitch(pitch);
    camera.SetOrbitYaw(yaw);
  }
  cvars.SetCVar("cameraView", std::to_string(view_index));
}

void ResetCameraViewPreset(openwow::world::WorldCamera& camera,
                           const int view_index) {
  if (!IsValidLuaViewIndex(view_index)) {
    return;
  }

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  const bool active = GetCurrentLuaViewIndex() == view_index;
  const std::string suffix =
      openwow::game::camera::kRetailCameraViewSuffixes[
          static_cast<std::size_t>(view_index)];
  const auto& defaults = openwow::game::camera::kRetailCameraViewDefaults[
      static_cast<std::size_t>(view_index)];
  cvars.SetCVar("cameraDistance" + suffix, defaults.distance);
  cvars.SetCVar("cameraPitch" + suffix, defaults.pitch_degrees);
  cvars.SetCVar("cameraYaw" + suffix, defaults.yaw_degrees);
  SyncCameraViewPreset(camera, view_index);
  if (active) {
    SetCameraViewPreset(camera, view_index);
  }
}

void StepCameraViewPreset(openwow::world::WorldCamera& camera,
                          const int direction) {
  const int current_view = GetCurrentLuaViewIndex();
  const int next_view = current_view + (direction < 0 ? -1 : 1);
  if (!IsValidLuaViewIndex(next_view)) {
    return;
  }

  SetCameraViewPreset(camera, next_view);
}

void FlipActiveCameraYawDegrees(openwow::world::WorldCamera& camera,
                                const double degrees) {

  camera.FlipYaw(static_cast<float>(degrees * kDegreesToRadians));
}

}
