
#include "openwow/game/barber_shop.h"

#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/model_preview.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/world/camera/world_camera.h"

#include <array>
#include <optional>
#include <string>

namespace openwow::game {

namespace {

constexpr std::uint8_t kBarberOffHandVisualSlot = 16;
constexpr std::uint8_t kBarberTabardVisualSlot = 18;
constexpr std::uint32_t kBarberPreviewTabardVisualId = 54559;
constexpr float kBarberPreviewModelBoundsRadiusFallback = 1.0f;
constexpr float kBarberWorldCameraZoomStepScale = 0.2f;
constexpr float kBarberPreviewCameraYaw = 3.1415927f;
constexpr float kBarberPreviewCameraPitch = 0.0f;
constexpr float kDegreesToRadians = 0.017453292f;
constexpr int kDefaultCameraViewIndex = 2;
constexpr int kBarberPreviewCameraViewIndex = 7;
constexpr std::array<const char*, 8> kCameraViewSuffixes{
    "", "A", "B", "C", "D", "E", "Com", "Barber Shop"};

struct BarberActivePlayerSnapshot {
  BarberAppearance appearance{};
  std::uint8_t race = 0;
  std::uint8_t gender = 0;
  std::optional<std::uint32_t> off_hand_visual_id;
  std::uint32_t tabard_display_id = 0;
};

std::optional<std::uint32_t> ResolveRetainedBarberOffHandVisualId(
    const WorldSession& session, const CGPlayer_C& player) {
  const ObjectGuid offhand_guid = player.GetEquippedItem(kBarberOffHandVisualSlot);
  if (offhand_guid.IsEmpty()) {
    return std::nullopt;
  }

  const auto* item = session.objects().GetItem(offhand_guid);
  if (item == nullptr) {
    return std::nullopt;
  }

  const auto* item_template = session.item_definitions().GetItem(item->GetEntry());
  if (item_template == nullptr ||
      item_template->inventory_type != InventoryType::Shield) {
    return std::nullopt;
  }

  const std::uint32_t visible_item = player.GetVisibleItemEntry(kBarberOffHandVisualSlot);
  if (visible_item == 0) {
    return std::nullopt;
  }

  return visible_item;
}

std::optional<BarberActivePlayerSnapshot> CaptureActivePlayerSnapshot(
    const WorldSession& session) {
  const auto* player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return std::nullopt;
  }

  BarberActivePlayerSnapshot snapshot{};
  snapshot.appearance = {
      .hair_style = player->GetHairStyle(),
      .hair_color = player->GetHairColor(),
      .facial_hair = player->GetFacialHair(),
      .skin_color = player->GetSkinColor(),
      .face = player->GetFace(),
  };
  snapshot.race = player->State().GetRace();
  snapshot.gender = player->State().GetGender();
  snapshot.off_hand_visual_id =
      ResolveRetainedBarberOffHandVisualId(session, *player);
  snapshot.tabard_display_id = player->GetVisibleItemEntry(kBarberTabardVisualSlot);
  return snapshot;
}

std::uint64_t GetActivePlayerGuidRaw(const WorldSession& session) {
  return session.objects().GetActivePlayerGuid().GetRawValue();
}

bool IsValidCameraViewIndex(const int view_index) {
  return view_index >= 0 && view_index < static_cast<int>(kCameraViewSuffixes.size());
}

std::string BuildCameraViewCVarName(const char* axis, const int view_index) {
  std::string name = "camera";
  name += axis;
  name += kCameraViewSuffixes[view_index];
  return name;
}

int ReadCurrentCameraViewIndex() {
  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  if (!cvars.Exists("cameraView")) {
    return kDefaultCameraViewIndex;
  }

  const int current_view = cvars.GetCVarInt("cameraView");
  return IsValidCameraViewIndex(current_view) ? current_view : kDefaultCameraViewIndex;
}

bool ReadCameraViewPreset(const int view_index, float& distance, float& pitch_radians,
                          float& yaw_radians) {
  if (!IsValidCameraViewIndex(view_index)) {
    return false;
  }

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  distance = cvars.GetCVarFloat(BuildCameraViewCVarName("Distance", view_index));
  pitch_radians =
      cvars.GetCVarFloat(BuildCameraViewCVarName("Pitch", view_index)) * kDegreesToRadians;
  yaw_radians =
      cvars.GetCVarFloat(BuildCameraViewCVarName("Yaw", view_index)) * kDegreesToRadians;
  return true;
}

void SetCurrentCameraViewIndex(const int view_index) {
  if (!IsValidCameraViewIndex(view_index)) {
    return;
  }

  openwow::ui::game::CVarSystem::Instance().SetCVar("cameraView", std::to_string(view_index),
                                                    true);
}

void ApplyCameraViewPreset(openwow::world::WorldCamera& camera, const int view_index) {
  float distance = 0.0f;
  float pitch_radians = 0.0f;
  float yaw_radians = 0.0f;
  if (!ReadCameraViewPreset(view_index, distance, pitch_radians, yaw_radians)) {
    return;
  }

  SetCurrentCameraViewIndex(view_index);

  camera.SetActiveView(view_index);
  camera.SetTargetDistance(distance);
  camera.SetTargetPitch(pitch_radians);

  camera.SetTargetOrbitYaw(yaw_radians);
}

float GetBarberPreviewCameraDistanceScale(const std::uint8_t race) {
  return race == 7 ? 3.0f : 2.0f;
}

void ApplyBarberPreviewCamera(openwow::world::WorldCamera& camera, const float bounds_radius,
                              const float distance_scale) {
  SetCurrentCameraViewIndex(kBarberPreviewCameraViewIndex);
  camera.SetActiveView(kBarberPreviewCameraViewIndex);
  camera.SetTargetDistance(bounds_radius * distance_scale);
  camera.SetTargetOrbitYaw(kBarberPreviewCameraYaw);
  camera.SetTargetPitch(kBarberPreviewCameraPitch);
}

void ApplyAppearanceToModelPreview(const BarberAppearance& appearance) {
  auto& preview = CharacterModelPreview::Get();
  preview.SetSkinColor(appearance.skin_color);
  preview.SetFace(appearance.face);
  preview.SetHairStyle(appearance.hair_style);
  preview.SetHairColor(appearance.hair_color);
  preview.SetFacialHair(appearance.facial_hair);
}

void ApplyBarberResetStylesToModelPreview(const BarberAppearance& appearance) {
  auto& preview = CharacterModelPreview::Get();
  preview.SetHairStyle(appearance.hair_style);
  preview.SetHairColor(appearance.hair_color);
  preview.SetFacialHair(appearance.facial_hair);
  preview.SetSkinColor(appearance.skin_color);
}

void ApplyBarberEquipmentPreview(const BarberActivePlayerSnapshot& snapshot,
                                 const bool session_open) {
  auto& preview = CharacterModelPreview::Get();
  preview.ClearAllSlots();
  if (snapshot.off_hand_visual_id.has_value()) {
    preview.EquipItem(PreviewSlotType::OffHand, *snapshot.off_hand_visual_id);
  }
  if (session_open) {
    preview.EquipItem(PreviewSlotType::Tabard, kBarberPreviewTabardVisualId);
  } else if (snapshot.tabard_display_id != 0) {
    preview.EquipItem(PreviewSlotType::Tabard, snapshot.tabard_display_id);
  }
}

void ApplyActivePlayerSnapshotToModelPreview(const BarberActivePlayerSnapshot& snapshot,
                                             const bool session_open) {
  auto& preview = CharacterModelPreview::Get();
  preview.SetRace(snapshot.race);
  preview.SetGender(snapshot.gender);
  ApplyAppearanceToModelPreview(snapshot.appearance);
  ApplyBarberEquipmentPreview(snapshot, session_open);
}

}

BarberShop& BarberShop::Get() {
  static BarberShop instance;
  return instance;
}

void BarberShop::Open(const WorldSession& session) {
  const auto snapshot = CaptureActivePlayerSnapshot(session);
  BarberAppearance base_appearance{};
  const float preview_camera_distance_scale =
      snapshot.has_value() ? GetBarberPreviewCameraDistanceScale(snapshot->race) : 2.0f;
  auto* const active_camera = world_camera_;
  const bool has_active_camera = active_camera != nullptr && snapshot.has_value();
  const int previous_camera_view =
      has_active_camera ? ReadCurrentCameraViewIndex() : kDefaultCameraViewIndex;

  {
    std::lock_guard lock(mutex_);
    open_ = true;
    preview_model_bounds_radius_ = kBarberPreviewModelBoundsRadiusFallback;
    preview_camera_distance_scale_ = preview_camera_distance_scale;
    previous_camera_view_ = previous_camera_view;
    has_saved_camera_view_ = has_active_camera;
    base_appearance = snapshot.has_value() ? snapshot->appearance : CurrentAppearanceUnlocked();
    SyncAppearanceStateUnlocked(base_appearance);
  }

  if (snapshot.has_value()) {
    ApplyActivePlayerSnapshotToModelPreview(*snapshot, true);
  } else {
    ApplyAppearanceToModelPreview(base_appearance);
  }

  if (has_active_camera) {
    ApplyBarberPreviewCamera(*active_camera, kBarberPreviewModelBoundsRadiusFallback,
                             preview_camera_distance_scale);
  }
}

void BarberShop::Close() {
  auto* const active_camera = world_camera_;
  int restore_camera_view = kDefaultCameraViewIndex;
  bool has_restore_camera_view = false;

  {
    std::lock_guard lock(mutex_);
    open_ = false;
    restore_camera_view = previous_camera_view_;
    has_restore_camera_view = has_saved_camera_view_;
    previous_camera_view_ = kDefaultCameraViewIndex;
    has_saved_camera_view_ = false;
  }

  if (active_camera != nullptr && has_restore_camera_view) {
    ApplyCameraViewPreset(*active_camera, restore_camera_view);
  }
}

bool BarberShop::IsOpen() const {
  std::lock_guard lock(mutex_);
  return open_;
}

void BarberShop::SetPreviewHairStyle(std::uint8_t style) {
  std::lock_guard lock(mutex_);
  preview_hair_style_ = style;
  selected_hair_style_ = style;
  if (open_) {
    ApplyAppearanceToModelPreview(CurrentAppearanceUnlocked());
  }
}

void BarberShop::SetPreviewHairColor(std::uint8_t color) {
  std::lock_guard lock(mutex_);
  preview_hair_color_ = color;
  if (open_) {
    ApplyAppearanceToModelPreview(CurrentAppearanceUnlocked());
  }
}

void BarberShop::SetPreviewFacialHair(std::uint8_t facial) {
  std::lock_guard lock(mutex_);
  preview_facial_hair_ = facial;
  selected_facial_hair_ = facial;
  if (open_) {
    ApplyAppearanceToModelPreview(CurrentAppearanceUnlocked());
  }
}

void BarberShop::SetPreviewSkinColor(std::uint8_t skin) {
  std::lock_guard lock(mutex_);
  preview_skin_color_ = skin;
  selected_skin_color_ = skin;
  if (open_) {
    ApplyAppearanceToModelPreview(CurrentAppearanceUnlocked());
  }
}

void BarberShop::SetPreviewFace(std::uint8_t face) {
  std::lock_guard lock(mutex_);
  preview_face_ = face;
  if (open_) {
    ApplyAppearanceToModelPreview(CurrentAppearanceUnlocked());
  }
}

void BarberShop::SetPreviewAppearance(const BarberAppearance& appearance) {
  std::lock_guard lock(mutex_);
  preview_hair_style_ = appearance.hair_style;
  preview_hair_color_ = appearance.hair_color;
  preview_facial_hair_ = appearance.facial_hair;
  preview_skin_color_ = appearance.skin_color;
  preview_face_ = appearance.face;
  SyncSelectedStyleStateUnlocked(appearance);
  if (open_) {
    ApplyAppearanceToModelPreview(appearance);
  }
}

void BarberShop::ResetPreviewStylesFromActivePlayerAppearance(
    const BarberAppearance& appearance) {
  std::lock_guard lock(mutex_);
  preview_hair_style_ = appearance.hair_style;
  preview_hair_color_ = appearance.hair_color;
  preview_facial_hair_ = appearance.facial_hair;
  preview_skin_color_ = appearance.skin_color;
  selected_hair_style_ = appearance.hair_style;
  selected_facial_hair_ = appearance.facial_hair;
  if (open_) {
    ApplyBarberResetStylesToModelPreview(appearance);
  }
}

std::uint8_t BarberShop::GetPreviewHairStyle() const {
  std::lock_guard lock(mutex_);
  return preview_hair_style_;
}

std::uint8_t BarberShop::GetPreviewHairColor() const {
  std::lock_guard lock(mutex_);
  return preview_hair_color_;
}

std::uint8_t BarberShop::GetPreviewFacialHair() const {
  std::lock_guard lock(mutex_);
  return preview_facial_hair_;
}

std::uint8_t BarberShop::GetPreviewSkinColor() const {
  std::lock_guard lock(mutex_);
  return preview_skin_color_;
}

std::uint8_t BarberShop::GetPreviewFace() const {
  std::lock_guard lock(mutex_);
  return preview_face_;
}

BarberAppearance BarberShop::GetPreviewAppearance() const {
  std::lock_guard lock(mutex_);
  return {
      .hair_style = preview_hair_style_,
      .hair_color = preview_hair_color_,
      .facial_hair = preview_facial_hair_,
      .skin_color = preview_skin_color_,
      .face = preview_face_,
  };
}

std::uint8_t BarberShop::GetSelectedHairStyle() const {
  std::lock_guard lock(mutex_);
  return selected_hair_style_;
}

std::uint8_t BarberShop::GetSelectedFacialHair() const {
  std::lock_guard lock(mutex_);
  return selected_facial_hair_;
}

std::uint8_t BarberShop::GetSelectedSkinColor() const {
  std::lock_guard lock(mutex_);
  return selected_skin_color_;
}

std::uint32_t BarberShop::GetCost() const {
  std::lock_guard lock(mutex_);
  return cost_;
}

void BarberShop::SetCost(std::uint32_t copper) {
  std::lock_guard lock(mutex_);
  cost_ = copper;
}

std::uint8_t BarberShop::GetNumHairStyles() const {
  std::lock_guard lock(mutex_);
  return num_hair_styles_;
}

std::uint8_t BarberShop::GetNumHairColors() const {
  std::lock_guard lock(mutex_);
  return num_hair_colors_;
}

std::uint8_t BarberShop::GetNumFacialHairStyles() const {
  std::lock_guard lock(mutex_);
  return num_facial_hair_;
}

void BarberShop::SetNumOptions(std::uint8_t hair_styles,
                               std::uint8_t hair_colors,
                               std::uint8_t facial_hair) {
  std::lock_guard lock(mutex_);
  num_hair_styles_ = hair_styles;
  num_hair_colors_ = hair_colors;
  num_facial_hair_ = facial_hair;
}

void BarberShop::ApplyChanges() {
  auto* const active_camera = world_camera_;
  int restore_camera_view = kDefaultCameraViewIndex;
  bool has_restore_camera_view = false;

  {
    std::lock_guard lock(mutex_);

    SyncAppearanceStateUnlocked({
        .hair_style = preview_hair_style_,
        .hair_color = preview_hair_color_,
        .facial_hair = preview_facial_hair_,
        .skin_color = preview_skin_color_,
        .face = preview_face_,
    });
    restore_camera_view = previous_camera_view_;
    has_restore_camera_view = has_saved_camera_view_;
    previous_camera_view_ = kDefaultCameraViewIndex;
    has_saved_camera_view_ = false;
    open_ = false;
  }

  if (active_camera != nullptr && has_restore_camera_view) {
    ApplyCameraViewPreset(*active_camera, restore_camera_view);
  }
}

bool BarberShop::Cancel(WorldSession& session) {
  const auto snapshot = CaptureActivePlayerSnapshot(session);
  const auto active_player_guid = GetActivePlayerGuidRaw(session);
  bool flush_deferred_active_player_refresh = false;
  auto* const active_camera = world_camera_;
  int restore_camera_view = kDefaultCameraViewIndex;
  bool has_restore_camera_view = false;

  {
    std::lock_guard lock(mutex_);
    if (!open_) {
      return false;
    }

    open_ = false;
    flush_deferred_active_player_refresh =
        deferred_active_player_appearance_refresh_;
    deferred_active_player_appearance_refresh_ = false;
    restore_camera_view = previous_camera_view_;
    has_restore_camera_view = has_saved_camera_view_;
    previous_camera_view_ = kDefaultCameraViewIndex;
    has_saved_camera_view_ = false;
    if (snapshot.has_value()) {
      SyncAppearanceStateUnlocked(snapshot->appearance);
    }
  }

  session.inspect().CloseBarberShop();
  auto& dispatch = ui::game::ScriptEventDispatch::Get();
  dispatch.FireBarberShopClose();
  if (snapshot.has_value()) {
    ApplyActivePlayerSnapshotToModelPreview(*snapshot, false);
  }
  if (active_camera != nullptr && has_restore_camera_view) {
    ApplyCameraViewPreset(*active_camera, restore_camera_view);
  }
  if (flush_deferred_active_player_refresh && active_player_guid != 0) {

    dispatch.FireUnitPortrait(active_player_guid);
    dispatch.FireUnitModel(active_player_guid);
  }
  return true;
}

void BarberShop::MarkDeferredActivePlayerAppearanceRefresh() {
  std::lock_guard lock(mutex_);
  deferred_active_player_appearance_refresh_ = true;
}

void BarberShop::RevertChanges() {
  std::lock_guard lock(mutex_);

  preview_hair_style_ = saved_hair_style_;
  preview_hair_color_ = saved_hair_color_;
  preview_facial_hair_ = saved_facial_hair_;
  preview_skin_color_ = saved_skin_color_;
  preview_face_ = saved_face_;
  SyncSelectedStyleStateUnlocked({
      .hair_style = saved_hair_style_,
      .hair_color = saved_hair_color_,
      .facial_hair = saved_facial_hair_,
      .skin_color = saved_skin_color_,
      .face = saved_face_,
  });
  if (open_) {
    ApplyAppearanceToModelPreview(CurrentAppearanceUnlocked());
  }
}

void BarberShop::SetPreviewModelBoundsRadius(const float radius) {
  if (radius <= 0.0f) {
    return;
  }

  auto* const active_camera = world_camera_;
  float distance_scale = 2.0f;
  bool update_active_camera = false;

  {
    std::lock_guard lock(mutex_);
    preview_model_bounds_radius_ = radius;
    distance_scale = preview_camera_distance_scale_;
    update_active_camera = open_ && has_saved_camera_view_ && active_camera != nullptr;
  }

  if (update_active_camera) {
    ApplyBarberPreviewCamera(*active_camera, radius, distance_scale);
  }
}

float BarberShop::GetPreviewModelBoundsRadius() const {
  std::lock_guard lock(mutex_);
  return preview_model_bounds_radius_;
}

float BarberShop::GetWorldCameraMinZoomDistance() const {
  std::lock_guard lock(mutex_);
  return open_ ? preview_model_bounds_radius_ : 0.0f;
}

float BarberShop::GetWorldCameraZoomStepScale() const {
  std::lock_guard lock(mutex_);
  return open_ ? kBarberWorldCameraZoomStepScale : 1.0f;
}

void BarberShop::Reset() {
  std::lock_guard lock(mutex_);
  open_ = false;
  preview_hair_style_ = 0;
  preview_hair_color_ = 0;
  preview_facial_hair_ = 0;
  preview_skin_color_ = 0;
  preview_face_ = 0;
  selected_hair_style_ = 0;
  selected_facial_hair_ = 0;
  selected_skin_color_ = 0;
  num_hair_styles_ = 1;
  num_hair_colors_ = 1;
  num_facial_hair_ = 1;
  cost_ = 0;
  preview_model_bounds_radius_ = kBarberPreviewModelBoundsRadiusFallback;
  preview_camera_distance_scale_ = 2.0f;
  previous_camera_view_ = kDefaultCameraViewIndex;
  has_saved_camera_view_ = false;
  saved_hair_style_ = 0;
  saved_hair_color_ = 0;
  saved_facial_hair_ = 0;
  saved_skin_color_ = 0;
  saved_face_ = 0;
  deferred_active_player_appearance_refresh_ = false;
}

BarberAppearance BarberShop::CurrentAppearanceUnlocked() const {
  return {
      .hair_style = preview_hair_style_,
      .hair_color = preview_hair_color_,
      .facial_hair = preview_facial_hair_,
      .skin_color = preview_skin_color_,
      .face = preview_face_,
  };
}

void BarberShop::SyncSelectedStyleStateUnlocked(
    const BarberAppearance& appearance) {
  selected_hair_style_ = appearance.hair_style;
  selected_facial_hair_ = appearance.facial_hair;
  selected_skin_color_ = appearance.skin_color;
}

void BarberShop::SyncAppearanceStateUnlocked(const BarberAppearance& appearance) {
  preview_hair_style_ = appearance.hair_style;
  preview_hair_color_ = appearance.hair_color;
  preview_facial_hair_ = appearance.facial_hair;
  preview_skin_color_ = appearance.skin_color;
  preview_face_ = appearance.face;
  SyncSelectedStyleStateUnlocked(appearance);
  saved_hair_style_ = appearance.hair_style;
  saved_hair_color_ = appearance.hair_color;
  saved_facial_hair_ = appearance.facial_hair;
  saved_skin_color_ = appearance.skin_color;
  saved_face_ = appearance.face;
}

}
