#include "openwow/audio/playback/sound_engine.h"

#include <algorithm>
#include <memory>
#include <string>

namespace openwow::audio {

SoundEngine::DspUnit* SoundEngine::ResolveDsp(void* dsp) {
  if (dsp == nullptr) {
    return nullptr;
  }

  auto* unit = static_cast<DspUnit*>(dsp);
  const auto it = std::find_if(dsp_units_.begin(), dsp_units_.end(),
                               [unit](const auto& candidate) {
                                 return candidate.get() == unit;
                               });
  return it == dsp_units_.end() ? nullptr : unit;
}

const SoundEngine::DspUnit* SoundEngine::ResolveDsp(const void* dsp) const {
  if (dsp == nullptr) {
    return nullptr;
  }

  const auto* unit = static_cast<const DspUnit*>(dsp);
  const auto it = std::find_if(dsp_units_.begin(), dsp_units_.end(),
                               [unit](const auto& candidate) {
                                 return candidate.get() == unit;
                               });
  return it == dsp_units_.end() ? nullptr : unit;
}

int SoundEngine::ConnectDspInput(DspUnit* destination,
                                 DspUnit* source,
                                 DspConnection** connection_out) {
  if (destination == nullptr || source == nullptr) {
    return 36;
  }

  auto connection = std::make_unique<DspConnection>();
  connection->source = source;
  connection->destination = destination;
  auto* handle = connection.get();
  source->outputs.push_back(handle);
  destination->inputs.push_back(handle);
  dsp_connections_.push_back(std::move(connection));
  if (connection_out != nullptr) {
    *connection_out = handle;
  }
  RefreshResolvedDspParameter(destination);
  return 0;
}

int SoundEngine::DisconnectDspConnection(DspConnection* connection) {
  if (connection == nullptr) {
    return 37;
  }

  if (connection->source != nullptr) {
    auto& outputs = connection->source->outputs;
    outputs.erase(std::remove(outputs.begin(), outputs.end(), connection), outputs.end());
  }

  if (connection->destination != nullptr) {
    auto& inputs = connection->destination->inputs;
    inputs.erase(std::remove(inputs.begin(), inputs.end(), connection), inputs.end());
    RefreshResolvedDspParameter(connection->destination);
  }

  const auto it = std::find_if(dsp_connections_.begin(), dsp_connections_.end(),
                               [connection](const auto& candidate) {
                                 return candidate.get() == connection;
                               });
  if (it != dsp_connections_.end()) {
    dsp_connections_.erase(it);
  }
  return 0;
}

int SoundEngine::GetDspOutputAtIndex(DspUnit* dsp,
                                     const std::size_t index,
                                     DspConnection** connection_out,
                                     DspUnit** output_out) {

  if (dsp == nullptr) {
    return 36;
  }

  if (index >= dsp->outputs.size()) {
    return 37;
  }

  if (dsp->outputs.empty()) {
    return 33;
  }

  auto* connection = dsp->outputs[index];
  if (connection_out != nullptr) {
    *connection_out = connection;
  }
  if (output_out != nullptr) {
    *output_out = connection != nullptr ? connection->destination : nullptr;
  }
  return connection != nullptr ? 0 : 33;
}

int SoundEngine::GetDspInputAtIndex(DspUnit* dsp,
                                    const std::size_t index,
                                    DspConnection** connection_out,
                                    DspUnit** input_out) {

  if (dsp == nullptr) {
    return 36;
  }
  if (index >= dsp->inputs.size()) {
    return 37;
  }
  if (dsp->inputs.empty()) {
    return 33;
  }
  auto* connection = dsp->inputs[index];
  if (connection_out != nullptr) {
    *connection_out = connection;
  }
  if (input_out != nullptr) {
    *input_out = connection != nullptr ? connection->source : nullptr;
  }
  return connection != nullptr ? 0 : 33;
}

std::size_t SoundEngine::GetNumDspInputs(const DspUnit* dsp) const {

  return dsp != nullptr ? dsp->inputs.size() : 0;
}

int SoundEngine::SpliceDspInputHead(DspUnit* head, DspUnit* node) {

  if (head == nullptr || node == nullptr) {
    return 37;
  }

  if (head->inputs.size() > 1) {
    return 18;
  }

  if (const int result = DisconnectDspGraphEdges(node, true, true); result != 0) {
    return result;
  }

  DspConnection* existing_conn = nullptr;
  DspUnit* existing_input = nullptr;
  if (!head->inputs.empty()) {
    if (const int result =
            GetDspInputAtIndex(head, 0, &existing_conn, &existing_input);
        result != 0) {
      return result;
    }
    if (existing_conn != nullptr) {
      if (const int result = DisconnectDspConnection(existing_conn); result != 0) {
        return result;
      }
    }
  }

  if (const int result = ConnectDspInput(head, node); result != 0) {
    return result;
  }

  if (existing_input != nullptr) {
    if (const int result = ConnectDspInput(node, existing_input); result != 0) {
      return result;
    }
  }

  return 0;
}

void SoundEngine::RefreshResolvedDspParameter(DspUnit* dsp) {
  if (dsp == nullptr) {
    return;
  }

  float upstream_parameter = 1.0f;
  if (!dsp->inputs.empty()) {
    const auto* connection = dsp->inputs.front();
    if (connection != nullptr && connection->source != nullptr) {
      upstream_parameter = connection->source->resolved_parameter_value;
    }
  }

  dsp->resolved_parameter_value = upstream_parameter * dsp->parameter_value;
  for (auto* connection : dsp->outputs) {
    if (connection != nullptr && connection->destination != nullptr) {
      RefreshResolvedDspParameter(connection->destination);
    }
  }
}

int SoundEngine::DisconnectDspGraphEdges(DspUnit* dsp,
                                         const bool disconnect_inputs,
                                         const bool disconnect_outputs) {
  if (dsp == nullptr) {
    return 36;
  }

  const auto disconnect_list = [this](std::vector<DspConnection*>& connections) {
    while (!connections.empty()) {
      auto* connection = connections.front();
      if (connection == nullptr) {
        return 33;
      }

      const int result = DisconnectDspConnection(connection);
      if (result != 0) {
        return result;
      }
    }
    return 0;
  };

  if (disconnect_inputs) {
    const int result = disconnect_list(dsp->inputs);
    if (result != 0) {
      return result;
    }
  }

  if (disconnect_outputs) {
    return disconnect_list(dsp->outputs);
  }

  return 0;
}

int SoundEngine::DisconnectDspInputsFrom(DspUnit* dsp,
                                         DspUnit* source_to_remove) {
  if (dsp == nullptr) {
    return 0;
  }

  for (auto it = dsp->inputs.begin(); it != dsp->inputs.end(); ) {
    DspConnection* connection = *it;
    if (connection != nullptr && connection->source == source_to_remove) {

      it = dsp->inputs.erase(it);

      if (connection->source != nullptr) {
        auto& outputs = connection->source->outputs;
        outputs.erase(std::remove(outputs.begin(), outputs.end(), connection),
                      outputs.end());
      }

      const auto pool_it = std::find_if(
          dsp_connections_.begin(), dsp_connections_.end(),
          [connection](const auto& c) { return c.get() == connection; });
      if (pool_it != dsp_connections_.end()) {
        dsp_connections_.erase(pool_it);
      }
    } else {
      ++it;
    }
  }

  RefreshResolvedDspParameter(dsp);
  return 0;
}

void SoundEngine::DestroyDspUnit(DspUnit* dsp) {
  if (dsp == nullptr) {
    return;
  }

  if (DisconnectDspGraphEdges(dsp, true, true) != 0) {
    return;
  }

  const auto it = std::find_if(dsp_units_.begin(), dsp_units_.end(),
                               [dsp](const auto& candidate) {
                                 return candidate.get() == dsp;
                               });
  if (it != dsp_units_.end()) {
    dsp_units_.erase(it);
  }
}

int SoundEngine::DeactivateDspNode(DspUnit* dsp) {
  if (dsp == nullptr) {
    return 37;
  }

  if (const int result = DisconnectDspGraphEdges(dsp, true, true); result != 0) {
    return result;
  }

  dsp->parameter_value = 1.0f;
  dsp->resolved_parameter_value = 1.0f;
  dsp->active = false;

  return 0;
}

void SoundEngine::ResetDspGraph() {
  playback_dsp_heads_.clear();
  dsp_connections_.clear();
  dsp_units_.clear();
}

int SoundEngine::CreateDSPByType(const std::uint32_t type_id, void** dsp_out) {
  if (dsp_out == nullptr) {
    return 37;
  }

  *dsp_out = nullptr;
  if (type_id == 0) {
    return 64;
  }

  std::lock_guard lock(dsp_graph_mutex_);
  auto dsp = std::make_unique<DspUnit>();
  dsp->debug_name = "DSPType:" + std::to_string(type_id);
  auto* handle = dsp.get();
  dsp_units_.push_back(std::move(dsp));
  *dsp_out = handle;
  return 0;
}

int SoundEngine::DSPSetBypass(void* dsp, bool bypass) {
  std::lock_guard lock(dsp_graph_mutex_);
  auto* unit = ResolveDsp(dsp);
  if (unit == nullptr) {
    return 36;
  }
  unit->bypassed = bypass;
  return 0;
}

int SoundEngine::DSPInsertAfter(void* dsp, void* target_dsp) {
  std::lock_guard lock(dsp_graph_mutex_);
  auto* inserted = ResolveDsp(dsp);
  auto* target = ResolveDsp(target_dsp);
  if (inserted == nullptr || target == nullptr) {
    return 0;
  }

  DspConnection* connection = nullptr;
  DspUnit* downstream = nullptr;
  if (GetDspOutputAtIndex(target, 0, &connection, &downstream) != 0 ||
      connection == nullptr || downstream == nullptr) {
    return 0;
  }

  (void)DisconnectDspConnection(connection);
  (void)ConnectDspInput(downstream, inserted);
  (void)ConnectDspInput(inserted, target);
  return 0;
}

void* SoundEngine::CreateDSPByName(const char* type_name) {
  if (type_name == nullptr) {
    return nullptr;
  }

  std::lock_guard lock(dsp_graph_mutex_);
  auto dsp = std::make_unique<DspUnit>();
  dsp->debug_name = type_name;
  auto* handle = dsp.get();
  dsp_units_.push_back(std::move(dsp));
  return handle;
}

SoundEngine::DspUnit* SoundEngine::CreateMonitorDSP() {
  std::lock_guard lock(dsp_graph_mutex_);
  auto dsp = std::make_unique<DspUnit>();
  dsp->debug_name = "Monitor DSP";
  auto* handle = dsp.get();
  dsp_units_.push_back(std::move(dsp));
  monitor_dsp_ = handle;
  return handle;
}

int SoundEngine::DSPSetParameter(void* dsp, float value) {
  std::lock_guard lock(dsp_graph_mutex_);
  auto* unit = ResolveDsp(dsp);
  if (unit == nullptr) {
    return 36;
  }
  unit->parameter_value = std::clamp(value, 0.0f, 1.0f);
  RefreshResolvedDspParameter(unit);
  return 0;
}

float SoundEngine::GetDspResolvedParameter(void* dsp) {
  std::lock_guard lock(dsp_graph_mutex_);
  auto* unit = ResolveDsp(dsp);
  return unit != nullptr ? unit->resolved_parameter_value : 1.0f;
}

int SoundEngine::RemoveDSP(void* dsp) {
  std::lock_guard lock(dsp_graph_mutex_);
  auto* unit = ResolveDsp(dsp);
  if (unit == nullptr) {
    return 36;
  }
  return DisconnectDspGraphEdges(unit, true, true);
}

void SoundEngine::DestroyDSP(void* dsp) {
  std::lock_guard lock(dsp_graph_mutex_);
  DestroyDspUnit(ResolveDsp(dsp));
}

void SoundEngine::ConnectMonitorDSP(SoundObj* obj) {
  if (monitor_dsp_ == nullptr || obj == nullptr) {
    return;
  }
  if (obj->playback_handle_id == 0) {
    return;
  }

  DspUnit* dsp_head = FindPlaybackDspHead(obj->playback_handle_id);
  if (dsp_head == nullptr) {
    return;
  }

  std::lock_guard lock(dsp_graph_mutex_);
  (void)SpliceDspInputHead(dsp_head, monitor_dsp_);
}

SoundEngine::DspUnit* SoundEngine::RegisterPlaybackDspHead(
    std::uint32_t handle_id) {
  if (handle_id == 0) {
    return nullptr;
  }

  auto it = playback_dsp_heads_.find(handle_id);
  if (it != playback_dsp_heads_.end()) {
    return it->second;
  }

  const std::string debug_name = "ChannelDSPHead_" + std::to_string(handle_id);
  auto* unit = static_cast<DspUnit*>(CreateDSPByName(debug_name.c_str()));
  if (unit != nullptr) {
    playback_dsp_heads_[handle_id] = unit;
  }
  return unit;
}

void SoundEngine::UnregisterPlaybackDspHead(std::uint32_t handle_id) {
  auto it = playback_dsp_heads_.find(handle_id);
  if (it == playback_dsp_heads_.end()) {
    return;
  }

  DspUnit* unit = it->second;
  playback_dsp_heads_.erase(it);

  std::lock_guard lock(dsp_graph_mutex_);
  (void)DisconnectDspGraphEdges(unit, true, true);
  DestroyDspUnit(unit);
}

SoundEngine::DspUnit* SoundEngine::FindPlaybackDspHead(
    std::uint32_t handle_id) const {
  auto it = playback_dsp_heads_.find(handle_id);
  return it != playback_dsp_heads_.end() ? it->second : nullptr;
}

}
