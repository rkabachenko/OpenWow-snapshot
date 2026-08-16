
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/player_control_runtime.h"

#include "openwow/core/display_settings.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/ceffect_c.h"
#include "openwow/game/missile_node.h"
#include "openwow/game/movement_callbacks.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/tracking_system.h"
#include "openwow/game/world_session.h"

#include "openwow/render/m2/m2_system.h"
#include "openwow/world/camera/world_camera.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>

namespace openwow::ui::game {
void GameUI_GetUnitModelDisplay(std::uint64_t guid);
}

namespace openwow::game {

openwow::audio::SoundRuntime& CGObject_C::sound_runtime() const {
  return object_manager_->sound_runtime();
}

thread_local ObjectGuid CGObject_C::s_active_player_guid_;

namespace {

constexpr double kOpacityByteToCompositeAlpha = 1.0 / (255.0 * 255.0);

[[nodiscard]] bool QueryPrimaryM2AttachmentPosition(
    const CGObject_C& object,
    const std::uint32_t attachment_lookup_index,
    float* const out_position) {
  const std::uint32_t instance_id = object.GetPrimaryM2InstanceId();
  if (instance_id == 0u || out_position == nullptr) {
    return false;
  }

  auto* const m2_system = object.m2_system();
  if (m2_system == nullptr) {
    return false;
  }

  const auto query = m2_system->QueryAttachmentPosition(
      instance_id, attachment_lookup_index);
  if (query.status != openwow::render::m2::M2ResultStatus::kReady) {
    return false;
  }

  out_position[0] = query.position[0];
  out_position[1] = query.position[1];
  out_position[2] = query.position[2];
  return true;
}

[[nodiscard]] bool QueryPrimaryM2ModelWorldPoint(
    const CGObject_C& object,
    float* const out_position) {
  const std::uint32_t instance_id = object.GetPrimaryM2InstanceId();
  if (instance_id == 0u || out_position == nullptr) {
    return false;
  }

  auto* const m2_system = object.m2_system();
  if (m2_system == nullptr) {
    return false;
  }

  const auto query = m2_system->QueryModelWorldPoint(instance_id);
  if (query.status != openwow::render::m2::M2ResultStatus::kReady) {
    return false;
  }

  out_position[0] = query.position[0];
  out_position[1] = query.position[1];
  out_position[2] = query.position[2];
  return true;
}

constexpr std::size_t kMaxObjectNotificationSlots = 16;

struct ObjectNotificationSlot {
  void (*callback)(CGObject_C* object, std::uint32_t notification_id);
};

ObjectNotificationSlot g_object_notification_slots[kMaxObjectNotificationSlots]{};
std::size_t g_object_notification_slot_count = 0;

void DispatchObjectNotification(CGObject_C* object,
                                std::uint32_t notification_id) {
  for (std::size_t i = 0; i < g_object_notification_slot_count; ++i) {
    auto* cb = g_object_notification_slots[i].callback;
    if (cb != nullptr) {
      cb(object, notification_id);
    }
  }
}

constexpr std::uint32_t kNotifyModelChanged = 150;

constexpr std::uint32_t kNotifyHighlightChanged = 636;

bool UsesPositionStyleCreateTransportAttachment(const TypeID type_id) {
  return type_id == TypeID::kDynamicObject || type_id == TypeID::kCorpse ||
         type_id == TypeID::kGameObject;
}

void FinalizePositionStyleCreateTransportAttachment(
    const ObjectManager* const objects,
    const TypeID type_id,
    MovementUpdate& position) {
  if (!UsesPositionStyleCreateTransportAttachment(type_id) ||
      !position.HasUpdateFlag(kUpdateFlagPosition) ||
      position.transport_guid.IsEmpty()) {
    return;
  }

  if (objects != nullptr &&
      Movement_ResolveAndCallVf244(
          *objects, 0, position.transport_guid.GetRawValue(), 1) != nullptr) {
    return;
  }

  position.transport_guid = ObjectGuid();
  position.position_x = position.transport_offset_x;
  position.position_y = position.transport_offset_y;
  position.position_z = position.transport_offset_z;
}

}

CGObject_C::CGObject_C(TypeID type_id) : type_id_(type_id) {
  speeds_.fill(0.0f);
  ClearObjectBoundingBox();
}

CGObject_C::CGObject_C(ObjectGuid guid, TypeID type_id)
    : guid_(guid), type_id_(type_id) {
  const auto count = FieldCountFor(type_id);
  fields_.resize(count, 0);
  dirty_mask_.resize((count + 31) / 32, 0);
  speeds_.fill(0.0f);
  ClearObjectBoundingBox();
}

CGObject_C::CGObject_C(ObjectManager& objects, TypeID type_id)
    : CGObject_C(type_id) {
  object_manager_ = &objects;
}

CGObject_C::CGObject_C(ObjectManager& objects, ObjectGuid guid, TypeID type_id)
    : CGObject_C(guid, type_id) {
  object_manager_ = &objects;
}

CGObject_C::~CGObject_C() {
  if (!guid_.IsEmpty()) {
    CEffect_C::DetachAllFromOwner(guid_);
  }
}

void CGObject_C::PrepareForWorldRemoval() {
  if (!guid_.IsEmpty()) {
    CEffect_C::DetachAllFromOwner(guid_);
  }
}

bool CGObject_C::IsActiveMover() const {
  const auto* const objects = object_manager();
  return objects != nullptr &&
         guid_ == objects->player_control().ActiveMoverGuid();
}

Position CGObject_C::GetPosition() const {
  Position position{GetX(), GetY(), GetZ(), GetOrientation()};

  ObjectGuid parent_guid;
  std::array<float, 3> local_position{};
  if (!TryGetRelativePosition(parent_guid, local_position)) {
    return position;
  }

  float world_position[3]{};
  const auto* const objects = object_manager();
  if (objects == nullptr) {
    return position;
  }
  Passenger_TransformLocalToWorldPosition(
      *objects, parent_guid.GetRawValue(), world_position,
      local_position.data());
  position.x = world_position[0];
  position.y = world_position[1];
  position.z = world_position[2];
  return position;
}

Position CGObject_C::GetRawPosition() const {
  constexpr std::uint16_t kUpdateFlagPosition = 0x100;
  if (position_.update_flags & kUpdateFlagPosition) {
    return {position_.transport_offset_x,
            position_.transport_offset_y,
            position_.transport_offset_z,
            position_.position_o};
  }
  return {GetX(), GetY(), GetZ(), GetOrientation()};
}

double CGObject_C::GetSquaredDistanceToPosition(const Position& position) const {
  const Position world_position = GetPosition();
  const double dx = static_cast<double>(world_position.x) - position.x;
  const double dy = static_cast<double>(world_position.y) - position.y;
  const double dz = static_cast<double>(world_position.z) - position.z;
  return dx * dx + dy * dy + dz * dz;
}

float CGObject_C::GetWorldFacing() const {
  const auto* const objects = object_manager();
  if (objects == nullptr) {
    return GetOrientation();
  }

  ObjectGuid parent_guid;
  std::array<float, 3> unused_local_position{};
  if (!TryGetRelativePosition(parent_guid, unused_local_position)) {

    return GetOrientation();
  }
  return Movement_TransformLocalFacingToWorld(*objects, parent_guid.GetRawValue(),
                                              GetLocalFacing());
}

void CGObject_C::GetObjectBoundingBox(float* out_bbox) const {
  if (out_bbox == nullptr) {
    return;
  }

  std::memcpy(out_bbox, object_bounding_box_.data(), sizeof(object_bounding_box_));
}

void CGObject_C::SetObjectBoundingBox(const float* bbox) {
  if (bbox == nullptr) {
    ClearObjectBoundingBox();
    return;
  }

  std::memcpy(object_bounding_box_.data(), bbox, sizeof(object_bounding_box_));
  has_object_bounding_box_ = true;
}

void CGObject_C::ClearObjectBoundingBox() {
  object_bounding_box_.fill(0.0f);
  has_object_bounding_box_ = false;
}

std::uint32_t CGObject_C::GetDisplayId() const {
  if (IsUnit()) return GetUInt32(UNIT_FIELD_DISPLAYID);
  if (IsGameObject()) return GetUInt32(GAMEOBJECT_DISPLAYID);
  return 0;
}

std::uint32_t CGObject_C::GetHealth() const {
  return IsUnit() ? GetUInt32(UNIT_FIELD_HEALTH) : 0;
}

std::uint32_t CGObject_C::GetMaxHealth() const {
  return IsUnit() ? GetUInt32(UNIT_FIELD_MAXHEALTH) : 0;
}

std::uint32_t CGObject_C::GetLevel() const {
  return IsUnit() ? GetUInt32(UNIT_FIELD_LEVEL) : 0;
}

std::uint16_t CGObject_C::AdjustLifetimeHold(const bool acquire) {
  if (!acquire || lifetime_hold_count_ == 0xFFFFu) {
    if (lifetime_hold_count_ != 0) {
      --lifetime_hold_count_;
    }
  } else {
    ++lifetime_hold_count_;
  }

  return lifetime_hold_count_;
}

const data::dbc::SpellVisualEntry* CGObject_C::ResolveSpellVisualRecord(
    const data::dbc::SpellEntry& spell,
    data::dbc::SpellVisualEntry& out,
    [[maybe_unused]] std::uint32_t kit_visual_id,
    [[maybe_unused]] std::uint32_t kit_visual_id_fallback) const {
  const auto* objects = object_manager();
  if (objects == nullptr) {
    return nullptr;
  }
  const auto* dbc = &objects->dbc_loader();

  auto visual_id = spell.spell_visual[0];
  const auto quality_level = static_cast<std::int32_t>(
      openwow::core::DisplaySettingsController::Instance().GetQualityLevel());
  if (quality_level < 2 && spell.spell_visual[1] != 0u) {
    visual_id = spell.spell_visual[1];
  }

  const auto* visual = visual_id != 0u
      ? dbc->spell_visual().LookupEntry(visual_id)
      : nullptr;
  if (visual == nullptr) {
    return nullptr;
  }

  out = *visual;
  return &out;
}

std::vector<std::uint16_t> CGObject_C::ApplyFieldValues(const UpdateFieldValues& field_data) {
  std::vector<std::uint16_t> updated_indices;
  ForEachAppliedUpdateField(field_data, [&](std::uint16_t field_index,
                                            std::uint32_t value_index) {
    if (value_index >= field_data.values.size() || field_index >= fields_.size()) {
      return;
    }

    const std::uint32_t new_val = field_data.values[value_index];
    if (fields_[field_index] != new_val) {
      fields_[field_index] = new_val;
      const std::uint32_t dblock = field_index / 32;
      const std::uint32_t dbit = field_index % 32;
      if (dblock < dirty_mask_.size()) {
        dirty_mask_[dblock] |= (1u << dbit);
      }
    }
    updated_indices.push_back(field_index);
  });
  return updated_indices;
}

std::vector<std::uint16_t> CGObject_C::ApplyCreateUpdate(const CreateObjectUpdate& upd) {
  guid_ = upd.guid;
  type_id_ = upd.type_id;
  fields_.resize(FieldCountFor(type_id_), 0);
  dirty_mask_.resize((FieldCountFor(type_id_) + 31) / 32, 0);
  position_ = upd.movement;
  object_time_offset_ms_ =
      upd.movement.HasUpdateFlag(kUpdateFlagTransport)
          ? upd.movement.transport_path_timer - upd.client_receive_tick_ms
          : 0u;
  FinalizePositionStyleCreateTransportAttachment(
      object_manager(), type_id_, position_);
  ClearObjectBoundingBox();

  if (upd.movement.IsLiving()) {
    speeds_ = upd.movement.speeds;
  }

  auto changed = ApplyFieldValues(upd.fields);

  if (!upd.defer_post_init) {
    CGObject_C::FinalizeCreateUpdate(upd);
  }
  return changed;
}

void CGObject_C::FinalizeCreateUpdate(const CreateObjectUpdate&) {

  SetOpacityTarget(GetModelOpacity(), ShouldFadeOnShow() ? 1000u : 0u);
  (void)UpdateOverlayModel();
}

void CGObject_C::FinalizeWorldPublication() {

  (void)UpdateOverlayModel();
}

void CGObject_C::FinalizePacketUpdatePromotion() {

  Show();
}

std::vector<std::uint16_t> CGObject_C::ApplyValuesUpdate(const ValuesUpdate& upd) {
  return ApplyFieldValues(upd.fields);
}

std::vector<std::uint16_t> CGObject_C::ApplyRawFieldValues(
    const UpdateFieldValues& field_data) {
  return ApplyFieldValues(field_data);
}

bool CGObject_C::ApplyMovementUpdate(const MovementOnlyUpdate& upd) {
  position_ = upd.movement;
  if (upd.movement.HasUpdateFlag(kUpdateFlagTransport)) {
    object_time_offset_ms_ =
        upd.movement.transport_path_timer - upd.client_receive_tick_ms;
  }
  if (upd.movement.IsLiving()) {
    speeds_ = upd.movement.speeds;
  }
  return true;
}

bool CGObject_C::TryGetRelativePosition(
    ObjectGuid& parent_guid,
    std::array<float, 3>& local_position) const {
  if (position_.IsLiving() && !position_.movement.transport.guid.IsEmpty()) {
    parent_guid = position_.movement.transport.guid;
    local_position = {position_.movement.transport.offset_x,
                      position_.movement.transport.offset_y,
                      position_.movement.transport.offset_z};
    return true;
  }

  if (position_.HasUpdateFlag(kUpdateFlagPosition) &&
      !position_.transport_guid.IsEmpty()) {
    parent_guid = position_.transport_guid;
    local_position = {position_.transport_offset_x,
                      position_.transport_offset_y,
                      position_.transport_offset_z};
    return true;
  }

  return false;
}

float CGObject_C::GetLocalFacing() const {
  if (position_.IsLiving() && !position_.movement.transport.guid.IsEmpty()) {
    return position_.movement.transport.offset_o;
  }
  if (position_.HasUpdateFlag(kUpdateFlagPosition) &&
      !position_.transport_guid.IsEmpty()) {
    return position_.position_o;
  }
  return GetOrientation();
}

void CGObject_C::SetActivePlayerGuid(const ObjectGuid& guid) {
  s_active_player_guid_ = guid;
}

ObjectGuid CGObject_C::GetActivePlayerGuid() {
  return s_active_player_guid_;
}

void CGObject_C::OnRightClickInteract(WorldSession*,
                                      TargetingSystem*) const {}

void CGObject_C::GetWorldMatrix(float* out_matrix) const {
  if (out_matrix == nullptr) {
    return;
  }

  if (GetVisualModelWorldTransform(out_matrix)) {
    return;
  }

  const Position position = GetPosition();
  const float facing = GetFacing();
  const float cosine = std::cos(facing);
  const float sine = std::sin(facing);
  out_matrix[0] = cosine;
  out_matrix[1] = sine;
  out_matrix[2] = 0.0f;
  out_matrix[3] = 0.0f;
  out_matrix[4] = -sine;
  out_matrix[5] = cosine;
  out_matrix[6] = 0.0f;
  out_matrix[7] = 0.0f;
  out_matrix[8] = 0.0f;
  out_matrix[9] = 0.0f;
  out_matrix[10] = 1.0f;
  out_matrix[11] = 0.0f;
  out_matrix[12] = position.x;
  out_matrix[13] = position.y;
  out_matrix[14] = position.z;
  out_matrix[15] = 1.0f;
}

bool CGObject_C::UpdateModelNodeTransform(float ,
                                          std::uint32_t ) {

  return true;
}

void CGObject_C::SetVisualModelWorldTransform(const float* const matrix) {
  if (matrix == nullptr) {
    ClearVisualModelWorldTransform();
    return;
  }

  std::memcpy(visual_model_world_transform_.data(), matrix,
              sizeof(float) * visual_model_world_transform_.size());
  has_visual_model_world_transform_ = true;
}

void CGObject_C::ClearVisualModelWorldTransform() {
  visual_model_world_transform_.fill(0.0f);
  has_visual_model_world_transform_ = false;
}

bool CGObject_C::BindObjectEffectPackage(const std::uint32_t package_id) {
  if (package_id == 0u) {
    object_effect_.reset();
    return false;
  }
  if (object_effect_ != nullptr &&
      object_effect_->GetBoundPackageId() == package_id) {
    return true;
  }

  auto replacement = std::make_unique<CObjectEffect>(sound_runtime());

  replacement->SetModifierInputResolver(
      [this]() -> float { return GetTypeHandlerAnimTime(); });
  if (!replacement->BindPackageAndApplyDefaultStates(package_id)) {
    object_effect_.reset();
    return false;
  }
  object_effect_ = std::move(replacement);
  return true;
}

void CGObject_C::ClearObjectEffectPackage() {
  object_effect_.reset();
}

bool CGObject_C::GetVisualModelWorldTransform(float* const out_matrix) const {
  if (!has_visual_model_world_transform_ || out_matrix == nullptr) {
    return false;
  }

  std::memcpy(out_matrix, visual_model_world_transform_.data(),
              sizeof(float) * visual_model_world_transform_.size());
  return true;
}

void CGObject_C::ApplyModelParentTransform(const float* const parent_matrix) {
  if (parent_matrix == nullptr) {
    return;
  }

  std::memcpy(model_parent_transform_.data(), parent_matrix,
              sizeof(float) * model_parent_transform_.size());
  has_model_parent_transform_ = true;
}

bool CGObject_C::GetModelParentTransform(float* const out_matrix) const {
  if (!has_model_parent_transform_ || out_matrix == nullptr) {
    return false;
  }

  std::memcpy(out_matrix, model_parent_transform_.data(),
              sizeof(float) * model_parent_transform_.size());
  return true;
}

void CGObject_C::QueryModelRebuildFlags(std::uint8_t flags,
                                        std::uint32_t& out_needs_construct,
                                        std::uint32_t& ) {
  if ((flags & 1) == 0) {
    out_needs_construct = 1;
  }
}

std::tuple<float, float, float, float>
CGObject_C::GetWorldRotation() const {
  const float facing = GetWorldFacing();

  const float half = facing * 0.5f;
  return {0.0f, 0.0f, std::sin(half), std::cos(half)};
}

void CGObject_C::DisableMouseoverHighlightAndNotify() {
  if (!mouseover_highlight_active_) {
    return;
  }
  mouseover_highlight_active_ = false;
  DispatchObjectNotification(this, kNotifyModelChanged);
  DispatchObjectNotification(this, kNotifyHighlightChanged);
}

CGObject_C::EffectNodeCreationResult CGObject_C::CreateSpellVisualEffectNode(
    const WorldSession& session,
    const std::uint32_t attachment_point,
    const std::uint32_t cleanup_tick,
    const std::uint32_t spell_id,
    const data::dbc::SpellVisualKitEntry* const kit,
    const data::dbc::SpellVisualEffectNameEntry* const effect_name,
    std::uint32_t& dispatch_flags,
    const std::uintptr_t animation_callback,
    const float* const position,
    const std::uint64_t source_guid,
    const std::uint32_t visual_kit_param,
    const std::uintptr_t transform_key,
    const std::array<float, 3>* const local_offset,
    const std::array<float, 3>* const local_rotation_degrees,
    const bool world_space) {
  EffectNodeCreationResult result{};
  result.node_spell_id = spell_id;

  const std::uint64_t owner_guid = guid_.GetRawValue();
  const std::uint64_t resolved_guid = (source_guid != 0u) ? source_guid : owner_guid;
  result.resolved_source_guid = resolved_guid;

  std::array<float, 3> explicit_position{};
  const std::array<float, 3>* explicit_position_ptr = nullptr;
  if (position != nullptr) {
    std::copy_n(position, explicit_position.size(), explicit_position.begin());
    explicit_position_ptr = &explicit_position;
  }

  CEffectCreateInfo create_info;
  create_info.owner = this;
  create_info.source_guid = ObjectGuid(resolved_guid);
  create_info.spell_id = spell_id;
  create_info.visual_kit = kit;
  create_info.effect_name = effect_name;
  create_info.attachment_point = static_cast<std::int32_t>(attachment_point);
  create_info.flags = dispatch_flags;
  create_info.visual_kit_param = visual_kit_param;
  create_info.transform_key = transform_key;
  create_info.cleanup_tick = cleanup_tick;
  create_info.position = explicit_position_ptr;
  create_info.local_offset =
      local_offset != nullptr ? *local_offset : std::array<float, 3>{};
  create_info.local_rotation_degrees =
      local_rotation_degrees != nullptr
          ? *local_rotation_degrees
          : std::array<float, 3>{};
  create_info.world_space = world_space;

  (void)animation_callback;

  auto* const node = CEffect_C::AddEffect(session, create_info);
  if (node == nullptr) {
    return result;
  }
  result.effect_id = node->Snapshot().effect_id;

  const bool should_hide = (dispatch_flags & kEffectFlagAuraVisual) != 0u;
  result.model_hidden = should_hide;
  if (should_hide) {
    node->SetPrimaryModelAlpha(0.0f);
  }

  const bool attached_model_selector =
      kit != nullptr &&
      (kit->flags & kKitFlagAttachedModelSelector) != 0u;
  if (attached_model_selector) {
    node->EnableAttachedModelSelector();
  }
  result.node_flags = node->GetFlags();
  result.attached_model_selector_flag_set = attached_model_selector;

  dispatch_flags = (dispatch_flags & ~kEffectFlagClearMask) | kEffectFlagCreated;
  result.updated_dispatch_flags = dispatch_flags;

  result.created = true;
  return result;
}

std::uint32_t CGObject_C::ResolveOverlayModelIndex() const {
  const auto type = static_cast<std::uint32_t>(overlay_display_type_);

  if (type == 0) {
    return overlay_model_index_override_;
  }

  if (type >= 2 && type <= 4) {
    if (TrackingSystem::Get().IsTrivialQuestTrackingActive()) {
      return type < kOverlayTypeToModelIndexCount ? kOverlayTypeToModelIndex[type] : 0;
    }
    return overlay_model_index_override_;
  }

  if (type < kOverlayTypeToModelIndexCount) {
    return kOverlayTypeToModelIndex[type];
  }

  return 0;
}

std::uint32_t CGObject_C::UpdateOverlayModel() {

  active_overlay_model_index_ = 0;
  overlay_model_scale_ = 1.0f;
  overlay_model_visible_ = false;
  overlay_bone_attached_ = false;
  overlay_bone_rotation_compensated_ = false;

  const std::uint32_t model_index = ResolveOverlayModelIndex();
  if (model_index == 0) {
    return 0;
  }

  active_overlay_model_index_ = model_index;
  overlay_model_visible_ = true;

  AttachOverlayModelToBone();

  return model_index;
}

void CGObject_C::AttachOverlayModelToBone() {
  if (active_overlay_model_index_ == 0 || primary_m2_instance_id_ == 0u) {
    return;
  }

  float attachment_position[3]{};
  if (!QueryPrimaryM2AttachmentPosition(
          *this, openwow::render::m2::kM2AttachmentLookupPlayerName,
          attachment_position)) {
    return;
  }

  overlay_bone_attached_ = true;
  RefreshOverlayBoneScale();
  SetIdleAnimation();
}

void CGObject_C::RefreshOverlayBoneScale() {
  if (active_overlay_model_index_ == 0u) {
    return;
  }
  overlay_model_scale_ = GetNameplateHeight();
}

void CGObject_C::SetQuestGiverIconStatus(OverlayDisplayType new_type) {
  const auto old_type = overlay_display_type_;
  const auto new_raw = static_cast<std::uint32_t>(new_type);
  const auto old_raw = static_cast<std::uint32_t>(old_type);

  const auto new_idx = new_raw < kOverlayTypeToModelIndexCount
                           ? kOverlayTypeToModelIndex[new_raw]
                           : 0u;

  if (new_type == old_type && !(new_idx != 0 && !overlay_model_visible_))
    return;

  const auto old_idx = old_raw < kOverlayTypeToModelIndexCount
                           ? kOverlayTypeToModelIndex[old_raw]
                           : 0u;

  const bool needs_update =
      (new_idx != old_idx) || (new_idx != 0 && !overlay_model_visible_);

  overlay_display_type_ = new_type;

  if (needs_update) {
    UpdateOverlayModel();
  }
}

void CGObject_C::SetIdleAnimation() {

  if (active_overlay_model_index_ == 0) {
    return;
  }

  if (render_object_has_sleep_flag_) {
    overlay_animation_id_ = 190;
  } else {
    overlay_animation_id_ = 0;
  }
}

void CGObject_C::ClearOverlayModelImmediate() {
  overlay_display_type_ = OverlayDisplayType::kNone;
  overlay_model_index_override_ = 0;
  active_overlay_model_index_ = 0;
  overlay_model_visible_ = false;
  overlay_bone_attached_ = false;
  overlay_bone_rotation_compensated_ = false;
}

void CGObject_C::ClearAllSlotBucketArrays() {
  switch (type_id_) {
    case TypeID::kItem:

      CMirrorHandler_ClearSlotBucketArray(
          kMirrorItemSectionSlots,
          mirror_derived_section_buckets_.data());
      CMirrorHandler_ClearSlotBucketArray(
          kMirrorObjectSectionSlots,
          mirror_object_buckets_.data());
      break;

    case TypeID::kContainer:

      CMirrorHandler_ClearSlotBucketArray(
          kMirrorContainerSectionSlots,
          mirror_extra_section_buckets_.data());
      CMirrorHandler_ClearSlotBucketArray(
          kMirrorItemSectionSlots,
          mirror_derived_section_buckets_.data());
      CMirrorHandler_ClearSlotBucketArray(
          kMirrorObjectSectionSlots,
          mirror_object_buckets_.data());
      break;

    case TypeID::kUnit:

      CMirrorHandler_ClearSlotBucketArray(
          kMirrorUnitSectionSlots,
          mirror_derived_section_buckets_.data());
      CMirrorHandler_ClearSlotBucketArray(
          kMirrorObjectSectionSlots,
          mirror_object_buckets_.data());
      break;

    case TypeID::kPlayer:

      CMirrorHandler_ClearSlotBucketArray(
          kMirrorPlayerVisibleSlots,
          mirror_extra_section_buckets_.data());
      if (IsActivePlayer()) {
        CMirrorHandler_ClearSlotBucketArray(
            kMirrorPlayerActiveSlots,
            mirror_active_player_buckets_.data());
      }
      CMirrorHandler_ClearSlotBucketArray(
          kMirrorUnitSectionSlots,
          mirror_derived_section_buckets_.data());
      CMirrorHandler_ClearSlotBucketArray(
          kMirrorObjectSectionSlots,
          mirror_object_buckets_.data());
      break;

    case TypeID::kGameObject:

      CMirrorHandler_ClearSlotBucketArray(
          kMirrorGameObjectSectionSlots,
          mirror_derived_section_buckets_.data());
      CMirrorHandler_ClearSlotBucketArray(
          kMirrorObjectSectionSlots,
          mirror_object_buckets_.data());
      break;

    case TypeID::kDynamicObject:

      CMirrorHandler_ClearSlotBucketArray(
          kMirrorDynObjSectionSlots,
          mirror_derived_section_buckets_.data());
      CMirrorHandler_ClearSlotBucketArray(
          kMirrorObjectSectionSlots,
          mirror_object_buckets_.data());
      break;

    case TypeID::kCorpse:

      CMirrorHandler_ClearSlotBucketArray(
          kMirrorCorpseSectionSlots,
          mirror_derived_section_buckets_.data());
      CMirrorHandler_ClearSlotBucketArray(
          kMirrorObjectSectionSlots,
          mirror_object_buckets_.data());
      break;

    default:

      CMirrorHandler_ClearSlotBucketArray(
          kMirrorObjectSectionSlots,
          mirror_object_buckets_.data());
      break;
  }
}

void CGObject_C::SetDisplayScale(
    const float scale, openwow::world::WorldCamera* const camera) {
  display_scale_update_tick_ = core::GameClock::GetTickCount32();
  display_scale_stored_ = scale;
  native_scale_ = scale;

  if (camera != nullptr &&
      camera->bound_object() == guid_.GetRawValue()) {
    ui::game::GameUI_GetUnitModelDisplay(guid_.GetRawValue());
  }
}

void CGObject_C::Show(const bool fade) {
  SetOpacityTarget(GetModelOpacity(), fade && ShouldFadeOnShow() ? 1000u : 0u);
}

void CGObject_C::RefreshRenderedOpacity() {
  rendered_opacity_ = static_cast<float>(
      static_cast<double>(opacity_master_)
      * static_cast<double>(opacity_current_)
      * kOpacityByteToCompositeAlpha);
}

float CGObject_C::GetEffectiveRenderOpacity() const {
  return rendered_opacity_;
}

void CGObject_C::SetOpacityMaster(const std::uint8_t alpha) {
  if (opacity_master_ == alpha) {
    return;
  }
  opacity_master_ = alpha;

  RefreshRenderedOpacity();
}

void CGObject_C::AnimateOpacityTransition(const std::uint32_t now_ms) {
  if (opacity_fade_duration_ > 0) {
    const auto elapsed =
        static_cast<std::int32_t>(now_ms - opacity_fade_start_time_);
    if (elapsed >= opacity_fade_duration_) {
      opacity_current_       = opacity_fade_end_;
      opacity_fade_duration_ = 0;
    } else {
      const int delta = static_cast<int>(opacity_fade_end_)
                      - static_cast<int>(opacity_fade_start_);
      opacity_current_ = static_cast<std::uint8_t>(
          static_cast<int>(opacity_fade_start_)
          + elapsed * delta / opacity_fade_duration_);
    }
  }

  RefreshRenderedOpacity();
}

void CGObject_C::SetOpacityTarget(const float target,
                                  const std::uint32_t duration_ms) {
  const auto target_byte =
      static_cast<std::uint8_t>(std::lrintf(target * 255.0f));

  if (opacity_fade_duration_ > 0 && opacity_fade_end_ == target_byte &&
      opacity_fade_duration_ == static_cast<std::int32_t>(duration_ms)) {
    return;
  }

  if (target_byte == opacity_current_) {
    opacity_fade_end_      = target_byte;
    opacity_fade_duration_ = 0;
    opacity_current_       = target_byte;
    RefreshRenderedOpacity();
    return;
  }

  opacity_fade_start_time_ = core::GameClock::GetTickCount32();
  opacity_fade_duration_   = static_cast<std::int32_t>(duration_ms);
  opacity_fade_start_      = opacity_current_;
  opacity_fade_end_        = target_byte;
  if (duration_ms == 0) {
    opacity_current_ = target_byte;
    opacity_fade_duration_ = 0;
  }
  RefreshRenderedOpacity();
}

float CGObject_C::GetNameplateHeight() const {
  float height;

  if (IsUnit()) {

    height = static_cast<const CGUnit_C*>(this)->Presentation().ModelHeight();
  } else {
    float bone_height = 0.0f;
    float att_pos[3]{};
    float origin[3]{};
    if (QueryPrimaryM2AttachmentPosition(
            *this, openwow::render::m2::kM2AttachmentLookupPlayerName, att_pos) &&
        QueryPrimaryM2ModelWorldPoint(*this, origin)) {
      bone_height = att_pos[2] - origin[2];
    }
    if (bone_height > 0.0f) {
      height = bone_height;
    } else {

      height = model_bounding_box_height_ * native_scale_ * 1.25f;
    }
  }

  if (height <= 4.0f) {
    return 1.0f;
  }
  return height * 0.25f * 1.5f;
}

Position CGObject_C::GetNamePlatePosition() const {
  float att_pos[3]{};
  if (QueryPrimaryM2AttachmentPosition(
          *this, openwow::render::m2::kM2AttachmentLookupPlayerName, att_pos)) {
    return {att_pos[0], att_pos[1], att_pos[2], GetOrientation()};
  }

  auto pos = GetPosition();
  pos.z += model_bounding_box_height_ * native_scale_ * 1.25f;
  return pos;
}

}
