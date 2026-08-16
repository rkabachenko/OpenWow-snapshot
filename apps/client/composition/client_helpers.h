#pragma once

#include "openwow/data/login_resource_validator.h"
#include "openwow/ui/glue/glue_widget_runtime.h"
#include "openwow/ui/screens/login_screen.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/vfs/virtual_file_system.h"

#include <SDL2/SDL.h>

#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "openwow/foundation/text/ascii.h"

namespace openwow::ui::glue {
class GlueLuaRuntime;
}

namespace openwow::client {

inline void GetDrawableSize(SDL_Window* window, int* width, int* height) {
  SDL_GL_GetDrawableSize(window, width, height);
  if (*width <= 0 || *height <= 0) {
    SDL_GetWindowSize(window, width, height);
  }
}

inline void GetLogicalSize(SDL_Window* window, int* width, int* height) {
  SDL_GetWindowSize(window, width, height);
}

inline void ScaleMouseToDrawable(SDL_Window* window, int& x, int& y) {
  int logical_w = 0;
  int logical_h = 0;
  int drawable_w = 0;
  int drawable_h = 0;
  SDL_GetWindowSize(window, &logical_w, &logical_h);
  GetDrawableSize(window, &drawable_w, &drawable_h);
  if (logical_w > 0 && logical_h > 0) {
    x = x * drawable_w / logical_w;
    y = y * drawable_h / logical_h;
  }
}

using openwow::text::ToLowerAscii;

bool PointInWidget(const openwow::ui::glue::GlueWidgetState& widget, int x, int y);

std::optional<openwow::ui::glue::GlueWidgetState> FindFirstVisibleWidget(
    const openwow::ui::glue::GlueWidgetRuntime& runtime,
    std::initializer_list<const char*> names);

void SyncLoginEditText(openwow::ui::glue::GlueLuaRuntime* lua_runtime,
                       const openwow::ui::screens::LoginScreen& login_screen);

void LogVfsMounts(const openwow::vfs::VirtualFileSystem& vfs);

void LogVfsProbe(const openwow::vfs::VirtualFileSystem& vfs,
                 const std::vector<std::string>& virtual_paths);

bool ShouldDumpMpqIndex();

void DumpVfsIndex(const openwow::vfs::VirtualFileSystem& vfs,
                  const std::filesystem::path& out_path);

}
