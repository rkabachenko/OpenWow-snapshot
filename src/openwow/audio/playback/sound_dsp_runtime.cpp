#include "openwow/audio/playback/sound_runtime_internal.h"

namespace openwow::audio {

bool SoundRuntime::AreDspEffectsEnabled() const {
  return cvar_get_bool_cb_ ? cvar_get_bool_cb_("Sound_EnableDSPEffects") : true;
}

SoundRuntime::DspFilterChain &
SoundRuntime::EnsureDspFilterChain(const std::string &filter_name) {
  auto &chain = dsp_filters_[filter_name];
  if (chain.output_dsp == nullptr) {
    chain.output_dsp = sound_engine_->CreateDSPByName(filter_name.c_str());
  }
  return chain;
}

void SoundRuntime::ApplyDspEffectToChain(DspFilterChain &chain, const DspEffectType type,
                                           const std::vector<float> &params) {
  auto &engine = *sound_engine_;
  DspFilterNode node;
  node.type = type;
  node.params = params;
  node.has_runtime_dsp = type != DspEffectType::kVolume;
  node.applied_parameter_writes = BuildAppliedDspParameterWrites(type, params);
  if (node.has_runtime_dsp) {
    if (engine.CreateDSPByType(MapDspEffectToRuntimeType(type),
                               &node.runtime_dsp) != 0) {
      return;
    }
    node.bypassed = DspEffectStartsBypassed(params);
    engine.DSPSetBypass(node.runtime_dsp, node.bypassed);
    engine.DSPInsertAfter(node.runtime_dsp, chain.output_dsp);
  }
  if (type == DspEffectType::kVolume && DspEffectStartsEnabled(params) && params.size() > 1) {
    chain.output_volume_scale = std::clamp(params[1], 0.0f, 1.0f);
    UpsertDspParameterWrite(chain.output_parameter_writes, 0, chain.output_volume_scale);
    engine.DSPSetParameter(chain.output_dsp, chain.output_volume_scale);
    ApplyDspFilterOutputScale(chain);
  }
  chain.nodes.push_back(std::move(node));
}

void SoundRuntime::ApplyDspFilterOutputScale(DspFilterChain &chain) {
  if (chain.output_binding.active_handle_id == 0 || chain.output_dsp == nullptr) {
    return;
  }

  ApplySoundHandleRelativeVolumeScale(chain.output_binding.active_handle_id,
                                      chain.output_volume_scale);
}

void SoundRuntime::DetachDspFilterOutputBinding(DspFilterChain &chain) {
  const std::uint32_t handle_id = DetachHandleBinding(chain.output_binding);
  if (handle_id != 0) {
    ApplySoundHandleRelativeVolumeScale(handle_id, 1.0f);
  }
}

void SoundRuntime::ReleaseRuntimeDsp(void *runtime_dsp) {
  if (runtime_dsp == nullptr) {
    return;
  }

  auto &engine = *sound_engine_;
  engine.RemoveDSP(runtime_dsp);
  engine.DestroyDSP(runtime_dsp);
}

void SoundRuntime::ClearDspFilterNodes(DspFilterChain &chain) {
  for (auto &node : chain.nodes) {
    ReleaseRuntimeDsp(node.runtime_dsp);
    node.runtime_dsp = nullptr;
  }
  chain.nodes.clear();
}

void SoundRuntime::SetDspFilterChainBypass(DspFilterChain &chain, const bool bypassed) {
  auto &engine = *sound_engine_;
  for (auto &node : chain.nodes) {
    if (node.has_runtime_dsp) {
      node.bypassed = bypassed;
      engine.DSPSetBypass(node.runtime_dsp, bypassed);
    }
  }
}

void SoundRuntime::ApplyDspEffect(const std::string &filter_name, DspEffectType type,
                                    const std::vector<float> &params) {
  if (!AreDspEffectsEnabled()) {
    return;
  }

  auto &chain = EnsureDspFilterChain(filter_name);
  ApplyDspEffectToChain(chain, type, params);
}

bool SoundRuntime::PrimeDspFilterForPlaybackHandle(const std::string_view filter_name,
                                                     const std::uint32_t handle_id) {
  const auto it = dsp_filters_.find(std::string(filter_name));
  if (it == dsp_filters_.end()) {
    return false;
  }

  auto &chain = it->second;
  const std::uint32_t previous_handle_id = chain.output_binding.active_handle_id;
  if (!AttachHandleBinding(chain.output_binding, handle_id)) {
    return false;
  }

  SetDspFilterChainBypass(chain, true);
  chain.activation_delay_seconds = kDspFilterActivationDelaySeconds;

  if (previous_handle_id != 0 && previous_handle_id != handle_id) {
    ApplySoundHandleRelativeVolumeScale(previous_handle_id, 1.0f);
  }

  ApplyDspFilterOutputScale(chain);
  return true;
}

void SoundRuntime::UpdateDspFilterActivationDelays(const float delta_seconds) {
  for (auto &[_, chain] : dsp_filters_) {
    if (chain.output_binding.active_handle_id != 0) {
      continue;
    }

    if (chain.activation_delay_seconds > 0.0f &&
        chain.activation_delay_seconds - delta_seconds <= 0.0f) {
      SetDspFilterChainBypass(chain, false);
    }

    chain.activation_delay_seconds -= delta_seconds;
    if (chain.activation_delay_seconds < 0.0f) {
      chain.activation_delay_seconds = 0.0f;
    }
  }
}

}
