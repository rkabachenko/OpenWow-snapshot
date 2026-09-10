#include "mobile_input_controller.h"

#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <string>

namespace openwow::client::mobile {

void MobileInputController::RefreshViewport(SDL_Window* window,
                                            void* native_window) {
  int logical_width = 1;
  int logical_height = 1;
  int drawable_width = 1;
  int drawable_height = 1;
  if (window != nullptr) {
    SDL_GetWindowSize(window, &logical_width, &logical_height);
    SDL_GetWindowSizeInPixels(window, &drawable_width, &drawable_height);
    if (drawable_width <= 0 || drawable_height <= 0) {
      SDL_GetWindowSize(window, &drawable_width, &drawable_height);
    }
  }

  viewport_.logical_width = std::max(1, logical_width);
  viewport_.logical_height = std::max(1, logical_height);
  viewport_.drawable_width = std::max(1, drawable_width);
  viewport_.drawable_height = std::max(1, drawable_height);
  viewport_.drawable_scale_x =
      static_cast<float>(viewport_.drawable_width) /
      static_cast<float>(viewport_.logical_width);
  viewport_.drawable_scale_y =
      static_cast<float>(viewport_.drawable_height) /
      static_cast<float>(viewport_.logical_height);
  viewport_.safe_area_points = QuerySafeAreaInsetsPoints(native_window);
  viewport_.safe_left_drawable =
      viewport_.safe_area_points.left * viewport_.drawable_scale_x;
  viewport_.safe_top_drawable =
      viewport_.safe_area_points.top * viewport_.drawable_scale_y;
  viewport_.safe_right_drawable =
      viewport_.safe_area_points.right * viewport_.drawable_scale_x;
  viewport_.safe_bottom_drawable =
      viewport_.safe_area_points.bottom * viewport_.drawable_scale_y;
}

TouchPoint MobileInputController::ResolvePoint(const float normalized_x,
                                               const float normalized_y) const
    noexcept {
  const float x = std::clamp(normalized_x, 0.0F, 1.0F);
  const float y = std::clamp(normalized_y, 0.0F, 1.0F);
  return {
      .logical_x = x * static_cast<float>(viewport_.logical_width),
      .logical_y = y * static_cast<float>(viewport_.logical_height),
      .drawable_x = x * static_cast<float>(viewport_.drawable_width),
      .drawable_y = y * static_cast<float>(viewport_.drawable_height),
  };
}

TouchContact* MobileInputController::BeginContact(
    const SDL_TouchFingerEvent& event) {
  if (auto* const existing = FindContact(event.fingerId); existing != nullptr) {
    existing->previous = existing->current;
    existing->current = ResolvePoint(event.x, event.y);
    existing->pressure = event.pressure;
    return existing;
  }
  for (auto& slot : contacts_) {
    if (slot.has_value()) {
      continue;
    }
    const TouchPoint point = ResolvePoint(event.x, event.y);
    slot = TouchContact{
        .touch_id = event.touchId,
        .finger_id = event.fingerId,
        .owner = TouchOwner::kUnassigned,
        .start = point,
        .previous = point,
        .current = point,
        .started_at_ms = event.timestamp,
        .pressure = event.pressure,
    };
    return &*slot;
  }

  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kWarn,
      "Mobile input contact capacity exceeded; ignoring finger=" +
          std::to_string(event.fingerId));
  return nullptr;
}

TouchContact* MobileInputController::UpdateContact(
    const SDL_TouchFingerEvent& event) {
  auto* const contact = FindContact(event.fingerId);
  if (contact == nullptr) {
    return nullptr;
  }
  contact->previous = contact->current;
  contact->current = ResolvePoint(event.x, event.y);
  contact->pressure = event.pressure;
  return contact;
}

TouchContact* MobileInputController::FindContact(
    const SDL_FingerID finger_id) noexcept {
  for (auto& slot : contacts_) {
    if (slot.has_value() && slot->finger_id == finger_id) {
      return &*slot;
    }
  }
  return nullptr;
}

const TouchContact* MobileInputController::FindContact(
    const SDL_FingerID finger_id) const noexcept {
  for (const auto& slot : contacts_) {
    if (slot.has_value() && slot->finger_id == finger_id) {
      return &*slot;
    }
  }
  return nullptr;
}

TouchContact* MobileInputController::FindContactByOwner(
    const TouchOwner owner) noexcept {
  for (auto& slot : contacts_) {
    if (slot.has_value() && slot->owner == owner) {
      return &*slot;
    }
  }
  return nullptr;
}

const TouchContact* MobileInputController::FindContactByOwner(
    const TouchOwner owner) const noexcept {
  for (const auto& slot : contacts_) {
    if (slot.has_value() && slot->owner == owner) {
      return &*slot;
    }
  }
  return nullptr;
}

TouchContact* MobileInputController::FindOtherContactByOwner(
    const TouchOwner owner, const SDL_FingerID finger_id) noexcept {
  for (auto& slot : contacts_) {
    if (slot.has_value() && slot->finger_id != finger_id &&
        slot->owner == owner) {
      return &*slot;
    }
  }
  return nullptr;
}

bool MobileInputController::HasOwner(const TouchOwner owner) const noexcept {
  for (const auto& slot : contacts_) {
    if (slot.has_value() && slot->owner == owner) {
      return true;
    }
  }
  return false;
}

std::optional<TouchContact> MobileInputController::EndContact(
    const SDL_TouchFingerEvent& event) {
  for (auto& slot : contacts_) {
    if (!slot.has_value() || slot->finger_id != event.fingerId) {
      continue;
    }
    slot->previous = slot->current;
    slot->current = ResolvePoint(event.x, event.y);
    slot->pressure = event.pressure;
    auto ended = std::move(slot);
    slot.reset();
    return ended;
  }
  return std::nullopt;
}

std::vector<TouchContact> MobileInputController::TakeAllContacts() {
  std::vector<TouchContact> contacts;
  contacts.reserve(kMaximumContacts);
  for (auto& slot : contacts_) {
    if (slot.has_value()) {
      contacts.push_back(std::move(*slot));
      slot.reset();
    }
  }
  return contacts;
}

}
