
#include "openwow/game/comsat_voice_codec_resampler_state.h"

namespace openwow::game {

void ComSatVoiceCodec_ResetResamplerState(
    ComSatVoiceCodecResamplerState& state) noexcept {
    state.write_position     = 0;
    state.reserved_a         = 0;
    state.reserved_b         = 0;
    state.pending_rate_ratio  = 1;
}

}
