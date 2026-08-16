#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "openwow/core/client_misc.h"

namespace openwow::screens {

namespace detail {

inline constexpr float kLoadingScreenTextFontHeight = 0.018f;
inline constexpr float kLoadingScreenTextWidthAt1024 = 515.0f;
inline constexpr std::uint32_t kLoadingScreenTextColorArgb =
    0xD7C8C8C8u;
inline constexpr std::uint32_t kLoadingScreenShadowColorArgb =
    0xFF000000u;
inline constexpr float kLoadingScreenShadowOffsetX = 0.001f;
inline constexpr float kLoadingScreenShadowOffsetY = -0.001f;

inline std::string TrimLoadingScreenStatusText(std::string_view text) {
  while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) {
    text.remove_suffix(1);
  }
  return std::string{text};
}

inline float ClampUnit(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

}

enum class LoadingProgressInput : std::uint8_t {
  Primary,
  Secondary,
  Unused,
};

struct LoadingScreenTextPresentation {
  bool has_source = false;
  bool built = false;
  int loading_screen_type = 0;
  std::string text;
  float font_height = detail::kLoadingScreenTextFontHeight;
  float anchor_x = 0.1f;
  float anchor_y = 0.5f;
  float width = 0.0f;
  std::uint32_t text_color_argb =
      detail::kLoadingScreenTextColorArgb;
  std::uint32_t shadow_color_argb =
      detail::kLoadingScreenShadowColorArgb;
  float shadow_offset_x = detail::kLoadingScreenShadowOffsetX;
  float shadow_offset_y = detail::kLoadingScreenShadowOffsetY;
  std::uint64_t source_generation = 0u;
  std::uint64_t accepted_source_generation = 0u;
  std::size_t accepted_text_layer_count = 0u;
};

class LoadingScreenManager {
 public:
  static LoadingScreenManager& Get() {
    static LoadingScreenManager instance;
    return instance;
  }

  void Reset() {
    visible_ = false;
    progress_ = 0.0f;
    loading_screen_type_ = 0;
    text_presentation_ = {};
    trial_mode_ = false;
    trial_alpha_ = 0.0f;
    render_layer_flags_ = 0;
    ResetCompositeProgress(false);
  }

  void Show() {
    visible_ = true;
    progress_ = 0.0f;
  }

  void Hide() {
    visible_ = false;
    progress_ = 1.0f;
    SetTransportWorldEntryHold(false);
    ResetTextRenderReceipt();
  }

  [[nodiscard]] bool IsVisible() const { return visible_; }

  void SetProgress(float progress) { progress_ = detail::ClampUnit(progress); }
  [[nodiscard]] float GetProgress() const { return progress_; }

  void ResetCompositeProgress(bool split_secondary_progress) {
    split_secondary_progress_ = split_secondary_progress;
    primary_progress_ = 0.0f;
    secondary_progress_ = 0.0f;
    unused_progress_ = 0.0f;
    progress_ = 0.0f;
  }

  bool UpdateCompositeProgressInput(LoadingProgressInput input, float progress) {
    float& slot = CompositeProgressSlot(input);
    const float clamped = detail::ClampUnit(progress);
    if (clamped <= slot) {
      return false;
    }
    slot = clamped;
    return true;
  }

  [[nodiscard]] float GetCompositeProgressInput(LoadingProgressInput input) {
    return CompositeProgressSlot(input);
  }

  [[nodiscard]] bool UsesSplitSecondaryProgress() const {
    return split_secondary_progress_;
  }

  void UpdateDisplayProgress(bool online_mode, bool online_trial_gate_open) {
    if (online_mode && trial_mode_) {
      progress_ = online_trial_gate_open
                      ? detail::ClampUnit(primary_progress_ * 0.30000001f + 0.69999999f)
                      : detail::ClampUnit(trial_alpha_ * 0.69999999f);
      return;
    }

    progress_ = ResolveCompositeProgress();
  }

  [[nodiscard]] bool IsTrialMode() const { return trial_mode_; }

  bool UpdateTrialLoadingAlpha(float alpha) {
    const float clamped = detail::ClampUnit(alpha);
    if (clamped <= trial_alpha_) {
      return false;
    }
    trial_alpha_ = clamped;
    return true;
  }

  [[nodiscard]] float GetTrialLoadingAlpha() const { return trial_alpha_; }

  [[nodiscard]] std::size_t GetTipCount() const { return tip_sources_.size(); }

  [[nodiscard]] const char* GetTipTextSourceByIndex(std::size_t index) const {
    return index < tip_sources_.size() ? tip_sources_[index].c_str() : nullptr;
  }

  void SetTextSource(const char* text) {
    std::uint64_t next_generation = text_presentation_.source_generation + 1u;
    if (next_generation == 0u) {
      ++next_generation;
    }
    text_presentation_ = {};
    text_presentation_.source_generation = next_generation;
    text_presentation_.has_source = text != nullptr;
    if (text != nullptr) {
      text_presentation_.text =
          detail::TrimLoadingScreenStatusText(text);
    }
  }

  [[nodiscard]] const char* GetTextSource() const {
    return text_presentation_.has_source
               ? text_presentation_.text.c_str()
               : nullptr;
  }

  void RebuildTextPresentation(int loading_screen_type,
                                     float ui_aspect_width_scale,
                                     float ui_vertical_scale) {

    loading_screen_type_ = loading_screen_type;
    text_presentation_.loading_screen_type = loading_screen_type;
    ResetTextRenderReceipt();
    if (!text_presentation_.has_source) {
      text_presentation_.built = false;
      return;
    }

    const float safe_width_scale =
        ui_aspect_width_scale > 0.0f ? ui_aspect_width_scale : 1.0f;
    const float safe_vertical_scale =
        ui_vertical_scale > 0.0f ? ui_vertical_scale : 1.0f;
    text_presentation_.built = true;

    text_presentation_.width =
        detail::kLoadingScreenTextWidthAt1024 /
        (safe_width_scale * 1024.0f);
    text_presentation_.anchor_x = 0.5f - text_presentation_.width * 0.5f;
    text_presentation_.anchor_y =
        0.1f + (safe_width_scale < 1.0f ? (1.0f - safe_width_scale) * 0.5f
                                        : 0.0f);
    static_cast<void>(safe_vertical_scale);
  }

  [[nodiscard]] int GetLoadingScreenType() const { return loading_screen_type_; }
  [[nodiscard]] const LoadingScreenTextPresentation& GetTextPresentation() const {
    return text_presentation_;
  }

  void ResetTextRenderReceipt() {
    text_presentation_.accepted_source_generation = 0u;
    text_presentation_.accepted_text_layer_count = 0u;
  }

  void PublishTextRenderReceipt(const bool accepted) {
    ResetTextRenderReceipt();
    if (accepted && text_presentation_.built) {
      text_presentation_.accepted_source_generation =
          text_presentation_.source_generation;
      text_presentation_.accepted_text_layer_count = 1u;
    }
  }

  void SetTrialLoadingMessage(bool enabled) {
    trial_mode_ = enabled;
    if (!enabled) {
      trial_alpha_ = 0.0f;
    }
  }

  void SetTransportWorldEntryHold(bool enabled) {
    if (enabled) {
      render_layer_flags_ |= 0x1u;
    } else {
      render_layer_flags_ &= ~0x1u;
    }
  }

  [[nodiscard]] bool HasTransportWorldEntryHold() const {
    return (render_layer_flags_ & 0x1u) != 0;
  }

  [[nodiscard]] std::uint32_t GetRenderLayerFlags() const {
    return render_layer_flags_;
  }

  void BindGameTipsStore(std::nullptr_t) { tip_sources_.clear(); }

  template <typename T>
  void BindGameTipsStore(const T* store) {
    tip_sources_.clear();
    if (store == nullptr) {
      return;
    }

    tip_sources_.reserve(store->size());
    for (std::uint32_t row = 0; row < store->size(); ++row) {
      const auto* entry = store->LookupEntryByRowIndex(static_cast<int>(row));
      if (entry != nullptr && !entry->text.empty()) {
        tip_sources_.emplace_back(entry->text);
      }
    }
  }

 private:
  [[nodiscard]] float ResolveCompositeProgress() const {
    if (split_secondary_progress_ && secondary_progress_ > 0.0f) {
      return detail::ClampUnit(primary_progress_ * 0.5f + secondary_progress_ * 0.5f);
    }
    return detail::ClampUnit(primary_progress_ + secondary_progress_);
  }

  float& CompositeProgressSlot(LoadingProgressInput input) {
    switch (input) {
      case LoadingProgressInput::Secondary:
        return secondary_progress_;
      case LoadingProgressInput::Unused:
        return unused_progress_;
      case LoadingProgressInput::Primary:
      default:
        return primary_progress_;
    }
  }

  bool visible_ = false;
  float progress_ = 0.0f;
  std::vector<std::string> tip_sources_;
  int loading_screen_type_ = 0;
  LoadingScreenTextPresentation text_presentation_;
  bool split_secondary_progress_ = false;
  float primary_progress_ = 0.0f;
  float secondary_progress_ = 0.0f;
  float unused_progress_ = 0.0f;
  bool trial_mode_ = false;
  float trial_alpha_ = 0.0f;
  std::uint32_t render_layer_flags_ = 0;
};

inline bool UpdateLoadingScreenDisplayProgress(bool online_mode,
                                               bool online_trial_gate_open) {
  auto& manager = LoadingScreenManager::Get();
  manager.UpdateDisplayProgress(online_mode, online_trial_gate_open);
  return online_mode && manager.IsTrialMode() && online_trial_gate_open;
}

inline bool UpdateLoadingScreenForMapChange(const std::uint32_t transport_entry,
                                            const std::uint32_t previous_map_id,
                                            const std::uint32_t current_transport_entry,
                                            const std::uint32_t loading_path_id) {
  openwow::core::LoadingScreen_CleanupDynamicMapChangeAssets();
  if (transport_entry == 0 || transport_entry != current_transport_entry ||
      !openwow::core::LoadingScreen_BuildMapChangeOverlayForPreviousMap(
          loading_path_id, previous_map_id)) {
    return false;
  }

  auto &assets = openwow::core::LoadingScreen_GetDynamicMapChangeAssets();
  assets.dynamic_elements_loaded = true;
  assets.world_tile_texture_count = 12;
  return true;
}

}

namespace openwow::core {

const char* LoadingScreen_SetTextSource(const char*);
void LoadingScreen_InitFont(int, bool);
const char* fn_TRIAL_LOADING_MESSAGE(char);

}
