#include "openwow/ui/display/settings/adapters/platform/display_mode_catalog.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace openwow::ui::display::platform {
namespace {

constexpr std::array<ScreenResolution, 4> kFallbackResolutions{{
    {800, 600},
    {1024, 768},
    {1280, 1024},
    {1600, 1200},
}};
constexpr std::array<ScreenResolution, 7>
    kValidationFallbackResolutions{{
        {640, 480},
        {800, 600},
        {1024, 768},
        {1152, 864},
        {1280, 960},
        {1280, 1024},
        {1600, 1200},
    }};
constexpr std::int64_t kModernRetailDefaultPixelTarget = 4'096'000;

std::int64_t PixelArea(const DisplayMode& mode) {
  return static_cast<std::int64_t>(mode.width) *
         static_cast<std::int64_t>(mode.height);
}

SDL_Window* ActiveDisplayWindow() {
  if (auto* window = SDL_GL_GetCurrentWindow(); window != nullptr) {
    return window;
  }
  if (auto* window = SDL_GetKeyboardFocus(); window != nullptr) {
    return window;
  }
  return SDL_GetMouseFocus();
}

int ActiveDisplayIndex() {
  if (auto* window = ActiveDisplayWindow(); window != nullptr) {
    const int index = SDL_GetWindowDisplayIndex(window);
    if (index >= 0) {
      return index;
    }
  }
  return 0;
}

std::vector<DisplayMode> NormalizeModes(std::vector<DisplayMode> modes) {
  modes.erase(
      std::remove_if(modes.begin(), modes.end(),
                     [](const DisplayMode& mode) {
                       return mode.width < 640 || mode.height < 480 ||
                              mode.bits_per_pixel < 16;
                     }),
      modes.end());
  std::stable_sort(
      modes.begin(), modes.end(),
      [](const DisplayMode& left, const DisplayMode& right) {
        if (PixelArea(left) != PixelArea(right)) {
          return PixelArea(left) < PixelArea(right);
        }
        if (left.width != right.width) {
          return left.width < right.width;
        }
        return left.height < right.height;
      });
  return modes;
}

std::vector<DisplayMode> EnumerateModes() {
  if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0 &&
      SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
    return {};
  }
  const int display = ActiveDisplayIndex();
  if (display < 0 || display >= SDL_GetNumVideoDisplays()) {
    return {};
  }

  std::vector<DisplayMode> result;
  const int count = SDL_GetNumDisplayModes(display);
  result.reserve(count > 0 ? static_cast<std::size_t>(count) : 0);
  for (int index = 0; index < count; ++index) {
    SDL_DisplayMode mode{};
    if (SDL_GetDisplayMode(display, index, &mode) == 0) {
      result.push_back(
          {mode.w, mode.h, static_cast<int>(SDL_BITSPERPIXEL(mode.format)),
           mode.refresh_rate});
    }
  }
#if defined(__APPLE__)
  result = detail::NormalizeI386GllDisplayModes(std::move(result));
#endif
  return NormalizeModes(std::move(result));
}

std::optional<DisplayMode> QueryCurrentMode() {
  if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0 &&
      SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
    return std::nullopt;
  }
  SDL_DisplayMode mode{};
  if (SDL_GetCurrentDisplayMode(ActiveDisplayIndex(), &mode) != 0) {
    return std::nullopt;
  }

  if (auto* window = ActiveDisplayWindow(); window != nullptr) {
    int logical_width = 0;
    int logical_height = 0;
    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GetWindowSize(window, &logical_width, &logical_height);
    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
    if (logical_width > 0 && logical_height > 0 && drawable_width > 0 &&
        drawable_height > 0) {
      mode.w = static_cast<int>(std::lround(
          static_cast<double>(mode.w) * drawable_width / logical_width));
      mode.h = static_cast<int>(std::lround(
          static_cast<double>(mode.h) * drawable_height / logical_height));
    }
  }
  return DisplayMode{
      mode.w,
      mode.h,
#if defined(__APPLE__)
      32,
#else
      SDL_BITSPERPIXEL(mode.format),
#endif
      mode.refresh_rate,
  };
}

}

namespace detail {

std::vector<DisplayMode> NormalizeI386GllDisplayModes(
    std::vector<DisplayMode> modes) {
  modes.erase(
      std::remove_if(
          modes.begin(), modes.end(),
          [](const DisplayMode& mode) {
            const auto width = static_cast<std::uint32_t>(mode.width);
            const auto height = static_cast<std::uint32_t>(mode.height);
            const auto refresh =
                static_cast<std::uint32_t>(mode.refresh_rate);
            return width <= 639U || height <= 399U ||
                   refresh - 1U <= 57U;
          }),
      modes.end());
  for (auto& mode : modes) {
    mode.bits_per_pixel = 32;
    mode.refresh_rate = 0;
  }
  std::sort(modes.begin(), modes.end(),
            [](const DisplayMode& left, const DisplayMode& right) {
              return left.width != right.width
                         ? left.width < right.width
                         : left.height < right.height;
            });
  modes.erase(std::unique(modes.begin(), modes.end(),
                          [](const DisplayMode& left,
                             const DisplayMode& right) {
                            return left.width == right.width &&
                                   left.height == right.height;
                          }),
              modes.end());
  return modes;
}

}

std::vector<DisplayMode> AvailableDisplayModes() {
  return EnumerateModes();
}

std::optional<DisplayMode> CurrentDisplayMode() {
  return QueryCurrentMode();
}

ScreenResolution DefaultScreenResolution() {
  const auto current = QueryCurrentMode();
  if (!current || current->width <= 0 || current->height <= 0) {
    return {1024, 768};
  }

  const double display_aspect = std::clamp(
      static_cast<double>(current->width) / current->height, 1.25,
      16.0 / 9.0);
  ScreenResolution selected{1024, 768};
  double best_score = std::numeric_limits<double>::infinity();
  for (const auto& mode : EnumerateModes()) {
    if (mode.width > current->width || mode.height > current->height ||
        mode.width <= 0 || mode.height <= 0) {
      continue;
    }
    const double aspect = static_cast<double>(mode.width) / mode.height;
    if (aspect < 1.248 || aspect > 1.7797778) {
      continue;
    }
    const double area_error =
        std::abs(static_cast<double>(kModernRetailDefaultPixelTarget -
                                    PixelArea(mode))) /
        kModernRetailDefaultPixelTarget;
    const double aspect_error =
        std::abs(display_aspect - aspect) / display_aspect * 1.01;
    const double score = std::max(area_error, aspect_error);
    if (score < best_score) {
      selected = {mode.width, mode.height};
      best_score = score;
    }
  }
  return selected;
}

std::vector<ScreenResolution> BuildFullscreenResolutionCatalog(
    const bool widescreen_enabled,
    const bool include_640x480_fallback) {
  std::vector<ScreenResolution> result;
  if (widescreen_enabled) {
    const int minimum_width = include_640x480_fallback ? 640 : 641;
    const int minimum_height = include_640x480_fallback ? 480 : 481;
    for (const auto& mode : EnumerateModes()) {
      if (mode.width < minimum_width || mode.height < minimum_height ||
          static_cast<double>(mode.width) / mode.height < 1.248) {
        continue;
      }
      const ScreenResolution candidate{mode.width, mode.height};
      if (result.empty() || result.back().width != candidate.width ||
          result.back().height != candidate.height) {
        result.push_back(candidate);
      }
    }
  }
  if (result.empty()) {
    const auto fallback =
        include_640x480_fallback
            ? std::span<const ScreenResolution>(
                  kValidationFallbackResolutions)
            : std::span<const ScreenResolution>(kFallbackResolutions);
    result.assign(fallback.begin(), fallback.end());
  }
  return result;
}

}
