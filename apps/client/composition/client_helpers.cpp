#include "client_helpers.h"

#include "openwow/ui/glue/glue_lua_runtime.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

namespace openwow::client {

bool PointInWidget(const openwow::ui::glue::GlueWidgetState& widget, int x, int y) {
  if (widget.width <= 0 || widget.height <= 0) {
    return false;
  }
  return x >= widget.x && x < (widget.x + widget.width)
         && y >= widget.y && y < (widget.y + widget.height);
}

std::optional<openwow::ui::glue::GlueWidgetState> FindFirstVisibleWidget(
    const openwow::ui::glue::GlueWidgetRuntime& runtime,
    std::initializer_list<const char*> names) {
  for (const auto* name : names) {
    if (name == nullptr) {
      continue;
    }
    const std::string widget_name(name);
    if (!runtime.IsVisible(widget_name)) {
      continue;
    }
    if (auto widget = runtime.GetWidget(widget_name); widget.has_value()) {
      return widget;
    }
  }
  return std::nullopt;
}

void SyncLoginEditText(openwow::ui::glue::GlueLuaRuntime* lua_runtime,
                       const openwow::ui::screens::LoginScreen& login_screen) {
  if (lua_runtime == nullptr) {
    return;
  }
  (void)lua_runtime->SetEditBoxTextProgrammatically(
      "AccountLoginAccountEdit", login_screen.username());
  (void)lua_runtime->SetEditBoxTextProgrammatically(
      "AccountLoginPasswordEdit", login_screen.password());
}

const char* MountKindToString(openwow::vfs::MountKind kind) {
  switch (kind) {
    case openwow::vfs::MountKind::kFilesystem: return "filesystem";
    case openwow::vfs::MountKind::kMpqArchive: return "mpq-archive";
    case openwow::vfs::MountKind::kEnhancedOverride: return "enhanced-override";
  }
  return "unknown";
}

void LogVfsMounts(const openwow::vfs::VirtualFileSystem& vfs) {
  const auto mounts = vfs.mounts();
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "VFS mount count: " + std::to_string(mounts.size()));
  for (const auto& mount : mounts) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kInfo,
        "VFS mount [" + mount.id + "] kind=" + MountKindToString(mount.kind)
            + " priority=" + std::to_string(mount.priority)
            + " enabled=" + std::string(mount.enabled ? "true" : "false")
            + " root=" + mount.source_root.string());
  }
}

void LogVfsProbe(const openwow::vfs::VirtualFileSystem& vfs,
                 const std::vector<std::string>& virtual_paths) {
  for (const auto& virtual_path : virtual_paths) {
    const auto resolved = vfs.Resolve(virtual_path);
    if (resolved.has_value()) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                         "VFS probe OK: " + virtual_path + " -> " + resolved->string());
    } else {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "VFS probe missing: " + virtual_path);
    }
  }
}

bool ShouldDumpMpqIndex() {
  const char* value = std::getenv("OPENWOW_DUMP_MPQ_INDEX");
  if (value == nullptr) {
    return false;
  }
  return std::string(value) == "1" || std::string(value) == "true";
}

void DumpVfsIndex(const openwow::vfs::VirtualFileSystem& vfs,
                  const std::filesystem::path& out_path) {
  std::error_code ec;
  std::filesystem::create_directories(out_path.parent_path(), ec);
  if (ec) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "MPQ index dump failed to create dir: " + out_path.parent_path().string());
    return;
  }
  std::ofstream out(out_path, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "MPQ index dump failed to open: " + out_path.string());
    return;
  }
  std::size_t count = 0;
  for (const auto& file : vfs.EnumerateFiles("/", true)) {
    out << file.generic_string() << "\n";
    ++count;
  }
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "Wrote VFS index: " + out_path.string() + " entries=" + std::to_string(count));
}

}
