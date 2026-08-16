#pragma once

#include <cstdint>

namespace openwow::game {

static constexpr int32_t kStatusInvalidParameterMix = static_cast<int32_t>(0xC0000030);

static constexpr int32_t kStatusInvalidCid = static_cast<int32_t>(0xC000000B);

static constexpr uint32_t kProcessAllAccess = 0x001F0FFF;

static constexpr int kSystemHandleInformation = 16;

static constexpr uint32_t kSystemPid = 4;

enum class DbgBreakpointProbeResult : uint8_t {
    kClean   = 0,
    kHooked  = 2,
};

bool Warden_ScanHandleTableForBotProcess(uint32_t* out_suspicious_pid);

int32_t Warden_NtOpenProcessProbe(uint32_t pid, void* nt_open_proc);

bool Warden_ResolveNtdllAndScanHandles(void* ntdll_handle, uint32_t* out_pid);

bool Warden_CheckWineEnvironment(void* kernel32_handle);

uint8_t Warden_CheckDbgBreakpointHook();

bool BuildBotDetectedProbeBytes(uint8_t* probe_byte_0,
                                uint8_t* probe_byte_1,
                                uint8_t* probe_byte_2);

int GetProbeValues(int* v1, int* v2, int* v3);

}
