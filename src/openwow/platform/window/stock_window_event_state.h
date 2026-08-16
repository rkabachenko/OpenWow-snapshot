#pragma once

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdint>
#include <optional>

namespace openwow::platform {

class StockWindowEventState {
 public:
  struct ClientSizeChange {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
  };

  void Reset() {
    minimized_ = false;
    has_client_size_ = false;
    client_width_ = 0;
    client_height_ = 0;
  }

  void SetMinimized(const bool minimized) {
    minimized_ = minimized;
  }

  [[nodiscard]] std::optional<ClientSizeChange> ConsumeClientSizeChange(
      const SDL_Event& event) {
    if (event.type != SDL_WINDOWEVENT) {
      return std::nullopt;
    }

    switch (event.window.event) {
      case SDL_WINDOWEVENT_MINIMIZED:
        minimized_ = true;
        return std::nullopt;
      case SDL_WINDOWEVENT_RESTORED:
        minimized_ = false;
        return std::nullopt;
      case SDL_WINDOWEVENT_RESIZED:
      case SDL_WINDOWEVENT_SIZE_CHANGED:
        if (minimized_) {
          return std::nullopt;
        }

        return UpdateClientSize(
            static_cast<std::uint32_t>(std::max(event.window.data1, 0)),
            static_cast<std::uint32_t>(std::max(event.window.data2, 0)));
      default:
        return std::nullopt;
    }
  }

  [[nodiscard]] static bool IsTerminationRequest(const SDL_Event& event) {
    return event.type == SDL_QUIT
        || (event.type == SDL_WINDOWEVENT
            && event.window.event == SDL_WINDOWEVENT_CLOSE);
  }

  template <typename Callback>
  [[nodiscard]] static bool ShouldAllowDefaultTermination(
      const SDL_Event& event, Callback&& invoke_termination_callback) {
    if (!IsTerminationRequest(event)) {
      return false;
    }

    return static_cast<int>(invoke_termination_callback()) != 0;
  }

 private:
  [[nodiscard]] std::optional<ClientSizeChange> UpdateClientSize(
      const std::uint32_t width, const std::uint32_t height) {
    if (has_client_size_ && client_width_ == width && client_height_ == height) {
      return std::nullopt;
    }

    has_client_size_ = true;
    client_width_ = width;
    client_height_ = height;
    return ClientSizeChange{width, height};
  }

  bool minimized_ = false;
  bool has_client_size_ = false;
  std::uint32_t client_width_ = 0;
  std::uint32_t client_height_ = 0;
};

}
