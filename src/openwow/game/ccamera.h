
#pragma once

#include <array>
#include <cstdint>

namespace openwow::game {

using CameraScalarCallback = void (*)(float time, int context, float* value);
using CameraVectorCallback = void (*)(float time, int context, float* value);

enum class CameraChannelFlag : std::uint8_t {
    kScheduled = 0x01,
    kCallbackOwned = 0x02,
    kCallbackQueued = 0x04,
    kDirty = 0x08,
};

constexpr CameraChannelFlag operator|(CameraChannelFlag a, CameraChannelFlag b) {
    return static_cast<CameraChannelFlag>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

struct CameraChannelState {
    std::uint8_t kind = 0;
    std::uint8_t flags = 0;
    std::uint16_t reserved = 0;
    int callback_context = 0;
    float callback_time = 0.0f;

    [[nodiscard]] bool HasFlag(CameraChannelFlag flag) const;
    void ClearFlag(CameraChannelFlag flag);
    void SetFlag(CameraChannelFlag flag);
    void ResetRuntimeState();
};

struct CameraChannelBase;

struct CameraScheduledChannelList {
    void InsertByCallbackTime(CameraChannelBase& channel);
    void Unlink(CameraChannelBase& channel);

    [[nodiscard]] bool Empty() const;
    [[nodiscard]] const CameraChannelBase* Front() const;
    [[nodiscard]] bool Contains(const CameraChannelBase& channel) const;

private:
    friend struct CameraChannelBase;

    CameraChannelBase* head_ = nullptr;
    CameraChannelBase* tail_ = nullptr;
};

struct CameraChannelBase {
    explicit CameraChannelBase(std::uint8_t kind = 0);
    ~CameraChannelBase();

    CameraChannelBase(const CameraChannelBase&) = delete;
    CameraChannelBase& operator=(const CameraChannelBase&) = delete;

    void ResetBaseState(std::uint8_t kind);
    void DetachScheduledCallback();

    [[nodiscard]] bool IsScheduled() const;

    CameraChannelState state{};

private:
    friend struct CameraScheduledChannelList;

    CameraScheduledChannelList* schedule_owner_ = nullptr;
    CameraChannelBase* scheduled_prev_ = nullptr;
    CameraChannelBase* scheduled_next_ = nullptr;
};

struct CameraScalarChannel : CameraChannelBase {
    CameraScalarChannel();

    CameraScalarCallback callback = nullptr;
    float value = 0.0f;

    void FireCallback(float time);
    void ValidateCallback(float time);
    void ResetCallbackState();
    void SetValue(float new_value);
};

struct CameraAngleChannel : CameraChannelBase {
    CameraAngleChannel();

    CameraScalarCallback callback = nullptr;
    float angle = 0.0f;
    float cosine = 1.0f;
    float sine = 0.0f;

    void FireCallback(float time);
    void ValidateCallback(float time);
    void ResetCallbackState();
    void SetValue(float new_angle);
};

struct CameraVectorChannel : CameraChannelBase {
    CameraVectorChannel();

    CameraVectorCallback callback = nullptr;
    std::array<float, 3> value{};

    void FireCallback(float time);
    void ValidateCallback(float time);
    void ResetCallbackState();
    void SetValue(const std::array<float, 3>& new_value);
};

struct CCamera {
    CCamera();

    void* storm_vtable = nullptr;
    std::uint32_t storm_ref_count = 0;
    CameraScheduledChannelList scheduled_channels{};
    std::array<CameraChannelBase*, 9> ordered_channels{};
    std::array<CameraVectorChannel, 2> vector_channels{};
    std::array<CameraScalarChannel, 3> scalar_channels{};
    std::array<CameraAngleChannel, 4> angle_channels{};

    void Reset();
};

std::uintptr_t CCamera_Create();
std::uintptr_t CCamera_Clone(std::uintptr_t source_camera);
void M2CameraAccessorGetVec3Property(std::uintptr_t camera,
                                       std::uint32_t property_index,
                                       float* out_value,
                                       int expected_kind);
void M2CameraAccessorSetVec3Masked(std::uintptr_t camera,
                                     std::uint32_t property_index,
                                     const float* value_xyz,
                                     std::uint8_t preserve_mask);
void M2CameraAccessorSetFloat(std::uintptr_t camera,
                                std::uint32_t property_index,
                                float value);

}
