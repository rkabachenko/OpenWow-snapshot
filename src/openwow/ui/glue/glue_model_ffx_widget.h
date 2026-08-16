#pragma once

#include "openwow/data/formats/m2/model_path.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace openwow::ui::glue {

class GlueModelInstance {
 public:
  void SetLoadedPath(const std::string& normalized_path) { loaded_path_ = normalized_path; }
  void SetRequestedPath(const std::string& requested_path) {
    requested_path_ = requested_path;
  }
  void Clear() {
    loaded_path_.clear();
    requested_path_.clear();
  }
  [[nodiscard]] const std::string& LoadedPath() const { return loaded_path_; }
  [[nodiscard]] const std::string& RequestedPath() const { return requested_path_; }

 private:
  std::string loaded_path_;
  std::string requested_path_;
};

struct ModelFFXContextBlock {
  static constexpr std::size_t kSize = 448;
  static constexpr std::size_t kDirtyOffset = 437;
  static constexpr std::size_t kGhostBranchGateOffset = 444;

  [[nodiscard]] bool dirty() const { return bytes_[kDirtyOffset] != 0; }
  void set_dirty(bool v) { bytes_[kDirtyOffset] = v ? 1u : 0u; }

  [[nodiscard]] std::uint8_t ghost_branch_gate() const {
    return bytes_[kGhostBranchGateOffset];
  }
  void set_ghost_branch_gate(std::uint8_t value) {
    bytes_[kGhostBranchGateOffset] = value;
  }

 private:
  std::array<std::uint8_t, kSize> bytes_{};
};

class GlueModelFFXWidget {
 public:
  explicit GlueModelFFXWidget(std::string name) : name_(std::move(name)) {}

  [[nodiscard]] const std::string& name() const { return name_; }

  GlueModelInstance* model_instance() { return &model_; }
  const GlueModelInstance* model_instance() const { return &model_; }

  [[nodiscard]] std::string SetModelFile(const std::string& raw_path) {
    const std::string normalized = openwow::data::m2::NormalizeModelPath(raw_path);
    if (normalized.empty()) {
      model_.Clear();
      return {};
    }
    model_.SetLoadedPath(normalized);
    model_.SetRequestedPath(raw_path);
    return normalized;
  }

  void ClearModel() { model_.Clear(); }

  ModelFFXContextBlock& context0() { return contexts_[0]; }
  const ModelFFXContextBlock& context0() const { return contexts_[0]; }

  [[nodiscard]] bool dirty() const { return contexts_[0].dirty(); }
  void set_dirty(bool v) { contexts_[0].set_dirty(v); }

  [[nodiscard]] bool ghost_branch_gate_enabled() const {
    return contexts_[0].ghost_branch_gate() != 0;
  }
  void set_ghost_branch_gate_enabled(bool value) {
    contexts_[0].set_ghost_branch_gate(value ? 1u : 0u);
  }

 private:
  std::string name_;
  GlueModelInstance model_;
  std::array<ModelFFXContextBlock, 2> contexts_{};
};

}
