#pragma once

#include <string_view>
#include <utility>

namespace openwow::ui::glue {

template <typename FocusFn, typename MouseDownFn>
void DispatchEditBoxMouseDownWithFocusTransfer(std::string_view focused_widget,
                                               std::string_view target_widget,
                                               FocusFn&& focus_fn,
                                               MouseDownFn&& mouse_down_fn) {
  if (focused_widget != target_widget) {
    std::forward<FocusFn>(focus_fn)();
  }
  std::forward<MouseDownFn>(mouse_down_fn)();
}

}
