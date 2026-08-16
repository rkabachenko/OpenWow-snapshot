
#include "openwow/game/warden_probes.h"

namespace openwow::game {

int32_t Warden_NtOpenProcessProbe(uint32_t , void* ) {
    return kStatusInvalidParameterMix;
}

bool Warden_ScanHandleTableForBotProcess(uint32_t* out_suspicious_pid) {
    if (out_suspicious_pid) {
        *out_suspicious_pid = 0;
    }
    return false;
}

bool Warden_ResolveNtdllAndScanHandles(void* ,
                                        uint32_t* out_pid) {
    return Warden_ScanHandleTableForBotProcess(out_pid);
}

bool Warden_CheckWineEnvironment(void* ) {
    return true;
}

uint8_t Warden_CheckDbgBreakpointHook() {
    return static_cast<uint8_t>(DbgBreakpointProbeResult::kClean);
}

bool BuildBotDetectedProbeBytes(uint8_t* probe_byte_0,
                                uint8_t* probe_byte_1,
                                uint8_t* probe_byte_2) {
    if (probe_byte_0) *probe_byte_0 = 0;
    if (probe_byte_1) *probe_byte_1 = 0;
    if (probe_byte_2) *probe_byte_2 = 0;

    return false;
}

int GetProbeValues(int* v1, int* v2, int* v3) {
    uint8_t p0 = 0, p1 = 0, p2 = 0;
    BuildBotDetectedProbeBytes(&p0, &p1, &p2);

    if (v1) *v1 = static_cast<int>(p0);
    if (v2) *v2 = static_cast<int>(p1);
    if (v3) *v3 = static_cast<int>(p2);

    return 1;
}

}
