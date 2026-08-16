
#include "openwow/core/storm_alloc.h"
#include "openwow/core/storm_cmd.h"
#include "openwow/core/storm_string.h"
#include "openwow/core/storm_tls.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace openwow::core {

static TlsSlotHandle g_evt_context_tls_slot = kInvalidTlsSlot;

constexpr std::array<CmdDefInitEntry, 16> kInitCommandLineDefinitions{{
    {0x00000u, static_cast<uint32_t>(StartupCommandId::kD3D), "d3d", nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(StartupCommandId::kD3D9Ex), "d3d9ex", nullptr, nullptr},
    {0x20000u, static_cast<uint32_t>(StartupCommandId::kDataDir), "datadir", nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(StartupCommandId::kNoLagFix), "nolagfix", nullptr, nullptr},
    {0x20000u, static_cast<uint32_t>(StartupCommandId::kLoadFile), "loadfile", nullptr, nullptr},
    {0x20000u, static_cast<uint32_t>(StartupCommandId::kGameType), "gametype", nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(StartupCommandId::kOpenGl), "opengl", nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(StartupCommandId::kSoftwareTnl), "swtnl", nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(StartupCommandId::kTimeDemo), "timedemo", nullptr, nullptr},
    {0x20000u, static_cast<uint32_t>(StartupCommandId::kResolutionOverride), "rez", nullptr, nullptr},
    {0x20000u, static_cast<uint32_t>(StartupCommandId::kDepthOverride), "depth", nullptr, nullptr},
    {0x20000u, static_cast<uint32_t>(StartupCommandId::kDetailOverride), "detail", nullptr, nullptr},
    {0x20000u, static_cast<uint32_t>(StartupCommandId::kSoundOverride), "sound", nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(StartupCommandId::kFullscreen), "fullscreen", nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(StartupCommandId::kSampleRate22050), "22050", nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(StartupCommandId::kNoWarnings), "nowarnings", nullptr, nullptr},
}};

void* Prop_Alloc() {
    return SMemAlloc(76, ".\\Prop.cpp", 0x30, 8);
}

void Storm_OperatorDelete(void* block) {
    (void)SMemFree(block, ".\\MemoryStorm.cpp", 94, 0);
}

void FrameXML_OperatorDelete(void* block) {
    (void)SMemFree(block, "delete", -1, 0);
}

LegacyResizableBufferView::LegacyResizableBufferView(void* storage)
    : storage_(static_cast<std::byte*>(storage)) {}

uint32_t LegacyResizableBufferView::capacity() const {
    return LoadU32(0);
}

uint32_t LegacyResizableBufferView::count() const {
    return LoadU32(4);
}

void* LegacyResizableBufferView::data() const {
    void* value = nullptr;
    std::memcpy(&value, storage_ + 8, sizeof(value));
    return value;
}

void LegacyResizableBufferView::set_capacity(uint32_t value) const {
    StoreU32(0, value);
}

void LegacyResizableBufferView::set_count(uint32_t value) const {
    StoreU32(4, value);
}

void LegacyResizableBufferView::set_data(void* value) const {
    std::memcpy(storage_ + 8, &value, sizeof(value));
}

uint32_t LegacyResizableBufferView::LoadU32(const std::size_t offset) const {
    uint32_t value = 0;
    std::memcpy(&value, storage_ + offset, sizeof(value));
    return value;
}

void LegacyResizableBufferView::StoreU32(const std::size_t offset,
                                         const uint32_t value) const {
    std::memcpy(storage_ + offset, &value, sizeof(value));
}

void* ResizeLegacyArrayStoragePreservingPrefix(
    const LegacyResizableBufferView buffer, const uint32_t new_capacity,
    const std::size_t element_size, const char* const debug_tag) {
    void* const old_buffer = buffer.data();
    const uint32_t old_count = buffer.count();

    buffer.set_capacity(new_capacity);

    void* new_buffer = SMemReAlloc(old_buffer, element_size * new_capacity,
                                   debug_tag, -2, 16);
    buffer.set_data(new_buffer);

    if (new_buffer == nullptr) {
        new_buffer = SMemAlloc(element_size * new_capacity, debug_tag, -2, 0);
        buffer.set_data(new_buffer);

        if (old_buffer != nullptr) {
            const auto copy_count = std::min(new_capacity, old_count);
            if (new_buffer != nullptr) {
                std::memcpy(new_buffer, old_buffer,
                            element_size * static_cast<std::size_t>(copy_count));
            }

            (void)SMemFree(old_buffer, debug_tag, -2, 0);
        }
    }

    return new_buffer;
}

namespace {

enum class ExactCountGrowthInitialization {
    kLeaveUninitialized,
    kZeroFill,
};

void ResizeLegacyArrayToExactCount(
    const LegacyResizableBufferView buffer, const uint32_t new_count,
    const std::size_t element_size, const char* const debug_tag,
    const ExactCountGrowthInitialization growth_initialization) {
    const uint32_t old_count = buffer.count();
    if (new_count == old_count) {
        return;
    }

    if (new_count == 0) {
        if (void* data = buffer.data(); data != nullptr) {
            (void)SMemFree(data, debug_tag, -2, 0);
        }
        buffer.set_capacity(0);
        buffer.set_count(0);
        buffer.set_data(nullptr);
        return;
    }

    auto* const resized_data = static_cast<std::byte*>(
        ResizeLegacyArrayStoragePreservingPrefix(buffer, new_count, element_size,
                                                 debug_tag));
    if (growth_initialization == ExactCountGrowthInitialization::kZeroFill &&
        resized_data != nullptr && old_count < new_count) {
        std::memset(resized_data + element_size * static_cast<std::size_t>(old_count), 0,
                    element_size *
                        static_cast<std::size_t>(new_count - old_count));
    }

    buffer.set_count(new_count);
}

}

void TSGrowableArray_4Byte_ResizeExactCount(
    const LegacyResizableBufferView buffer, const uint32_t new_count,
    const char* const debug_tag) {
    ResizeLegacyArrayToExactCount(
        buffer, new_count, sizeof(std::uint32_t), debug_tag,
        ExactCountGrowthInitialization::kLeaveUninitialized);
}

void TSGrowableArray_CImVector_Resize(const LegacyResizableBufferView buffer,
                                      const uint32_t new_count) {
    ResizeLegacyArrayToExactCount(
        buffer, new_count, sizeof(std::uint32_t), kCImVectorStormTypeTag,
        ExactCountGrowthInitialization::kZeroFill);
}

void* GetEvtContextTlsValue() {
    return StormTls::Instance().GetValue(g_evt_context_tls_slot);
}

bool SetEvtContextTlsValue(void* value) {
    return StormTls::Instance().SetValue(g_evt_context_tls_slot, value);
}

uint32_t EvtContextTls_GetIndexedDword(uint32_t index) {
    auto* const tls_payload =
        static_cast<uint32_t*>(StormTls::Instance().GetValue(g_evt_context_tls_slot));
    if (tls_payload == nullptr) {
        return 0;
    }

    return tls_payload[index];
}

void* EvtContextTls_SetIndexedDword(uint32_t index, uint32_t value) {
    auto* const tls_payload =
        static_cast<uint32_t*>(StormTls::Instance().GetValue(g_evt_context_tls_slot));
    if (tls_payload != nullptr) {
        tls_payload[index] = value;
    }

    return tls_payload;
}

void FreeEvtContextTlsSlot() {
    StormTls::Instance().FreeSlot(g_evt_context_tls_slot);
}

bool InitSCritical_callee_47CBC0() {
    g_evt_context_tls_slot = StormTls::Instance().AllocSlot();
    return SetEvtContextTlsValue(nullptr);
}

void InitSCriticalRetail() {
    InitSCritical_callee_47CBC0();

}

bool InitCommandLineRetail() {
    auto& storm_cmd = StormCmd::Instance();
    if (!storm_cmd.InitErrorStrings(kInitCommandLineDefinitions)) {
        return false;
    }
    return storm_cmd.InitCommandLine(nullptr, nullptr);
}

}
