#pragma once

namespace openwow::render::ui {

enum class UiTextureState {
  kNormal,
  kDesaturated,
};

[[nodiscard]] bool UiTextureStateSupported(UiTextureState state);

}
