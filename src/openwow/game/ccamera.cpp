
#include "openwow/game/ccamera.h"

#include "openwow/core/storm_error.h"
#include "openwow/core/storm_ref_counted.h"
#include "openwow/core/storm_string.h"
#include "openwow/foundation/math/vec3_exact_compare.h"

#include <cstddef>
#include <cmath>
#include <new>

namespace openwow::game {

namespace {

constexpr int kErrorInvalidParameter = 87;
constexpr std::uint8_t kVectorChannelKind = 3;
constexpr std::uint8_t kScalarChannelKind = 6;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kReciprocalTwoPi = 1.0f / kTwoPi;
constexpr float kHalfPi = 1.57079632679489661923f;

using CameraDestroyMethod = int (*)(void*, int);

int DestroyCamera(void* object, int release_flag) {
    auto* camera = static_cast<CCamera*>(object);
    camera->~CCamera();
    if ((release_flag & 1) != 0) {
        openwow::core::SMemFree(camera, "delete", -1, 0);
    }
    return 0;
}

CameraDestroyMethod kCameraStormVTable[] = {&DestroyCamera};

float NormalizeAnglePositiveTurn(float angle) {

    const int whole_turns = static_cast<int>(kReciprocalTwoPi * angle);
    float normalized = angle - static_cast<float>(whole_turns) * kTwoPi;
    if (angle < 0.0f) {
        normalized += kTwoPi;
    }
    return normalized;
}

bool VectorValuesMatch(const std::array<float, 3>& lhs,
                       const std::array<float, 3>& rhs) {
    return openwow::math::vec3::AllComponentsEqual(lhs.data(), rhs.data());
}

CCamera* AllocateCamera() {
    void* storage = openwow::core::SMemAlloc(sizeof(CCamera), "HCAMERA", -2, 0);
    if (!storage) {
        return nullptr;
    }

    return new (storage) CCamera();
}

void SetInvalidParameterError() {
    openwow::core::SErrSetLastError(kErrorInvalidParameter);
}

CCamera* ResolveCamera(std::uintptr_t camera) {
    return reinterpret_cast<CCamera*>(camera);
}

CameraChannelBase* ResolveChannel(CCamera* camera,
                                  std::uint32_t property_index,
                                  std::uint8_t expected_kind) {
    if (!camera || property_index >= camera->ordered_channels.size()) {
        SetInvalidParameterError();
        return nullptr;
    }

    CameraChannelBase* channel = camera->ordered_channels[property_index];
    if (!channel || channel->state.kind != expected_kind) {
        SetInvalidParameterError();
        return nullptr;
    }

    return channel;
}

template <typename ChannelType>
void ResolveQueuedRead(ChannelType& channel) {
    if (!channel.state.HasFlag(CameraChannelFlag::kCallbackQueued)) {
        return;
    }

    if (channel.state.HasFlag(CameraChannelFlag::kCallbackOwned)) {
        channel.FireCallback(0.0f);
        return;
    }

    channel.ValidateCallback(0.0f);
}

CameraScalarChannel* ResolveScalarChannelBySlot(CCamera& camera,
                                                std::uint32_t property_index) {
    switch (property_index) {
        case 1:
            return &camera.scalar_channels[0];
        case 2:
            return &camera.scalar_channels[1];
        case 3:
            return &camera.scalar_channels[2];
        default:
            return nullptr;
    }
}

CameraAngleChannel* ResolveAngleChannelBySlot(CCamera& camera,
                                              std::uint32_t property_index) {
    switch (property_index) {
        case 0:
            return &camera.angle_channels[0];
        case 4:
            return &camera.angle_channels[1];
        case 5:
            return &camera.angle_channels[2];
        case 6:
            return &camera.angle_channels[3];
        default:
            return nullptr;
    }
}

void BindVectorChannelSlot(CCamera& camera,
                           CameraVectorChannel& channel,
                           std::size_t slot_index,
                           std::uint8_t flags) {
    channel.state.kind = kVectorChannelKind;
    channel.state.flags = flags;
    camera.ordered_channels[slot_index] = &channel;
    if ((flags & static_cast<std::uint8_t>(CameraChannelFlag::kScheduled)) != 0u) {
        camera.scheduled_channels.InsertByCallbackTime(channel);
    }
}

void BindScalarChannelSlot(CCamera& camera,
                           CameraChannelBase& channel,
                           std::size_t slot_index,
                           std::uint8_t flags) {
    channel.state.kind = kScalarChannelKind;
    channel.state.flags = flags;
    camera.ordered_channels[slot_index] = &channel;
    if ((flags & static_cast<std::uint8_t>(CameraChannelFlag::kScheduled)) != 0u) {
        camera.scheduled_channels.InsertByCallbackTime(channel);
    }
}

void InitializeCameraCtorState(CCamera& camera) {
    camera.storm_vtable = kCameraStormVTable;
    camera.storm_ref_count = 0;
    camera.ordered_channels.fill(nullptr);

    for (auto& channel : camera.vector_channels) {
        channel.ResetBaseState(kVectorChannelKind);
        channel.ResetCallbackState();
        channel.value = {0.0f, 0.0f, 0.0f};
    }
    camera.vector_channels[0].value = {100.0f, 0.0f, 0.0f};

    for (auto& channel : camera.scalar_channels) {
        channel.ResetBaseState(kScalarChannelKind);
        channel.ResetCallbackState();
        channel.value = 0.0f;
    }
    camera.scalar_channels[0].value = 100.0f;
    camera.scalar_channels[1].value = 5000.0f;
    camera.scalar_channels[2].value = 8.0f;

    for (auto& channel : camera.angle_channels) {
        channel.ResetBaseState(kScalarChannelKind);
        channel.ResetCallbackState();
        channel.angle = 0.0f;
        channel.cosine = 1.0f;
        channel.sine = 0.0f;
    }
    camera.angle_channels[1].SetValue(kHalfPi);

    BindVectorChannelSlot(camera, camera.vector_channels[0], 7, 0);
    BindVectorChannelSlot(camera, camera.vector_channels[1], 8, 0);
    BindScalarChannelSlot(camera, camera.scalar_channels[0], 1, 0);
    BindScalarChannelSlot(camera, camera.scalar_channels[1], 2, 0);
    BindScalarChannelSlot(camera, camera.scalar_channels[2], 3, 0);
    BindScalarChannelSlot(camera, camera.angle_channels[0], 0, 0);
    BindScalarChannelSlot(camera, camera.angle_channels[1], 4, 0);
    BindScalarChannelSlot(camera, camera.angle_channels[2], 5, 0);
    BindScalarChannelSlot(camera, camera.angle_channels[3], 6, 0);
}

}

bool CameraChannelState::HasFlag(CameraChannelFlag flag) const {
    return (flags & static_cast<std::uint8_t>(flag)) != 0;
}

void CameraChannelState::ClearFlag(CameraChannelFlag flag) {
    flags &= static_cast<std::uint8_t>(~static_cast<std::uint8_t>(flag));
}

void CameraChannelState::SetFlag(CameraChannelFlag flag) {
    flags |= static_cast<std::uint8_t>(flag);
}

void CameraChannelState::ResetRuntimeState() {
    callback_context = 0;
    callback_time = 0.0f;
}

void CameraScheduledChannelList::InsertByCallbackTime(CameraChannelBase& channel) {
    channel.DetachScheduledCallback();

    CameraChannelBase* insert_before = head_;
    while (insert_before &&
           insert_before->state.callback_time > channel.state.callback_time) {
        insert_before = insert_before->scheduled_next_;
    }

    channel.schedule_owner_ = this;
    if (!insert_before) {
        channel.scheduled_prev_ = tail_;
        channel.scheduled_next_ = nullptr;
        if (tail_) {
            tail_->scheduled_next_ = &channel;
        } else {
            head_ = &channel;
        }
        tail_ = &channel;
        return;
    }

    channel.scheduled_next_ = insert_before;
    channel.scheduled_prev_ = insert_before->scheduled_prev_;
    if (insert_before->scheduled_prev_) {
        insert_before->scheduled_prev_->scheduled_next_ = &channel;
    } else {
        head_ = &channel;
    }
    insert_before->scheduled_prev_ = &channel;
}

void CameraScheduledChannelList::Unlink(CameraChannelBase& channel) {
    channel.DetachScheduledCallback();
}

bool CameraScheduledChannelList::Empty() const {
    return head_ == nullptr;
}

const CameraChannelBase* CameraScheduledChannelList::Front() const {
    return head_;
}

bool CameraScheduledChannelList::Contains(const CameraChannelBase& channel) const {
    const CameraChannelBase* current = head_;
    while (current) {
        if (current == &channel) {
            return true;
        }
        current = current->scheduled_next_;
    }
    return false;
}

CameraChannelBase::CameraChannelBase(std::uint8_t kind) {
    ResetBaseState(kind);
}

CameraChannelBase::~CameraChannelBase() {
    DetachScheduledCallback();
}

void CameraChannelBase::ResetBaseState(std::uint8_t kind) {
    DetachScheduledCallback();
    state.kind = kind;
    state.flags = 0;
    state.reserved = 0;
    state.ResetRuntimeState();
}

void CameraChannelBase::DetachScheduledCallback() {
    if (!schedule_owner_) {
        return;
    }

    if (scheduled_prev_) {
        scheduled_prev_->scheduled_next_ = scheduled_next_;
    } else {
        schedule_owner_->head_ = scheduled_next_;
    }

    if (scheduled_next_) {
        scheduled_next_->scheduled_prev_ = scheduled_prev_;
    } else {
        schedule_owner_->tail_ = scheduled_prev_;
    }

    schedule_owner_ = nullptr;
    scheduled_prev_ = nullptr;
    scheduled_next_ = nullptr;
}

bool CameraChannelBase::IsScheduled() const {
    return schedule_owner_ != nullptr;
}

CameraScalarChannel::CameraScalarChannel()
    : CameraChannelBase(kScalarChannelKind) {}

void CameraScalarChannel::FireCallback(float time) {
    if (callback) {
        float next_value = value;
        callback(time, state.callback_context, &next_value);
        SetValue(next_value);
    }
    state.ClearFlag(CameraChannelFlag::kCallbackQueued);
}

void CameraScalarChannel::ValidateCallback(float time) {
    if (!callback) {
        state.ClearFlag(CameraChannelFlag::kCallbackQueued);
        return;
    }

    float next_value = value;
    callback(time, state.callback_context, &next_value);
    if (next_value == value) {
        state.ClearFlag(CameraChannelFlag::kCallbackQueued);
        return;
    }

    openwow::core::SErrSetLastError(kErrorInvalidParameter);
}

void CameraScalarChannel::ResetCallbackState() {
    state.ResetRuntimeState();
    callback = nullptr;
}

void CameraScalarChannel::SetValue(float new_value) {
    if (new_value != value) {
        value = new_value;
        state.SetFlag(CameraChannelFlag::kDirty);
    }
}

CameraAngleChannel::CameraAngleChannel()
    : CameraChannelBase(kScalarChannelKind) {}

void CameraAngleChannel::FireCallback(float time) {
    if (callback) {
        float next_angle = angle;
        callback(time, state.callback_context, &next_angle);
        SetValue(next_angle);
    }
    state.ClearFlag(CameraChannelFlag::kCallbackQueued);
}

void CameraAngleChannel::ValidateCallback(float time) {
    if (!callback) {
        state.ClearFlag(CameraChannelFlag::kCallbackQueued);
        return;
    }

    float next_angle = angle;
    callback(time, state.callback_context, &next_angle);
    if (next_angle == angle) {
        state.ClearFlag(CameraChannelFlag::kCallbackQueued);
        return;
    }

    openwow::core::SErrSetLastError(kErrorInvalidParameter);
}

void CameraAngleChannel::ResetCallbackState() {
    state.ResetRuntimeState();
    callback = nullptr;
}

void CameraAngleChannel::SetValue(float new_angle) {
    const float wrapped_angle = NormalizeAnglePositiveTurn(new_angle);
    if (wrapped_angle != angle) {
        angle = wrapped_angle;
        state.SetFlag(CameraChannelFlag::kDirty);
    }

    cosine = std::cos(angle);
    sine = std::sin(angle);
}

CameraVectorChannel::CameraVectorChannel()
    : CameraChannelBase(kVectorChannelKind) {}

void CameraVectorChannel::FireCallback(float time) {
    if (callback) {
        std::array<float, 3> next_value = value;
        callback(time, state.callback_context, next_value.data());
        SetValue(next_value);
    }
    state.ClearFlag(CameraChannelFlag::kCallbackQueued);
}

void CameraVectorChannel::ValidateCallback(float time) {
    if (!callback) {
        state.ClearFlag(CameraChannelFlag::kCallbackQueued);
        return;
    }

    std::array<float, 3> next_value = value;
    callback(time, state.callback_context, next_value.data());
    if (VectorValuesMatch(next_value, value)) {
        state.ClearFlag(CameraChannelFlag::kCallbackQueued);
        return;
    }

    openwow::core::SErrSetLastError(kErrorInvalidParameter);
}

void CameraVectorChannel::ResetCallbackState() {
    state.ResetRuntimeState();
    callback = nullptr;
}

void CameraVectorChannel::SetValue(const std::array<float, 3>& new_value) {
    if (!VectorValuesMatch(new_value, value)) {
        value = new_value;
        state.SetFlag(CameraChannelFlag::kDirty);
    }
}

CCamera::CCamera() {
    InitializeCameraCtorState(*this);
}

void CCamera::Reset() {
    InitializeCameraCtorState(*this);
}

std::uintptr_t CCamera_Create() {
    CCamera* camera = AllocateCamera();
    if (!camera) {
        return 0;
    }
    return openwow::core::StormRefCounted_AddRefChecked(camera);
}

std::uintptr_t CCamera_Clone(std::uintptr_t source_camera) {
    if (source_camera == 0) {
        openwow::core::SErrSetLastError(kErrorInvalidParameter);
        return 0;
    }

    auto* source = reinterpret_cast<const CCamera*>(source_camera);
    CCamera* clone = AllocateCamera();
    if (!clone) {
        return 0;
    }

    for (std::size_t i = 0; i < clone->vector_channels.size(); ++i) {
        clone->vector_channels[i].ResetCallbackState();
        clone->vector_channels[i].SetValue(source->vector_channels[i].value);
    }
    for (std::size_t i = 0; i < clone->scalar_channels.size(); ++i) {
        clone->scalar_channels[i].ResetCallbackState();
        clone->scalar_channels[i].SetValue(source->scalar_channels[i].value);
    }
    for (std::size_t i = 0; i < clone->angle_channels.size(); ++i) {
        clone->angle_channels[i].ResetCallbackState();
        clone->angle_channels[i].SetValue(source->angle_channels[i].angle);
    }

    return openwow::core::StormRefCounted_AddRefChecked(clone);
}

void M2CameraAccessorGetVec3Property(std::uintptr_t camera,
                                       std::uint32_t property_index,
                                       float* out_value,
                                       int expected_kind) {
    if (!out_value) {
        SetInvalidParameterError();
        return;
    }

    auto* camera_object = ResolveCamera(camera);
    auto* base = ResolveChannel(camera_object, property_index,
                                static_cast<std::uint8_t>(expected_kind));
    if (!base) {
        return;
    }

    auto& channel = *static_cast<CameraVectorChannel*>(base);
    ResolveQueuedRead(channel);

    out_value[0] = channel.value[0];
    out_value[1] = channel.value[1];
    out_value[2] = channel.value[2];
}

void M2CameraAccessorSetVec3Masked(std::uintptr_t camera,
                                     std::uint32_t property_index,
                                     const float* value_xyz,
                                     std::uint8_t preserve_mask) {
    float merged_value[3] = {0.0f, 0.0f, 0.0f};
    M2CameraAccessorGetVec3Property(camera, property_index, merged_value,
                                      kVectorChannelKind);

    if ((preserve_mask & 0x01u) == 0) {
        merged_value[0] = value_xyz[0];
    }
    if ((preserve_mask & 0x02u) == 0) {
        merged_value[1] = value_xyz[1];
    }
    if ((preserve_mask & 0x04u) == 0) {
        merged_value[2] = value_xyz[2];
    }

    auto* camera_object = ResolveCamera(camera);
    auto* base = ResolveChannel(camera_object, property_index, kVectorChannelKind);
    if (!base) {
        return;
    }

    if (base->state.HasFlag(CameraChannelFlag::kCallbackOwned)) {
        SetInvalidParameterError();
        return;
    }

    auto& channel = *static_cast<CameraVectorChannel*>(base);
    channel.state.ResetRuntimeState();
    channel.SetValue({merged_value[0], merged_value[1], merged_value[2]});
}

void M2CameraAccessorSetFloat(std::uintptr_t camera,
                                std::uint32_t property_index,
                                float value) {
    auto* camera_object = ResolveCamera(camera);
    auto* base = ResolveChannel(camera_object, property_index, kScalarChannelKind);
    if (!base) {
        return;
    }

    if (base->state.HasFlag(CameraChannelFlag::kCallbackOwned)) {
        SetInvalidParameterError();
        return;
    }

    base->state.ResetRuntimeState();

    if (auto* scalar = ResolveScalarChannelBySlot(*camera_object, property_index)) {
        scalar->SetValue(value);
        return;
    }

    if (auto* angle = ResolveAngleChannelBySlot(*camera_object, property_index)) {
        angle->SetValue(value);
        return;
    }

    SetInvalidParameterError();
}

}
