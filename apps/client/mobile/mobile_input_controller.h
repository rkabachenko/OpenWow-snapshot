#pragma once

#include "mobile_ios_platform.h"

#include <SDL2/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace openwow::client::mobile {

enum class TouchOwner : std::uint8_t {
  kUnassigned,
  kIgnored,
  kGlueUi,
  kWorldUi,
  kVirtualMouse,
  kMovement,
  kWorldCamera,
  kWorldTap,
};

struct TouchPoint {
  float logical_x{0.0F};
  float logical_y{0.0F};
  float drawable_x{0.0F};
  float drawable_y{0.0F};
};

struct ViewportMetrics {
  int logical_width{1};
  int logical_height{1};
  int drawable_width{1};
  int drawable_height{1};
  float drawable_scale_x{1.0F};
  float drawable_scale_y{1.0F};
  SafeAreaInsetsPoints safe_area_points;
  float safe_left_drawable{0.0F};
  float safe_top_drawable{0.0F};
  float safe_right_drawable{0.0F};
  float safe_bottom_drawable{0.0F};
};

struct TouchContact {
  SDL_TouchID touch_id{0};
  SDL_FingerID finger_id{0};
  TouchOwner owner{TouchOwner::kUnassigned};
  TouchPoint start;
  TouchPoint previous;
  TouchPoint current;
  std::uint32_t started_at_ms{0};
  float pressure{0.0F};
};

class MobileInputController final {
 public:
  static constexpr std::size_t kMaximumContacts = 10;

  void RefreshViewport(SDL_Window* window, void* native_window);
  [[nodiscard]] const ViewportMetrics& viewport() const noexcept {
    return viewport_;
  }
  [[nodiscard]] TouchPoint ResolvePoint(float normalized_x,
                                        float normalized_y) const noexcept;

  [[nodiscard]] TouchContact* BeginContact(const SDL_TouchFingerEvent& event);
  [[nodiscard]] TouchContact* UpdateContact(const SDL_TouchFingerEvent& event);
  [[nodiscard]] TouchContact* FindContact(SDL_FingerID finger_id) noexcept;
  [[nodiscard]] const TouchContact* FindContact(
      SDL_FingerID finger_id) const noexcept;
  [[nodiscard]] TouchContact* FindContactByOwner(TouchOwner owner) noexcept;
  [[nodiscard]] const TouchContact* FindContactByOwner(
      TouchOwner owner) const noexcept;
  [[nodiscard]] TouchContact* FindOtherContactByOwner(
      TouchOwner owner, SDL_FingerID finger_id) noexcept;
  [[nodiscard]] bool HasOwner(TouchOwner owner) const noexcept;
  [[nodiscard]] std::optional<TouchContact> EndContact(
      const SDL_TouchFingerEvent& event);
  [[nodiscard]] std::vector<TouchContact> TakeAllContacts();

 private:
  ViewportMetrics viewport_;
  std::array<std::optional<TouchContact>, kMaximumContacts> contacts_;
};

}
