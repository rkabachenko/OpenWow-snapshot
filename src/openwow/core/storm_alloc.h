
#pragma once

#include <cstddef>
#include <cstdint>

namespace openwow::core {

void* Prop_Alloc();

void Storm_OperatorDelete(void* block);

void FrameXML_OperatorDelete(void* block);

class LegacyResizableBufferView {
 public:
    explicit LegacyResizableBufferView(void* storage);

    [[nodiscard]] uint32_t capacity() const;
    [[nodiscard]] uint32_t count() const;
    [[nodiscard]] void* data() const;

    void set_capacity(uint32_t value) const;
    void set_count(uint32_t value) const;
    void set_data(void* value) const;

 private:
    [[nodiscard]] uint32_t LoadU32(std::size_t offset) const;
    void StoreU32(std::size_t offset, uint32_t value) const;

    std::byte* storage_ = nullptr;
};

void* ResizeLegacyArrayStoragePreservingPrefix(
    LegacyResizableBufferView buffer, uint32_t new_capacity,
    std::size_t element_size, const char* debug_tag);

inline void* TSGrowableArray_4Byte_SetCapacity_47CCC0(
    LegacyResizableBufferView buffer, uint32_t new_capacity,
    const char* debug_tag = "pa<x>") {
    return ResizeLegacyArrayStoragePreservingPrefix(
        buffer, new_capacity, sizeof(uint32_t), debug_tag);
}

inline constexpr char kFramePriorityPointerStormTypeTag[] = ".PAVFRAMEPRIORITY@@";
inline constexpr char kVehiclePassengerPointerStormTypeTag[] =
    ".PAVCVehiclePassenger_C@@";

inline void* TSGrowableArray_PAVFRAMEPRIORITY_SetCapacityPreservingPrefix(
    LegacyResizableBufferView buffer, uint32_t new_capacity) {
    return ResizeLegacyArrayStoragePreservingPrefix(
        buffer, new_capacity, sizeof(std::uint32_t),
        kFramePriorityPointerStormTypeTag);
}

inline void* TSGrowableArray_PAVCVehiclePassenger_C_SetCapacityPreservingPrefix(
    LegacyResizableBufferView buffer, uint32_t new_capacity) {
    return ResizeLegacyArrayStoragePreservingPrefix(
        buffer, new_capacity, sizeof(std::uint32_t),
        kVehiclePassengerPointerStormTypeTag);
}

inline constexpr char kSkillLineAbilityRecPointerStormTypeTag[] =
    ".PBVSkillLineAbilityRec@@";

inline void*
TSGrowableArray_PBVSkillLineAbilityRec_SetCapacityPreservingPrefix(
    LegacyResizableBufferView buffer, uint32_t new_capacity) {
    return ResizeLegacyArrayStoragePreservingPrefix(
        buffer, new_capacity, sizeof(std::uint32_t),
        kSkillLineAbilityRecPointerStormTypeTag);
}

inline constexpr char kTSGrowableArrayUint16StormTypeTag[] = ".G";

inline void* TSGrowableArray_uint16_SetCapacityPreservingPrefix(
    LegacyResizableBufferView buffer, uint32_t new_capacity) {
    return ResizeLegacyArrayStoragePreservingPrefix(
        buffer, new_capacity, sizeof(std::uint16_t),
        kTSGrowableArrayUint16StormTypeTag);
}

inline void* TSGrowableArray_8Byte_SetCapacityPreservingPrefix(
    LegacyResizableBufferView buffer, uint32_t new_capacity,
    const char* debug_tag) {
    return ResizeLegacyArrayStoragePreservingPrefix(
        buffer, new_capacity, sizeof(std::uint64_t), debug_tag);
}

inline constexpr char kC2iVectorStormTypeTag[] = ".?AVC2iVector@NTempest@@";
inline constexpr char kCImVectorStormTypeTag[] = ".?AVCImVector@NTempest@@";

inline void* TSGrowableArray_C2iVector_SetCapacityPreservingPrefix(
    LegacyResizableBufferView buffer, uint32_t new_capacity) {
    return ResizeLegacyArrayStoragePreservingPrefix(
        buffer, new_capacity, sizeof(std::uint64_t), kC2iVectorStormTypeTag);
}

inline void* TSGrowableArray_CImVector_SetCapacityPreservingPrefix(
    LegacyResizableBufferView buffer, uint32_t new_capacity) {
    return ResizeLegacyArrayStoragePreservingPrefix(
        buffer, new_capacity, sizeof(uint32_t), kCImVectorStormTypeTag);
}

void TSGrowableArray_4Byte_ResizeExactCount(
    LegacyResizableBufferView buffer, uint32_t new_count,
    const char* debug_tag = "pa<x>");

void TSGrowableArray_CImVector_Resize(LegacyResizableBufferView buffer,
                                      uint32_t new_count);

bool InitSCritical_callee_47CBC0();

void* GetEvtContextTlsValue();
bool SetEvtContextTlsValue(void* value);
uint32_t EvtContextTls_GetIndexedDword(uint32_t index);
void* EvtContextTls_SetIndexedDword(uint32_t index, uint32_t value);
void FreeEvtContextTlsSlot();

void InitSCriticalRetail();

bool InitCommandLineRetail();

}
