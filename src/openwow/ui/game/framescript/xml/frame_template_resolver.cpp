#include "openwow/ui/game/framescript/xml/frame_template_resolver.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/framexml/framexml_parser.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/foundation/text/ascii.h"

#include <lua.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::game::frame_api {

std::string TrimTemplateToken(std::string_view token) {
  while (!token.empty() &&
         std::isspace(static_cast<unsigned char>(token.front())) != 0) {
    token.remove_prefix(1);
  }
  while (!token.empty() &&
         std::isspace(static_cast<unsigned char>(token.back())) != 0) {
    token.remove_suffix(1);
  }
  return std::string(token);
}

std::vector<std::string> SplitTemplateList(std::string_view inherits) {
  std::vector<std::string> names;
  while (!inherits.empty()) {
    const std::size_t comma = inherits.find(',');
    const std::string name =
        TrimTemplateToken(inherits.substr(0, comma));
    if (!name.empty()) {
      names.push_back(name);
    }
    if (comma == std::string_view::npos) {
      break;
    }
    inherits.remove_prefix(comma + 1);
  }
  return names;
}

bool TemplateKindMatches(const openwow::ui::framexml::UiFrame &frame,
                         const char *expected_kind) {
  return expected_kind == nullptr || expected_kind[0] == '\0' ||
         openwow::text::EqualsIgnoreCaseAscii(frame.kind, expected_kind);
}

bool ResolveTemplateNodeRecursive(lua_State *L,
                                  const std::string &name,
                                  const char *expected_kind,
                                  const std::string &root_name,
                                  std::vector<std::string> *active_stack,
                                  TemplateResolveResult *result) {
  if (result == nullptr || active_stack == nullptr) {
    return false;
  }

  if (std::find(active_stack->begin(), active_stack->end(), name) !=
      active_stack->end()) {
    result->failed_name = root_name;
    result->recursive = true;
    return false;
  }

  const auto *frame = openwow::ui::framexml::GetVirtualTemplate(name);
  if (frame == nullptr || !TemplateKindMatches(*frame, expected_kind)) {
    result->failed_name = name;
    result->recursive = false;
    return false;
  }

  active_stack->push_back(name);
  for (const std::string &inherited_name : SplitTemplateList(frame->inherits)) {

    const bool may_name_font =
        expected_kind != nullptr &&
        openwow::text::EqualsIgnoreCaseAscii(expected_kind, "FontString");
    if (may_name_font && PushNamedFontObject(L, inherited_name.c_str())) {
      lua_pop(L, 1);
      continue;
    }
    if (may_name_font) {
      lua_pop(L, 1);
    }
    if (!ResolveTemplateNodeRecursive(L, inherited_name, expected_kind,
                                      root_name, active_stack, result)) {
      active_stack->pop_back();
      return false;
    }
  }
  active_stack->pop_back();

  result->base_to_derived.push_back(frame);
  return true;
}

TemplateResolveResult ResolveTemplateNodes(lua_State *L, const char *inherits,
                                           const char *expected_kind) {
  TemplateResolveResult result;
  if (inherits == nullptr || inherits[0] == '\0') {
    return result;
  }

  const std::string root_name = TrimTemplateToken(inherits);
  const std::vector<std::string> roots = SplitTemplateList(root_name);
  if (roots.empty()) {
    result.failed_name = root_name;
    return result;
  }

  std::vector<std::string> active_stack;
  for (const std::string &name : roots) {
    if (!ResolveTemplateNodeRecursive(L, name, expected_kind, root_name,
                                      &active_stack, &result)) {
      return result;
    }
  }
  return result;
}

int RaiseInheritedTemplateError(lua_State *L, int owner_index,
                                const char *method_name,
                                const TemplateResolveResult &result) {
  const char* owner_name =
      lua_adapter::ScriptObjectDisplayName(L, owner_index);
  if (result.recursive) {
    lua_pushfstring(L, "%s:%s(): Recursively inherited node \"%s\"",
                    owner_name, method_name, result.failed_name.c_str());
    return lua_error(L);
  }
  lua_pushfstring(L, "%s:%s(): Couldn't find inherited node \"%s\"",
                  owner_name, method_name, result.failed_name.c_str());
  return lua_error(L);
}

}
