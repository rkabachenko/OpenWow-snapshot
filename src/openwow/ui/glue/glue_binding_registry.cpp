#include "openwow/ui/glue/glue_binding_registry.h"

#include "openwow/ui/framexml/framexml_parser_detail.h"
#include "openwow/ui/xml/frame_xml_parser.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace openwow::ui::glue {

using openwow::text::ToLowerAscii;
using openwow::text::Trim;

namespace {

using BindingMap =
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<GlueScriptBinding>>>;
using WidgetsByEvent = std::unordered_map<std::string, std::vector<std::string>>;
using WidgetsSeenByEvent = std::unordered_map<std::string, std::unordered_set<std::string>>;

std::string ResolveParentToken(std::string value, const std::string& parent_name) {
  if (value.empty()) {
    return {};
  }
  const std::string needle = "$parent";
  std::string::size_type pos = 0;
  while ((pos = value.find(needle, pos)) != std::string::npos) {
    value.replace(pos, needle.size(), parent_name);
    pos += parent_name.size();
  }
  return Trim(value);
}

std::string CanonicalScriptEvent(std::string_view tag) {
  return openwow::ui::framexml::detail::CanonicalizeScriptEvent(tag);
}

std::string MakeInvalidFunctionAttributeScript(const std::string& raw) {
  std::string escaped;
  escaped.reserve(raw.size());
  for (const char ch : raw) {
    if (ch == '\\' || ch == '"') {
      escaped.push_back('\\');
    }
    if (ch == '\n' || ch == '\r') {
      escaped.push_back(' ');
      continue;
    }
    escaped.push_back(ch);
  }
  return "error(\"Invalid XML function attribute: " + escaped + "\", 2)";
}

std::string NormalizeFunctionNameImpl(const std::string& raw) {
  auto out = Trim(raw);
  if (out.empty()) {
    return {};
  }
  const auto is_start = [](const unsigned char ch) {
    return std::isalpha(ch) != 0 || ch == '_';
  };
  const auto is_rest = [](const unsigned char ch) {
    return std::isalnum(ch) != 0 || ch == '_' || ch == ':' || ch == '.';
  };
  if (!is_start(static_cast<unsigned char>(out.front()))) {
    return {};
  }
  for (std::size_t i = 1; i < out.size(); ++i) {
    if (!is_rest(static_cast<unsigned char>(out[i]))) {
      return {};
    }
  }
  return out;
}

void RegisterBinding(const std::string& widget_name, const std::string& event_name,
                     const std::string& function_attr, const std::string& script_body,
                     BindingMap* map, WidgetsByEvent* widgets_by_event,
                     WidgetsSeenByEvent* widgets_seen_by_event) {
  if (widget_name.empty() || event_name.empty() || map == nullptr || widgets_by_event == nullptr ||
      widgets_seen_by_event == nullptr) {
    return;
  }

  GlueScriptBinding binding;
  const std::string function_name = NormalizeFunctionNameImpl(function_attr);
  if (!Trim(function_attr).empty()) {
    if (!function_name.empty()) {
      binding.kind = GlueScriptKind::kFunctionName;
      binding.value = function_name;
    } else {
      binding.kind = GlueScriptKind::kInlineLua;
      binding.value = MakeInvalidFunctionAttributeScript(function_attr);
    }
  } else {
    const std::string body = Trim(script_body);
    if (body.empty()) {
      return;
    }
    if (const auto fn = NormalizeFunctionNameImpl(body); !fn.empty()) {
      binding.kind = GlueScriptKind::kFunctionName;
      binding.value = fn;
    } else {
      binding.kind = GlueScriptKind::kInlineLua;
      binding.value = body;
    }
  }

  if (binding.value.empty()) {
    return;
  }
  (*map)[widget_name][event_name].push_back(binding);
  if ((*widgets_seen_by_event)[event_name].insert(widget_name).second) {
    (*widgets_by_event)[event_name].push_back(widget_name);
  }
}

void RegisterScriptsForWidget(const openwow::ui::xml::XMLNode& widget_node,
                              const std::string& widget_name, BindingMap* map,
                              WidgetsByEvent* widgets_by_event,
                              WidgetsSeenByEvent* widgets_seen_by_event) {
  if (widget_name.empty()) {
    return;
  }

  for (const auto& child : widget_node.children) {
    if (ToLowerAscii(child.tag) != "scripts") {
      continue;
    }
    for (const auto& handler : child.children) {
      const std::string event_name = CanonicalScriptEvent(handler.tag);
      if (event_name.empty()) {
        continue;
      }
      RegisterBinding(widget_name, event_name, handler.GetAttr("function"), handler.text,
                      map, widgets_by_event, widgets_seen_by_event);
    }
  }
}

void VisitXmlNodeForBindings(const openwow::ui::xml::XMLNode& node,
                             const std::string& lexical_parent_name, BindingMap* map,
                             WidgetsByEvent* widgets_by_event,
                             WidgetsSeenByEvent* widgets_seen_by_event) {
  std::string child_lexical_parent = lexical_parent_name;

  if (openwow::ui::framexml::detail::IsRuntimeWidgetTag(node.tag)) {
    const std::string explicit_parent_attr = node.GetAttr("parent");
    const std::string effective_parent_name =
        explicit_parent_attr.empty() ? lexical_parent_name
                                     : ResolveParentToken(explicit_parent_attr, lexical_parent_name);
    const std::string widget_name =
        ResolveParentToken(node.GetAttr("name"), effective_parent_name);
    RegisterScriptsForWidget(node, widget_name, map, widgets_by_event, widgets_seen_by_event);
    if (!widget_name.empty()) {
      child_lexical_parent = widget_name;
    }
  }

  for (const auto& child : node.children) {
    VisitXmlNodeForBindings(child, child_lexical_parent, map, widgets_by_event,
                            widgets_seen_by_event);
  }
}

bool LoadBindingsFromXmlTreeText(const std::string& xml_text, BindingMap* map,
                                 WidgetsByEvent* widgets_by_event,
                                 WidgetsSeenByEvent* widgets_seen_by_event,
                                 std::string* error) {
  if (xml_text.empty()) {
    return true;
  }
  openwow::ui::xml::XMLNode root;
  if (!openwow::ui::xml::FrameXMLParser::ParseDocument(xml_text, &root, error)) {
    return false;
  }
  VisitXmlNodeForBindings(root, {}, map, widgets_by_event, widgets_seen_by_event);
  return true;
}

}

void GlueBindingRegistry::Clear() {
  map_.clear();
  widgets_by_event_.clear();
}

void GlueBindingRegistry::LoadFromXml(const openwow::vfs::VirtualFileSystem& vfs,
                                      const std::vector<std::string>& xml_candidates) {
  Clear();

  std::unordered_map<std::string, std::unordered_set<std::string>> widgets_seen_by_event;

  for (const auto& candidate : xml_candidates) {
    const auto xml = vfs.ReadTextFile(candidate);
    if (!xml) {
      continue;
    }
    std::string error;
    if (!LoadBindingsFromXmlTreeText(*xml, &map_, &widgets_by_event_, &widgets_seen_by_event,
                                     &error)) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "GlueBindingRegistry: failed to parse " + candidate +
                             (error.empty() ? std::string{} : ": " + error));
    }
  }
}

void GlueBindingRegistry::LoadBindingsFromXmlText(const std::string& xml_text) {
  if (xml_text.empty()) return;

  std::unordered_map<std::string, std::unordered_set<std::string>> widgets_seen_by_event;
  std::string error;
  if (!LoadBindingsFromXmlTreeText(xml_text, &map_, &widgets_by_event_, &widgets_seen_by_event,
                                   &error)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueBindingRegistry: failed to parse XML text" +
                           (error.empty() ? std::string{} : ": " + error));
  }
}

std::vector<GlueScriptBinding> GlueBindingRegistry::BindingsFor(const std::string& widget_name,
                                                                const std::string& event_name) const {
  const auto widget_it = map_.find(widget_name);
  if (widget_it == map_.end()) {
    return {};
  }
  const auto event_it = widget_it->second.find(event_name);
  if (event_it == widget_it->second.end()) {
    return {};
  }
  return event_it->second;
}

bool GlueBindingRegistry::HasBindingsFor(const std::string& widget_name,
                                        const std::string& event_name) const {
  const auto widget_it = map_.find(widget_name);
  if (widget_it == map_.end()) {
    return false;
  }
  const auto event_it = widget_it->second.find(event_name);
  if (event_it == widget_it->second.end()) {
    return false;
  }
  return !event_it->second.empty();
}

std::vector<std::string> GlueBindingRegistry::WidgetsWithEvent(const std::string& event_name) const {
  const auto it = widgets_by_event_.find(event_name);
  if (it == widgets_by_event_.end()) {
    return {};
  }
  return it->second;
}

std::string GlueBindingRegistry::NormalizeFunctionName(const std::string& raw) {
  return NormalizeFunctionNameImpl(raw);
}

}
