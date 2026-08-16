#pragma once

#include "openwow/vfs/virtual_file_system.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::ui::glue {

enum class GlueScriptKind : std::uint8_t {
  kInlineLua = 0,
  kFunctionName = 1,
};

struct GlueScriptBinding {
  GlueScriptKind kind{GlueScriptKind::kInlineLua};
  std::string value;
};

class GlueBindingRegistry {
 public:
  void Clear();
  void LoadFromXml(const openwow::vfs::VirtualFileSystem& vfs,
                   const std::vector<std::string>& xml_candidates);

  void LoadBindingsFromXmlText(const std::string& xml_text);
  std::vector<GlueScriptBinding> BindingsFor(const std::string& widget_name,
                                            const std::string& event_name) const;
  bool HasBindingsFor(const std::string& widget_name, const std::string& event_name) const;
  std::vector<std::string> WidgetsWithEvent(const std::string& event_name) const;

 private:
  static std::string NormalizeFunctionName(const std::string& raw);
  std::unordered_map<std::string, std::unordered_map<std::string, std::vector<GlueScriptBinding>>> map_;
  std::unordered_map<std::string, std::vector<std::string>> widgets_by_event_;
};

}
