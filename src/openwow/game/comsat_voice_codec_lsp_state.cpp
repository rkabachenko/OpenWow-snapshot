
#include "openwow/game/comsat_voice_codec_lsp_state.h"

#include <algorithm>
#include <cstring>

namespace openwow::game {

void ComSatVoiceCodec_InitLspPredictorState(
    ComSatVoiceCodecLspPredictorState& state,
    const float* source_data) noexcept {

    state.follow_phase = 0;
    state.reserved_a = 0;
    state.reserved_b = 0;

    if (source_data) {
        std::memcpy(state.lsp_cosines.data(), source_data,
                    kComSatLspOrder * sizeof(float));
    }

    state.primary_gain_history.fill(0.1f);

    state.primary_gain_cap = 1.0f;

    state.secondary_gain_history.fill(0.5f);

    state.secondary_gain_cap = 0.5f;

    state.frame_accumulator = kComSatLspDefaultFrameAccumulator;

    state.random_seed = kComSatLspDefaultRandomSeed;
}

}
