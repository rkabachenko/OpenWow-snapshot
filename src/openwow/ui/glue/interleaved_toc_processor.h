#pragma once

#include "openwow/ui/glue/glue_binding_registry.h"
#include "openwow/ui/glue/glue_lua_runtime.h"
#include "openwow/ui/glue/glue_toc_loader.h"
#include "openwow/ui/glue/glue_widget_runtime.h"
#include "openwow/vfs/virtual_file_system.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace openwow::ui::glue {

using OnWidgetCreatedCallback =
    std::function<void(const std::string& widget_name, const std::string& event_source)>;

struct InterleavedProcessResult {
  bool ok{false};
  std::string error;
  std::uint32_t xml_files_processed{0};
  std::uint32_t lua_files_executed{0};
  std::uint32_t widgets_created{0};
  std::uint32_t onload_fired{0};
};

class InterleavedTocProcessor {
 public:
  InterleavedTocProcessor(const openwow::vfs::VirtualFileSystem& vfs,
                          GlueLuaRuntime& lua_runtime,
                          GlueWidgetRuntime& widget_runtime,
                          GlueBindingRegistry& binding_registry);

  void SetOnWidgetCreated(OnWidgetCreatedCallback callback);

  InterleavedProcessResult ProcessToc(const std::vector<TocEntry>& entries);

 private:

  void ProcessFile(const std::string& path);

  void ProcessLuaFile(const std::string& path);

  void ProcessXmlFile(const std::string& path);

  std::string ResolveRelativePath(const std::string& base_xml_path,
                                  const std::string& relative_ref) const;

  const openwow::vfs::VirtualFileSystem& vfs_;
  GlueLuaRuntime& lua_runtime_;
  GlueWidgetRuntime& widget_runtime_;
  GlueBindingRegistry& binding_registry_;
  OnWidgetCreatedCallback on_widget_created_;

  std::unordered_set<std::string> processing_stack_;

  std::uint32_t xml_files_processed_{0};
  std::uint32_t lua_files_executed_{0};
  std::uint32_t widgets_created_{0};
  std::uint32_t onload_fired_{0};
  std::vector<std::string> errors_;
};

}
