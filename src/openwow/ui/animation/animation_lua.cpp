#include "openwow/ui/lua_c_api_convenience.h"

#include "openwow/ui/animation/animation_lua.h"
#include "openwow/ui/animation/animation_coordinate_space.h"
#include "openwow/ui/animation/animation_lua_helpers.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/lua_cpu_profiler.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_post_hook_closure.h"
#include "openwow/ui/lua_taint_api.h"
#include "openwow/ui/widgets/script_object.h"
#include "openwow/ui/widgets/script_region.h"

#include <algorithm>

#ifdef lua_pushcfunction
#undef lua_pushcfunction
#endif
static inline void lua_pushcfunction(lua_State *L, lua_CFunction f) {
  lua_pushcclosure(L, f, 0);
}
#include "openwow/ui/animation/alpha_anim.h"
#include "openwow/ui/animation/animation.h"
#include "openwow/ui/animation/animation_group.h"
#include "openwow/ui/animation/animation_xml.h"
#include "openwow/ui/animation/animation_types.h"
#include "openwow/ui/animation/path_anim.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/ui/animation/rotation_anim.h"
#include "openwow/ui/animation/scale_anim.h"
#include "openwow/ui/animation/translation_anim.h"
#include "openwow/ui/framexml/framexml_parser.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/ui/ui_script_helpers.h"

extern "C" {
#include <lua.hpp>
#include <lua.hpp>
#include <lua.hpp>
}

#include <algorithm>
#include <cctype>
#include <cstring>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace openwow::ui::anim {

static bool IcaseEqual(const char *lhs, const char *rhs);
static constexpr const char *kControlPointMethodTableRegistryKey =
    "openwow.control_point_method_table";
static int CreateAnimationGroupTableOnRegion(lua_State* L,
                                              int region_idx,
                                             const char* name,
                                             const char* inherits_from = nullptr);

namespace {

constexpr const char* kXmlAnimationGroupsLoadedField = "__ow_xml_animation_groups_loaded";

template <typename ScriptOwner>
void AttachXmlScriptHandlers(lua_State *L, ScriptOwner *owner,
                             const std::vector<openwow::ui::framexml::ScriptHandler> &handlers,
                             const openwow::ui::framexml::XmlScriptOwner owner_kind,
                             openwow::ui::framexml::XmlScriptCache *script_cache) {
  if (L == nullptr || owner == nullptr || handlers.empty()) {
    return;
  }

  openwow::ui::framexml::XmlScriptCache local_cache;
  auto &cache = script_cache != nullptr ? *script_cache : local_cache;
  for (const auto &handler : handlers) {
    if (openwow::ui::framexml::GetXmlScriptWrapperFormat(owner_kind, handler.event) == nullptr) {
      continue;
    }

    std::string error;

    if (!cache.PushResolvedHandler(L, handler, owner_kind, &error)) {
      owner->SetScriptRef(handler.event, L, LUA_NOREF, 0);
      continue;
    }

    owner->SetScriptRef(handler.event, L, luaL_ref(L, LUA_REGISTRYINDEX), 0);
  }
}

}

static AnimationGroup *TryGetGroupPtr(lua_State *L, int idx) {
  idx = lua_absindex(L, idx);
  lua_getfield(L, idx, "__ow_anim_group_ptr");
  auto *ptr = static_cast<AnimationGroup *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return ptr;
}

static AnimationGroup *TryPushGlobalAnimationGroup(lua_State *L,
                                                   const char *global_name,
                                                   int *group_idx) {
  if (group_idx != nullptr) {
    *group_idx = 0;
  }
  if (global_name == nullptr || global_name[0] == '\0') {
    return nullptr;
  }

  lua_getglobal(L, global_name);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return nullptr;
  }

  auto *group = TryGetGroupPtr(L, -1);
  if (group == nullptr) {
    lua_pop(L, 1);
    return nullptr;
  }

  if (group_idx != nullptr) {
    *group_idx = lua_absindex(L, -1);
  }
  return group;
}

static AnimationGroup *GetGroupPtrChecked(lua_State *L, int idx) {
  idx = lua_absindex(L, idx);
  if (!lua_istable(L, idx)) {
    luaL_error(L, "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
    return nullptr;
  }

  lua_getfield(L, idx, "__ow_type");
  const char *type = lua_tostring(L, -1);
  const bool has_type = type != nullptr;
  const bool is_group = has_type && std::strcmp(type, "AnimationGroup") == 0;
  lua_pop(L, 1);

  if (!has_type) {
    luaL_error(L, "Attempt to find 'this' in non-framescript object");
    return nullptr;
  }

  if (!is_group) {
    luaL_error(L, "Wrong object type for member function");
    return nullptr;
  }

  lua_getfield(L, idx, "__ow_anim_group_ptr");
  auto *ptr = static_cast<AnimationGroup *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  if (ptr == nullptr) {
    luaL_error(L, "Attempt to find 'this' in non-framescript object");
    return nullptr;
  }
  return ptr;
}

static Animation *GetAnimPtr(lua_State *L, int idx) {
  idx = lua_absindex(L, idx);
  if (!lua_istable(L, idx)) {
    luaL_error(L, "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
    return nullptr;
  }

  lua_getfield(L, idx, "__ow_type");
  const char *type = lua_tostring(L, -1);
  const bool has_type = type != nullptr;
  const bool is_animation = has_type && std::strcmp(type, "Animation") == 0;
  lua_pop(L, 1);

  if (!has_type) {
    luaL_error(L, "Attempt to find 'this' in non-framescript object");
    return nullptr;
  }

  if (!is_animation) {
    luaL_error(L, "Wrong object type for member function");
    return nullptr;
  }

  lua_getfield(L, idx, "__ow_anim_ptr");
  auto *ptr = static_cast<Animation *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  if (ptr == nullptr) {
    luaL_error(L, "Attempt to find 'this' in non-framescript object");
    return nullptr;
  }
  return ptr;
}

static int PushAnimationRegionParent(lua_State *L) {
  auto *anim = GetAnimPtr(L, 1);
  if (!anim || GetAnimRegionParent(anim) == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  lua_getfield(L, 1, "__ow_group");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_pushnil(L);
    return 1;
  }

  lua_getfield(L, -1, "__ow_parent");
  lua_remove(L, -2);
  return 1;
}

static int PushAnimationParent(lua_State* L) {
  GetAnimPtr(L, 1);
  lua_getfield(L, 1, "__ow_group");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_pushnil(L);
  }
  return 1;
}

static PathAnim *GetPathPtrChecked(lua_State *L, int idx) {
  auto *anim = GetAnimPtr(L, idx);
  if (!anim) {
    return nullptr;
  }
  if (anim->GetKind() != AnimKind::Path) {
    luaL_error(L, "Wrong object type for member function");
    return nullptr;
  }
  return static_cast<PathAnim *>(anim);
}

template <typename AnimT>
static AnimT *GetAnimPtrChecked(lua_State *L, int idx, const AnimKind expected_kind) {
  auto *anim = GetAnimPtr(L, idx);
  if (!anim) {
    return nullptr;
  }
  if (anim->GetKind() != expected_kind) {
    luaL_error(L, "Wrong object type for member function");
    return nullptr;
  }
  return static_cast<AnimT *>(anim);
}

static ScaleAnim *GetScalePtrChecked(lua_State *L, int idx) {
  return GetAnimPtrChecked<ScaleAnim>(L, idx, AnimKind::Scale);
}

static TranslationAnim *GetTranslationPtrChecked(lua_State *L, int idx) {
  return GetAnimPtrChecked<TranslationAnim>(L, idx, AnimKind::Translation);
}

static RotationAnim *GetRotationPtrChecked(lua_State *L, int idx) {
  return GetAnimPtrChecked<RotationAnim>(L, idx, AnimKind::Rotation);
}

static PathAnim *TryGetPathPtr(lua_State *L, int idx) {
  idx = lua_absindex(L, idx);
  if (!lua_istable(L, idx)) {
    return nullptr;
  }

  lua_getfield(L, idx, "__ow_type");
  const char *type = lua_tostring(L, -1);
  const bool is_animation = type != nullptr && std::strcmp(type, "Animation") == 0;
  lua_pop(L, 1);
  if (!is_animation) {
    return nullptr;
  }

  lua_getfield(L, idx, "__ow_anim_ptr");
  auto *anim = static_cast<Animation *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  if (!anim || anim->GetKind() != AnimKind::Path) {
    return nullptr;
  }

  return static_cast<PathAnim *>(anim);
}

static PathAnim *TryPushGlobalPath(lua_State *L,
                                   const char *global_name,
                                   int *path_idx) {
  if (path_idx != nullptr) {
    *path_idx = 0;
  }
  if (global_name == nullptr || global_name[0] == '\0') {
    return nullptr;
  }

  lua_getglobal(L, global_name);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return nullptr;
  }

  auto *path = TryGetPathPtr(L, -1);
  if (path == nullptr) {
    lua_pop(L, 1);
    return nullptr;
  }

  if (path_idx != nullptr) {
    *path_idx = lua_absindex(L, -1);
  }
  return path;
}

static bool IsNonRegionFramescriptType(const char *type_name) {
  return std::strcmp(type_name, "AnimationGroup") == 0 ||
         std::strcmp(type_name, "Animation") == 0 ||
         std::strcmp(type_name, "Alpha") == 0 ||
         std::strcmp(type_name, "Scale") == 0 ||
         std::strcmp(type_name, "Translation") == 0 ||
         std::strcmp(type_name, "Rotation") == 0 ||
         std::strcmp(type_name, "Path") == 0 ||
         std::strcmp(type_name, "ControlPoint") == 0 ||
         std::strcmp(type_name, "Font") == 0;
}

static int ValidateAnimationRegionSelf(lua_State *L, int idx) {
  idx = lua_absindex(L, idx);
  if (!lua_istable(L, idx)) {
    luaL_error(L, "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
    return 0;
  }

  lua_getfield(L, idx, "__ow_type");
  const char *type_name = lua_tostring(L, -1);
  const bool has_type = type_name != nullptr && *type_name != '\0';
  const bool is_region = has_type && !IsNonRegionFramescriptType(type_name);
  lua_pop(L, 1);

  if (!has_type) {
    luaL_error(L, "Attempt to find 'this' in non-framescript object");
    return 0;
  }

  if (!is_region) {
    luaL_error(L, "Wrong object type for member function");
    return 0;
  }

  return idx;
}

static PathControlPoint *GetControlPointPtr(lua_State *L, int idx) {
  idx = lua_absindex(L, idx);
  if (!lua_istable(L, idx)) {
    luaL_error(L, "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
    return nullptr;
  }

  lua_getfield(L, idx, "__ow_type");
  const char *type = lua_tostring(L, -1);
  const bool has_type = type != nullptr;
  const bool is_control_point = has_type && std::strcmp(type, "ControlPoint") == 0;
  lua_pop(L, 1);

  if (!has_type) {
    luaL_error(L, "Attempt to find 'this' in non-framescript object");
    return nullptr;
  }

  if (!is_control_point) {
    luaL_error(L, "Wrong object type for member function");
    return nullptr;
  }

  lua_getfield(L, idx, "__ow_control_point_ptr");
  auto *ptr = static_cast<PathControlPoint *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  if (ptr == nullptr) {
    luaL_error(L, "Attempt to find 'this' in non-framescript object");
    return nullptr;
  }
  return ptr;
}

static const char *ControlPointDisplayName(PathControlPoint *point) {
  if (!point) {
    return "<unnamed>";
  }
  return point->GetName().empty() ? "<unnamed>" : point->GetName().c_str();
}

static const char *PathDisplayName(PathAnim *path) {
  if (!path) {
    return "<unnamed>";
  }
  return path->GetName().empty() ? "<unnamed>" : path->GetName().c_str();
}

static const char *GroupDisplayName(AnimationGroup *group) {
  if (!group) {
    return "<unnamed>";
  }
  return group->GetName().empty() ? "<unnamed>" : group->GetName().c_str();
}

static const char *AnimationDisplayName(const Animation *animation) {
  if (!animation) {
    return "<unnamed>";
  }
  return animation->GetName().empty() ? "<unnamed>" : animation->GetName().c_str();
}

static const char* FrameDisplayName(lua_State* L, int frame_idx) {
  if (L == nullptr) {
    return "<unnamed>";
  }

  frame_idx = lua_absindex(L, frame_idx);
  if (!lua_istable(L, frame_idx)) {
    return "<unnamed>";
  }

  lua_getfield(L, frame_idx, "__ow_name");
  const char* name = lua_tostring(L, -1);
  lua_pop(L, 1);
  if (name == nullptr || name[0] == '\0') {
    return "<unnamed>";
  }
  return name;
}

namespace {

constexpr int kScriptReturnValueStackLimit = 2048;

bool EnsureScriptReturnStackCapacity(lua_State *L, int return_count) {
  if (return_count <= 0) {
    return true;
  }

  if (lua_gettop(L) > kScriptReturnValueStackLimit - return_count) {
    return false;
  }

  return lua_checkstack(L, return_count) != 0;
}

}

static const char* RequireGroupScriptHandlerString(lua_State* L,
                                                   AnimationGroup* group,
                                                   const char* signature) {
  if (lua_isstring(L, 2)) {
    return lua_tostring(L, 2);
  }

  luaL_error(L, "Usage: %s:%s", GroupDisplayName(group), signature);
  return nullptr;
}

static const char* RequireExistingGroupScriptHandler(lua_State* L,
                                                     AnimationGroup* group,
                                                     const char* signature) {
  const char* handler = RequireGroupScriptHandlerString(L, group, signature);
  if (handler == nullptr) {
    return nullptr;
  }

  const char* canonical_handler = NormalizeAnimGroupScriptHandler(handler);
  if (canonical_handler == nullptr) {
    luaL_error(L, "%s doesn't have a \"%s\" script", GroupDisplayName(group), handler);
    return nullptr;
  }

  return canonical_handler;
}

static const char* RequireAnimationScriptHandlerString(lua_State* L,
                                                       const Animation* animation,
                                                       const char* signature) {
  if (lua_isstring(L, 2)) {
    return lua_tostring(L, 2);
  }

  luaL_error(L, "Usage: %s:%s", AnimationDisplayName(animation), signature);
  return nullptr;
}

static const char* RequireExistingAnimationScriptHandler(lua_State* L,
                                                         const Animation* animation,
                                                         const char* signature) {
  const char* handler = RequireAnimationScriptHandlerString(L, animation, signature);
  if (handler == nullptr) {
    return nullptr;
  }

  const char* canonical_handler = NormalizeAnimScriptHandler(handler);
  if (canonical_handler == nullptr) {
    luaL_error(L, "%s doesn't have a \"%s\" script",
               AnimationDisplayName(animation),
               handler);
    return nullptr;
  }

  return canonical_handler;
}

static float ControlPointPixelToStored(float pixels) {
  return PixelAnimationOffsetToStored(pixels);
}

static float ControlPointStoredToPixel(float stored) {
  return StoredAnimationOffsetToPixels(stored);
}

static void EnsureArrayField(lua_State *L, int owner_idx, const char *field) {
  owner_idx = lua_absindex(L, owner_idx);
  lua_getfield(L, owner_idx, field);
  if (lua_istable(L, -1)) {
    return;
  }
  lua_pop(L, 1);
  lua_newtable(L);
  lua_pushvalue(L, -1);
  lua_setfield(L, owner_idx, field);
}

static void AppendToArrayField(lua_State *L, int owner_idx, const char *field, int value_idx) {
  owner_idx = lua_absindex(L, owner_idx);
  value_idx = lua_absindex(L, value_idx);
  EnsureArrayField(L, owner_idx, field);
  int array_idx = lua_absindex(L, -1);
  lua_Integer len = luaL_len(L, array_idx);
  lua_pushvalue(L, value_idx);
  lua_seti(L, array_idx, len + 1);
  lua_pop(L, 1);
}

static void PrependToArrayField(lua_State *L, int owner_idx, const char *field, int value_idx) {
  owner_idx = lua_absindex(L, owner_idx);
  value_idx = lua_absindex(L, value_idx);
  EnsureArrayField(L, owner_idx, field);
  int array_idx = lua_absindex(L, -1);
  const lua_Integer len = luaL_len(L, array_idx);
  for (lua_Integer index = len; index >= 1; --index) {
    lua_geti(L, array_idx, index);
    lua_seti(L, array_idx, index + 1);
  }
  lua_pushvalue(L, value_idx);
  lua_seti(L, array_idx, 1);
  lua_pop(L, 1);
}

static void RemoveControlPointTableFromPath(lua_State *L, int path_idx, PathControlPoint *point) {
  path_idx = lua_absindex(L, path_idx);
  lua_getfield(L, path_idx, "__ow_control_points");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }

  int array_idx = lua_absindex(L, -1);
  const lua_Integer len = luaL_len(L, array_idx);
  lua_Integer remove_at = 0;
  for (lua_Integer index = 1; index <= len; ++index) {
    lua_geti(L, array_idx, index);
    if (lua_istable(L, -1)) {
      lua_getfield(L, -1, "__ow_control_point_ptr");
      auto *current = static_cast<PathControlPoint *>(lua_touserdata(L, -1));
      lua_pop(L, 1);
      if (current == point) {
        remove_at = index;
        lua_pop(L, 1);
        break;
      }
    }
    lua_pop(L, 1);
  }

  if (remove_at != 0) {
    for (lua_Integer index = remove_at; index < len; ++index) {
      lua_geti(L, array_idx, index + 1);
      lua_seti(L, array_idx, index);
    }
    lua_pushnil(L);
    lua_seti(L, array_idx, len);
  }

  lua_pop(L, 1);
}

static void RemoveAnimationTableFromGroup(lua_State *L, int group_idx, Animation *animation) {
  group_idx = lua_absindex(L, group_idx);
  lua_getfield(L, group_idx, "__ow_animations");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }

  const int array_idx = lua_absindex(L, -1);
  const lua_Integer len = luaL_len(L, array_idx);
  lua_Integer remove_at = 0;
  for (lua_Integer index = 1; index <= len; ++index) {
    lua_geti(L, array_idx, index);
    if (lua_istable(L, -1)) {
      lua_getfield(L, -1, "__ow_anim_ptr");
      auto *current = static_cast<Animation *>(lua_touserdata(L, -1));
      lua_pop(L, 1);
      if (current == animation) {
        remove_at = index;
        lua_pop(L, 1);
        break;
      }
    }
    lua_pop(L, 1);
  }

  if (remove_at != 0) {
    for (lua_Integer index = remove_at; index < len; ++index) {
      lua_geti(L, array_idx, index + 1);
      lua_seti(L, array_idx, index);
    }
    lua_pushnil(L);
    lua_seti(L, array_idx, len);
  }

  lua_pop(L, 1);
}

static bool PushAnimationTableFromGroup(lua_State *L, int group_idx, Animation *animation) {
  group_idx = lua_absindex(L, group_idx);
  lua_getfield(L, group_idx, "__ow_animations");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }

  const int array_idx = lua_absindex(L, -1);
  const lua_Integer len = luaL_len(L, array_idx);
  for (lua_Integer index = 1; index <= len; ++index) {
    lua_geti(L, array_idx, index);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      continue;
    }

    lua_getfield(L, -1, "__ow_anim_ptr");
    auto *current = static_cast<Animation *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (current == animation) {
      lua_remove(L, array_idx);
      return true;
    }

    lua_pop(L, 1);
  }

  lua_pop(L, 1);
  return false;
}

static bool PushControlPointTableFromPath(lua_State *L, int path_idx, PathControlPoint *point) {
  path_idx = lua_absindex(L, path_idx);
  lua_getfield(L, path_idx, "__ow_control_points");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }

  int array_idx = lua_absindex(L, -1);
  const lua_Integer len = luaL_len(L, array_idx);
  for (lua_Integer index = 1; index <= len; ++index) {
    lua_geti(L, array_idx, index);
    if (lua_istable(L, -1)) {
      lua_getfield(L, -1, "__ow_control_point_ptr");
      auto *current = static_cast<PathControlPoint *>(lua_touserdata(L, -1));
      lua_pop(L, 1);
      if (current == point) {
        lua_remove(L, array_idx);
        return true;
      }
    }
    lua_pop(L, 1);
  }

  lua_pop(L, 1);
  return false;
}

static void InstallControlPointMethods(lua_State *L, int tbl) {
  tbl = lua_absindex(L, tbl);

  lua_pushcfunction(L, [](lua_State *inner) -> int {
    if (GetControlPointPtr(inner, 1) == nullptr) {
      return 0;
    }
    lua_pushstring(inner, "ControlPoint");
    return 1;
  });
  lua_setfield(L, tbl, "GetObjectType");

  lua_pushcfunction(L, [](lua_State *inner) -> int {
    auto *point = GetControlPointPtr(inner, 1);
    if (!point) {
      return 0;
    }
    if (!lua_isstring(inner, 2)) {
      return luaL_error(inner, "Usage: %s:IsObjectType(\"type\")", ControlPointDisplayName(point));
    }
    const char *type_name = lua_tostring(inner, 2);
    if (IcaseEqual(type_name, "ControlPoint") || IcaseEqual(type_name, "Object")) {
      lua_pushnumber(inner, 1.0);
    } else {
      lua_pushnil(inner);
    }
    return 1;
  });
  lua_setfield(L, tbl, "IsObjectType");

  lua_pushcfunction(L, [](lua_State *inner) -> int {
    auto *point = GetControlPointPtr(inner, 1);
    if (point && !point->GetName().empty()) {
      lua_pushstring(inner, point->GetName().c_str());
    } else {
      lua_pushnil(inner);
    }
    return 1;
  });
  lua_setfield(L, tbl, "GetName");

  lua_pushcfunction(L, [](lua_State *inner) -> int {
    if (GetControlPointPtr(inner, 1) == nullptr) {
      return 0;
    }
    lua_getfield(inner, 1, "__ow_path");
    if (lua_istable(inner, -1)) {
      return 1;
    }
    lua_pop(inner, 1);
    lua_pushnil(inner);
    return 1;
  });
  lua_setfield(L, tbl, "GetParent");

  lua_pushcfunction(L, [](lua_State *inner) -> int {
    auto *point = GetControlPointPtr(inner, 1);
    if (!point || lua_isnoneornil(inner, 2)) {
      return luaL_error(inner, "%s:SetParent(): Cannot set a 'nil' parent for control points",
                        ControlPointDisplayName(point));
    }

    PathAnim *new_parent = nullptr;
    int new_parent_idx = 0;
    if (lua_isstring(inner, 2)) {
      new_parent = TryPushGlobalPath(inner, lua_tostring(inner, 2), &new_parent_idx);
      if (!new_parent) {
        return luaL_error(inner, "%s:SetParent(): Couldn't find Path named '%s'",
                          ControlPointDisplayName(point), lua_tostring(inner, 2));
      }
    } else if (lua_istable(inner, 2)) {
      new_parent = TryGetPathPtr(inner, 2);
      if (new_parent == nullptr) {
        lua_getfield(inner, 2, "__ow_anim_ptr");
        const bool has_animation_ptr = lua_touserdata(inner, -1) != nullptr;
        lua_pop(inner, 1);
        lua_getfield(inner, 2, "__ow_anim_group_ptr");
        const bool has_group_ptr = lua_touserdata(inner, -1) != nullptr;
        lua_pop(inner, 1);
        lua_getfield(inner, 2, "__ow_control_point_ptr");
        const bool has_control_point_ptr = lua_touserdata(inner, -1) != nullptr;
        lua_pop(inner, 1);
        lua_rawgeti(inner, 2, 0);
        const bool has_script_object_this = lua_touserdata(inner, -1) != nullptr;
        lua_pop(inner, 1);
        if (!has_animation_ptr && !has_group_ptr && !has_control_point_ptr &&
            !has_script_object_this) {
          return luaL_error(inner, "%s:SetParent(): Couldn't find 'this' in parent object",
                            ControlPointDisplayName(point));
        }
        return luaL_error(inner, "%s:SetParent(): Wrong parent object type, expected Path",
                          ControlPointDisplayName(point));
      }
      new_parent_idx = lua_absindex(inner, 2);
    } else {
      const char *missing_name = lua_tostring(inner, 2);
      if (!missing_name) {
        missing_name = luaL_typename(inner, 2);
      }
      return luaL_error(inner, "%s:SetParent(): Couldn't find Path named '%s'",
                        ControlPointDisplayName(point), missing_name);
    }

    const int old_order = point->GetOrder();
    if (lua_isnumber(inner, 3)) {
      point->SetOrder(static_cast<int>(lua_tonumber(inner, 3)) - 1, false, false);
    } else {
      int new_order = point->GetOrder();
      if (new_order == -1) {
        new_order = new_parent->GetMaxOrder() + 1;
      }
      point->SetOrder(new_order, false, true);
    }

    lua_getfield(inner, 1, "__ow_path");
    const int old_path_idx = lua_absindex(inner, -1);
    bool moved = false;
    if (auto *old_parent = point->GetParent()) {
      moved = old_parent->ReparentControlPoint(*point, new_parent);
    }
    if (moved) {
      if (lua_istable(inner, old_path_idx)) {
        RemoveControlPointTableFromPath(inner, old_path_idx, point);
      }
      lua_pushvalue(inner, 1);
      AppendToArrayField(inner, new_parent_idx, "__ow_control_points", -1);
      lua_pop(inner, 1);
      lua_pushvalue(inner, new_parent_idx);
      lua_setfield(inner, 1, "__ow_path");
    } else if (old_order != point->GetOrder()) {
      auto *parent = point->GetParent();
      if (parent) {
        parent->OnControlPointOrderChanged(*point);
      }
    }
    lua_pop(inner, 1);
    return 0;
  });
  lua_setfield(L, tbl, "SetParent");

  lua_pushcfunction(L, [](lua_State *inner) -> int {
    auto *point = GetControlPointPtr(inner, 1);
    if (!point || !lua_isnumber(inner, 2) || !lua_isnumber(inner, 3)) {
      return luaL_error(inner, "Usage: %s:SetOffset(x, y)", ControlPointDisplayName(point));
    }
    point->SetOffset(ControlPointPixelToStored(static_cast<float>(lua_tonumber(inner, 2))),
                     ControlPointPixelToStored(static_cast<float>(lua_tonumber(inner, 3))));
    return 0;
  });
  lua_setfield(L, tbl, "SetOffset");

  lua_pushcfunction(L, [](lua_State *inner) -> int {
    auto *point = GetControlPointPtr(inner, 1);
    if (!point) {
      lua_pushnumber(inner, 0.0);
      lua_pushnumber(inner, 0.0);
      return 2;
    }
    lua_pushnumber(inner, ControlPointStoredToPixel(point->GetOffsetX()));
    lua_pushnumber(inner, ControlPointStoredToPixel(point->GetOffsetY()));
    return 2;
  });
  lua_setfield(L, tbl, "GetOffset");

  lua_pushcfunction(L, [](lua_State *inner) -> int {
    auto *point = GetControlPointPtr(inner, 1);
    if (!point || !lua_isnumber(inner, 2)) {
      return luaL_error(inner, "Usage: %s:SetOrder(order)", ControlPointDisplayName(point));
    }
    point->SetOrder(static_cast<int>(lua_tonumber(inner, 2)) - 1, true, false);
    return 0;
  });
  lua_setfield(L, tbl, "SetOrder");

  lua_pushcfunction(L, [](lua_State *inner) -> int {
    auto *point = GetControlPointPtr(inner, 1);
    lua_pushinteger(inner, point ? (point->GetOrder() + 1) : 1);
    return 1;
  });
  lua_setfield(L, tbl, "GetOrder");
}

static void PushSharedControlPointMethodTable(lua_State *L) {
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(L);
  lua_getfield(L, LUA_REGISTRYINDEX, kControlPointMethodTableRegistryKey);
  if (lua_istable(L, -1)) {
    return;
  }
  lua_pop(L, 1);

  lua_newtable(L);
  const int methods_idx = lua_absindex(L, -1);
  InstallControlPointMethods(L, methods_idx);

  lua_pushvalue(L, methods_idx);
  lua_setfield(L, methods_idx, "__index");

  lua_pushvalue(L, methods_idx);
  lua_setfield(L, LUA_REGISTRYINDEX, kControlPointMethodTableRegistryKey);
}

static void PushNewControlPointTable(lua_State *L, int path_idx, PathControlPoint *point) {
  path_idx = lua_absindex(L, path_idx);
  lua_newtable(L);
  const int cp_tbl = lua_absindex(L, -1);
  lua_pushstring(L, "ControlPoint");
  lua_setfield(L, cp_tbl, "__ow_type");
  lua_pushlightuserdata(L, point);
  lua_setfield(L, cp_tbl, "__ow_control_point_ptr");
  lua_pushvalue(L, path_idx);
  lua_setfield(L, cp_tbl, "__ow_path");
  if (point && !point->GetName().empty()) {
    lua_pushstring(L, point->GetName().c_str());
    lua_setfield(L, cp_tbl, "__ow_name");
  }
  PushSharedControlPointMethodTable(L);
  lua_setmetatable(L, cp_tbl);
  if (point != nullptr) {
    lua_pushvalue(L, cp_tbl);
    point->SetLuaObjectRef(L, luaL_ref(L, LUA_REGISTRYINDEX));
  }
}

static void PushOrCreateControlPointTableForPath(lua_State *L, int path_idx,
                                                 PathControlPoint *point) {
  path_idx = lua_absindex(L, path_idx);
  if (PushControlPointTableFromPath(L, path_idx, point)) {
    return;
  }

  PushNewControlPointTable(L, path_idx, point);
  AppendToArrayField(L, path_idx, "__ow_control_points", -1);
}

static bool TryParseLoopType(const char *s, AnimLoopType *out) {
  if (!s || !out) {
    return false;
  }

  int loop_type = 0;
  if (!ParseLoopTypeString(s, &loop_type)) {
    return false;
  }

  *out = static_cast<AnimLoopType>(static_cast<uint8_t>(loop_type));
  return true;
}

static AnimLoopType ParseLoopTypeOrDefault(const char *s) {
  AnimLoopType loop_type = AnimLoopType::None;
  TryParseLoopType(s, &loop_type);
  return loop_type;
}

static const char *LoopTypeToString(AnimLoopType t) {
  switch (t) {
  case AnimLoopType::Repeat:
    return "REPEAT";
  case AnimLoopType::Bounce:
    return "BOUNCE";
  default:
    return "NONE";
  }
}

static const char *LoopStateToString(AnimLoopState s) {
  switch (s) {
  case AnimLoopState::Forward:
    return "FORWARD";
  case AnimLoopState::Reverse:
    return "REVERSE";
  default:
    return "NONE";
  }
}

static void InstallSharedAnimMethods(lua_State *L, int tbl);
static void InstallAlphaMethods(lua_State *L, int tbl);
static void InstallScaleMethods(lua_State *L, int tbl);
static void InstallTranslationMethods(lua_State *L, int tbl);
static void InstallRotationMethods(lua_State *L, int tbl);
static void InstallPathMethods(lua_State *L, int tbl);
static void InstallGroupMethods(lua_State *L, int tbl);
static void PushOrCreateControlPointTableForPath(lua_State *L, int path_idx,
                                                 PathControlPoint *point);

static constexpr const char *kAnimationMethodTableRegistryKey = "openwow.animation_method_tables";
static constexpr const char *kAnimationGroupMethodTableRegistryKey =
    "openwow.animation_group_method_table";
static constexpr const char* kAnimationGroupTemplateUserdataMetatable =
    "openwow.animation_group_template_userdata";
static constexpr const char* kAnimationTemplateUserdataMetatable =
    "openwow.animation_template_userdata";
static constexpr std::string_view kParentToken = "$parent";

static bool HasParentTokenPrefix(const char* name) {
  if (name == nullptr) {
    return false;
  }

  for (std::size_t index = 0; index < kParentToken.size(); ++index) {
    const unsigned char lhs = static_cast<unsigned char>(name[index]);
    const unsigned char rhs = static_cast<unsigned char>(kParentToken[index]);
    if (lhs == '\0' || std::tolower(lhs) != std::tolower(rhs)) {
      return false;
    }
  }

  return true;
}

static std::string FindNearestNamedAncestor(lua_State* L, int parent_idx) {
  parent_idx = lua_absindex(L, parent_idx);

  lua_pushvalue(L, parent_idx);
  for (int depth = 0; depth < 64 && lua_istable(L, -1) != 0; ++depth) {
    lua_getfield(L, -1, "__ow_name");
    const char* parent_name = lua_tostring(L, -1);
    if (parent_name != nullptr && parent_name[0] != '\0') {
      const std::string resolved_name(parent_name);
      lua_pop(L, 2);
      return resolved_name;
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "__ow_parent");
    lua_remove(L, -2);
  }

  lua_pop(L, 1);
  return {};
}

static std::string ResolveAnimationObjectName(lua_State* L, int parent_idx, const char* raw_name) {
  if (raw_name == nullptr) {
    return {};
  }

  if (!HasParentTokenPrefix(raw_name)) {
    return raw_name;
  }

  std::string resolved_name = FindNearestNamedAncestor(L, parent_idx);
  resolved_name.append(raw_name + kParentToken.size());
  return resolved_name;
}

static void AttachAnimationObjectHandle(lua_State* L, int table_idx, void* object_pointer) {
  if (L == nullptr || object_pointer == nullptr) {
    return;
  }

  table_idx = lua_absindex(L, table_idx);
  lua_pushlightuserdata(L, object_pointer);
  lua_rawseti(L, table_idx, 0);
}

static void SetAnimationObjectNameField(lua_State* L, int table_idx, const std::string& name) {
  table_idx = lua_absindex(L, table_idx);
  lua_pushlstring(L, name.data(), name.size());
  lua_setfield(L, table_idx, "__ow_name");
}

static void BindAnimationObjectGlobal(lua_State* L, int table_idx, const std::string& name) {
  if (L == nullptr) {
    return;
  }

  table_idx = lua_absindex(L, table_idx);
  SetAnimationObjectNameField(L, table_idx, name);

  lua_getglobal(L, name.c_str());
  const bool global_is_nil = lua_isnil(L, -1) != 0;
  lua_pop(L, 1);
  if (!global_is_nil) {
    return;
  }

  openwow::ui::ReplaceLuaGlobalValue(L, name.c_str(), table_idx);
}

static void RegisterNamedGroupOnRegion(lua_State *L, int region_idx, int group_idx,
                                      const char *name) {
  if (name == nullptr || name[0] == '\0') {
    return;
  }

  region_idx = lua_absindex(L, region_idx);
  group_idx = lua_absindex(L, group_idx);

  lua_getfield(L, region_idx, "__ow_named_anim_groups");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, region_idx, "__ow_named_anim_groups");
  }
  lua_pushvalue(L, group_idx);
  lua_setfield(L, -2, name);
  lua_pop(L, 1);
}

static void RegisterAnimationGroupParentKeyOnRegion(lua_State *L, int region_idx, int group_idx,
                                                   const char *parent_key) {
  if (parent_key == nullptr || parent_key[0] == '\0') {
    return;
  }

  region_idx = lua_absindex(L, region_idx);
  group_idx = lua_absindex(L, group_idx);
  if (!lua_istable(L, region_idx) || !lua_istable(L, group_idx)) {
    return;
  }

  lua_pushvalue(L, group_idx);
  lua_setfield(L, region_idx, parent_key);
}

int CreateAnimationGroupOnRegion(lua_State *L, int region_idx, const char *name) {
  region_idx = lua_absindex(L, region_idx);
  if (!lua_istable(L, region_idx)) {
    lua_newtable(L);
    return lua_absindex(L, -1);
  }

  PushAnimGroupTable(L, region_idx);
  const int group_idx = lua_absindex(L, -1);
  const bool has_requested_name = name != nullptr && name[0] != '\0';
  const std::string resolved_name =
      has_requested_name ? ResolveAnimationObjectName(L, region_idx, name) : std::string();

  if (auto *group = TryGetGroupPtr(L, group_idx)) {
    if (has_requested_name) {
      group->SetName(resolved_name);
    }
    group->SetParentFrame(const_cast<void *>(lua_topointer(L, region_idx)));

    auto *script_obj = static_cast<openwow::ui::widgets::CScriptObject *>(
        openwow::ui::game::detail::GetLuaNativeScriptObjectThisPointer(L, region_idx));
    if (auto *region = dynamic_cast<openwow::ui::widgets::CScriptRegion *>(script_obj)) {
      group->SetOwnerRegion(region);
    }
  }

  PrependToArrayField(L, region_idx, "__ow_anim_groups", group_idx);
  if (has_requested_name) {
    RegisterNamedGroupOnRegion(L, region_idx, group_idx, resolved_name.c_str());
    BindAnimationObjectGlobal(L, group_idx, resolved_name);
  }
  return group_idx;
}

static void RegisterNamedAnimationOnGroup(lua_State *L, int group_idx, int anim_idx,
                                          const char *name) {
  if (name == nullptr || name[0] == '\0') {
    return;
  }

  group_idx = lua_absindex(L, group_idx);
  anim_idx = lua_absindex(L, anim_idx);

  lua_getfield(L, group_idx, "__ow_named_anims");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, group_idx, "__ow_named_anims");
  }
  lua_pushvalue(L, anim_idx);
  lua_setfield(L, -2, name);
  lua_pop(L, 1);
}

static void UnregisterNamedAnimationOnGroup(lua_State *L, int group_idx, int anim_idx,
                                            const char *name) {
  if (name == nullptr || name[0] == '\0') {
    return;
  }

  group_idx = lua_absindex(L, group_idx);
  anim_idx = lua_absindex(L, anim_idx);

  lua_getfield(L, group_idx, "__ow_named_anims");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }

  lua_getfield(L, -1, name);
  const bool matches = lua_rawequal(L, -1, anim_idx) != 0;
  lua_pop(L, 1);
  if (matches) {
    lua_pushnil(L);
    lua_setfield(L, -2, name);
  }
  lua_pop(L, 1);
}

static const char *RuntimeAnimTypeForSpec(const openwow::ui::framexml::UiAnimation &spec) {
  if (spec.type.empty() || spec.type == "Animation") {
    return "Animation";
  }
  return spec.type.c_str();
}

static bool IcaseEqual(const char *lhs, const char *rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }

  while (*lhs != '\0' && *rhs != '\0') {
    if (std::tolower(static_cast<unsigned char>(*lhs)) !=
        std::tolower(static_cast<unsigned char>(*rhs))) {
      return false;
    }
    ++lhs;
    ++rhs;
  }

  return *lhs == *rhs;
}

static const char *CanonicalAnimationTypeName(const char *anim_type) {
  if (anim_type == nullptr || *anim_type == '\0' || IcaseEqual(anim_type, "Animation")) {
    return "Animation";
  }
  if (IcaseEqual(anim_type, "Alpha")) {
    return "Alpha";
  }
  if (IcaseEqual(anim_type, "Scale")) {
    return "Scale";
  }
  if (IcaseEqual(anim_type, "Translation")) {
    return "Translation";
  }
  if (IcaseEqual(anim_type, "Rotation")) {
    return "Rotation";
  }
  if (IcaseEqual(anim_type, "Path")) {
    return "Path";
  }
  return "Animation";
}

static void RegisterAnimationParentKeyOnGroup(lua_State* L,
                                              int group_idx,
                                              int anim_idx,
                                              const char* parent_key) {
  if (L == nullptr || parent_key == nullptr || parent_key[0] == '\0') {
    return;
  }

  group_idx = lua_absindex(L, group_idx);
  anim_idx = lua_absindex(L, anim_idx);
  if (!lua_istable(L, group_idx) || !lua_istable(L, anim_idx)) {
    return;
  }

  lua_pushvalue(L, anim_idx);
  lua_setfield(L, group_idx, parent_key);
}

static void EnsureAnimationTemplateUserdataMetatable(lua_State* L) {
  if (luaL_newmetatable(L, kAnimationTemplateUserdataMetatable) != 0) {
    lua_pushcfunction(L, [](lua_State* state) -> int {
      auto* spec = static_cast<openwow::ui::framexml::UiAnimation*>(lua_touserdata(state, 1));
      if (spec != nullptr) {
        spec->~UiAnimation();
      }
      return 0;
    });
    lua_setfield(L, -2, "__gc");
  }
  lua_pop(L, 1);
}

static void EnsureAnimationGroupTemplateUserdataMetatable(lua_State* L) {
  if (luaL_newmetatable(L, kAnimationGroupTemplateUserdataMetatable) != 0) {
    lua_pushcfunction(L, [](lua_State* state) -> int {
      auto* spec = static_cast<openwow::ui::framexml::UiAnimationGroup*>(
          lua_touserdata(state, 1));
      if (spec != nullptr) {
        spec->~UiAnimationGroup();
      }
      return 0;
    });
    lua_setfield(L, -2, "__gc");
  }
  lua_pop(L, 1);
}

static void PushAnimationGroupTemplateUserdata(
    lua_State* L,
    const openwow::ui::framexml::UiAnimationGroup& spec) {
  EnsureAnimationGroupTemplateUserdataMetatable(L);
  void* storage = lua_newuserdatauv(L, sizeof(openwow::ui::framexml::UiAnimationGroup), 0);
  auto* copy = new (storage) openwow::ui::framexml::UiAnimationGroup(spec);
  (void)copy;
  luaL_getmetatable(L, kAnimationGroupTemplateUserdataMetatable);
  lua_setmetatable(L, -2);
}

static const openwow::ui::framexml::UiAnimationGroup* TryGetAnimationGroupTemplateSpec(
    lua_State* L,
    int idx) {
  return static_cast<const openwow::ui::framexml::UiAnimationGroup*>(
      luaL_testudata(L, idx, kAnimationGroupTemplateUserdataMetatable));
}

static void PushAnimationTemplateUserdata(lua_State* L,
                                          const openwow::ui::framexml::UiAnimation& spec) {
  EnsureAnimationTemplateUserdataMetatable(L);
  void* storage = lua_newuserdatauv(L, sizeof(openwow::ui::framexml::UiAnimation), 0);
  auto* copy = new (storage) openwow::ui::framexml::UiAnimation(spec);
  (void)copy;
  luaL_getmetatable(L, kAnimationTemplateUserdataMetatable);
  lua_setmetatable(L, -2);
}

static const openwow::ui::framexml::UiAnimation* TryGetAnimationTemplateSpec(lua_State* L,
                                                                              int idx) {
  return static_cast<const openwow::ui::framexml::UiAnimation*>(
      luaL_testudata(L, idx, kAnimationTemplateUserdataMetatable));
}

static void PushControlPointTemplateSpec(lua_State* L,
                                         const openwow::ui::framexml::UiPathControlPoint& spec) {
  lua_newtable(L);
  const int table_idx = lua_absindex(L, -1);

  if (!spec.name.empty()) {
    lua_pushstring(L, spec.name.c_str());
    lua_setfield(L, table_idx, "name");
  }
  if (!spec.inherits.empty()) {
    lua_pushstring(L, spec.inherits.c_str());
    lua_setfield(L, table_idx, "inherits");
  }
  if (!spec.parent_key.empty()) {
    lua_pushstring(L, spec.parent_key.c_str());
    lua_setfield(L, table_idx, "parentKey");
  }
  if (spec.offset_x.has_value()) {
    lua_pushnumber(L, *spec.offset_x);
    lua_setfield(L, table_idx, "offsetX");
  }
  if (spec.offset_y.has_value()) {
    lua_pushnumber(L, *spec.offset_y);
    lua_setfield(L, table_idx, "offsetY");
  }
}

static void RegisterFrameAnimationTemplates(
    lua_State* L,
    int frame_idx,
    const std::vector<openwow::ui::framexml::UiAnimationGroup>& groups) {
  frame_idx = lua_absindex(L, frame_idx);
  if (!lua_istable(L, frame_idx)) {
    return;
  }

  lua_getfield(L, frame_idx, "__ow_animation_templates");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, frame_idx, "__ow_animation_templates");
  }

  const int template_map_idx = lua_absindex(L, -1);
  for (const auto& group : groups) {
    for (const auto& animation : group.animations) {
      if (animation.name.empty()) {
        continue;
      }

      PushAnimationTemplateUserdata(L, animation);
      lua_setfield(L, template_map_idx, animation.name.c_str());
    }
  }

  lua_pop(L, 1);
}

static void RegisterFrameAnimationGroupTemplates(
    lua_State* L,
    int frame_idx,
    const std::vector<openwow::ui::framexml::UiAnimationGroup>& groups) {
  frame_idx = lua_absindex(L, frame_idx);
  if (!lua_istable(L, frame_idx)) {
    return;
  }

  lua_getfield(L, frame_idx, "__ow_animation_group_templates");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, frame_idx, "__ow_animation_group_templates");
  }

  const int template_map_idx = lua_absindex(L, -1);
  for (const auto& group : groups) {
    if (group.name.empty()) {
      continue;
    }

    PushAnimationGroupTemplateUserdata(L, group);
    lua_setfield(L, template_map_idx, group.name.c_str());
  }

  lua_pop(L, 1);
}

static void RegisterFrameControlPointTemplates(
    lua_State* L,
    int frame_idx,
    const std::vector<openwow::ui::framexml::UiAnimationGroup>& groups) {
  frame_idx = lua_absindex(L, frame_idx);
  if (!lua_istable(L, frame_idx)) {
    return;
  }

  lua_getfield(L, frame_idx, "__ow_control_point_templates");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, frame_idx, "__ow_control_point_templates");
  }

  const int template_map_idx = lua_absindex(L, -1);
  for (const auto& group : groups) {
    for (const auto& animation : group.animations) {
      for (const auto& control_point : animation.control_points) {
        if (control_point.name.empty()) {
          continue;
        }

        PushControlPointTemplateSpec(L, control_point);
        lua_setfield(L, template_map_idx, control_point.name.c_str());
      }
    }
  }

  lua_pop(L, 1);
}

static bool PushOwningFrameForGroup(lua_State* L, int group_idx) {
  group_idx = lua_absindex(L, group_idx);
  lua_getfield(L, group_idx, "__ow_parent");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }

  return true;
}

static bool PushOwningFrameForPath(lua_State* L, int path_idx) {
  path_idx = lua_absindex(L, path_idx);
  lua_getfield(L, path_idx, "__ow_group");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }

  lua_getfield(L, -1, "__ow_parent");
  lua_remove(L, -2);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }

  return true;
}

static bool PushFrameAnimationTemplate(lua_State* L, int frame_idx, const char* name) {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }

  frame_idx = lua_absindex(L, frame_idx);
  lua_getfield(L, frame_idx, "__ow_animation_templates");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }

  lua_getfield(L, -1, name);
  lua_remove(L, -2);
  if (TryGetAnimationTemplateSpec(L, -1) == nullptr) {
    lua_pop(L, 1);
    return false;
  }

  return true;
}

static bool PushFrameAnimationGroupTemplate(lua_State* L, int frame_idx, const char* name) {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }

  frame_idx = lua_absindex(L, frame_idx);
  lua_getfield(L, frame_idx, "__ow_animation_group_templates");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }

  lua_getfield(L, -1, name);
  lua_remove(L, -2);
  if (TryGetAnimationGroupTemplateSpec(L, -1) == nullptr) {
    lua_pop(L, 1);
    return false;
  }

  return true;
}

static bool PushFrameControlPointTemplate(lua_State* L, int frame_idx, const char* name) {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }

  frame_idx = lua_absindex(L, frame_idx);
  lua_getfield(L, frame_idx, "__ow_control_point_templates");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }

  lua_getfield(L, -1, name);
  lua_remove(L, -2);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }

  return true;
}

static const openwow::ui::framexml::UiAnimation* ResolveAnimationTemplateForGroup(
    lua_State* L,
    int group_idx,
    const std::string& name) {
  if (name.empty()) {
    return nullptr;
  }

  const int original_top = lua_gettop(L);
  if (PushOwningFrameForGroup(L, group_idx)) {
    const int frame_idx = lua_absindex(L, -1);
    if (PushFrameAnimationTemplate(L, frame_idx, name.c_str())) {
      const auto* spec = TryGetAnimationTemplateSpec(L, -1);
      lua_settop(L, original_top);
      return spec;
    }
  }
  lua_settop(L, original_top);

  return openwow::ui::framexml::GetVirtualAnimationTemplate(name);
}

static const openwow::ui::framexml::UiAnimationGroup* ResolveAnimationGroupTemplateForFrame(
    lua_State* L,
    int frame_idx,
    const std::string& name) {
  if (name.empty()) {
    return nullptr;
  }

  const int original_top = lua_gettop(L);
  frame_idx = lua_absindex(L, frame_idx);
  if (lua_istable(L, frame_idx) && PushFrameAnimationGroupTemplate(L, frame_idx, name.c_str())) {
    const auto* spec = TryGetAnimationGroupTemplateSpec(L, -1);
    lua_settop(L, original_top);
    return spec;
  }
  lua_settop(L, original_top);

  return openwow::ui::framexml::GetVirtualAnimationGroupTemplate(name);
}

static bool PushNamedControlPointTemplateForPath(lua_State* L, int path_idx, const char* name) {
  const int original_top = lua_gettop(L);
  if (PushOwningFrameForPath(L, path_idx)) {
    const int frame_idx = lua_absindex(L, -1);
    if (PushFrameControlPointTemplate(L, frame_idx, name)) {
      lua_remove(L, frame_idx);
      return true;
    }
  }
  lua_settop(L, original_top);

  const auto* virtual_template = openwow::ui::framexml::GetVirtualControlPointTemplate(
      name != nullptr ? std::string(name) : std::string());
  if (virtual_template == nullptr) {
    return false;
  }

  PushControlPointTemplateSpec(L, *virtual_template);
  return true;
}

static std::string GetStringField(lua_State* L, int table_idx, const char* field) {
  table_idx = lua_absindex(L, table_idx);
  lua_getfield(L, table_idx, field);
  std::string value;
  if (const char* string_value = lua_tostring(L, -1)) {
    value = string_value;
  }
  lua_pop(L, 1);
  return value;
}

static std::optional<float> GetNumberField(lua_State* L, int table_idx, const char* field) {
  table_idx = lua_absindex(L, table_idx);
  lua_getfield(L, table_idx, field);
  std::optional<float> value;
  if (lua_isnumber(L, -1)) {
    value = static_cast<float>(lua_tonumber(L, -1));
  }
  lua_pop(L, 1);
  return value;
}

static int EnsureControlPointTableForPath(lua_State* L,
                                          int path_idx,
                                          PathControlPoint* point,
                                          int* point_table_idx) {
  if (point_table_idx != nullptr && *point_table_idx > 0) {
    return *point_table_idx;
  }

  PushOrCreateControlPointTableForPath(L, path_idx, point);
  const int created_idx = lua_absindex(L, -1);
  if (point_table_idx != nullptr) {
    *point_table_idx = created_idx;
  }
  return created_idx;
}

static void ApplyControlPointTemplateTable(lua_State* L,
                                           int path_idx,
                                           PathControlPoint* point,
                                           int* point_table_idx,
                                           int template_idx,
                                           std::unordered_set<std::string>* recursion_stack) {
  if (L == nullptr || point == nullptr) {
    return;
  }

  path_idx = lua_absindex(L, path_idx);
  template_idx = lua_absindex(L, template_idx);

  std::unordered_set<std::string> local_stack;
  auto& stack = recursion_stack != nullptr ? *recursion_stack : local_stack;

  const std::string template_name = GetStringField(L, template_idx, "name");
  const bool inserted_name =
      !template_name.empty() && stack.insert(template_name).second;

  const std::string inherited_name = GetStringField(L, template_idx, "inherits");
  if (!inherited_name.empty() && stack.find(inherited_name) == stack.end()
      && PushNamedControlPointTemplateForPath(L, path_idx, inherited_name.c_str())) {
    const int inherited_idx = lua_absindex(L, -1);
    ApplyControlPointTemplateTable(
        L, path_idx, point, point_table_idx, inherited_idx, &stack);
    lua_pop(L, 1);
  }

  const std::string parent_key = GetStringField(L, template_idx, "parentKey");
  if (!parent_key.empty()) {
    const int point_idx = EnsureControlPointTableForPath(L, path_idx, point, point_table_idx);
    lua_pushvalue(L, point_idx);
    lua_setfield(L, path_idx, parent_key.c_str());
  }

  const float offset_x = GetNumberField(L, template_idx, "offsetX").value_or(0.0f);
  const float offset_y = GetNumberField(L, template_idx, "offsetY").value_or(0.0f);
  point->SetOffset(ControlPointPixelToStored(offset_x), ControlPointPixelToStored(offset_y));

  if (inserted_name) {
    stack.erase(template_name);
  }
}

static const openwow::ui::framexml::UiAnimation* FindLocalAnimationTemplate(
    const std::vector<openwow::ui::framexml::UiAnimationGroup>& groups,
    const std::string& name) {
  if (name.empty()) {
    return nullptr;
  }

  for (const auto& group : groups) {
    for (const auto& animation : group.animations) {
      if (animation.name == name) {
        return &animation;
      }
    }
  }

  return nullptr;
}

static const openwow::ui::framexml::UiAnimationGroup* FindLocalAnimationGroupTemplate(
    const std::vector<openwow::ui::framexml::UiAnimationGroup>& groups,
    const std::string& name) {
  if (name.empty()) {
    return nullptr;
  }

  for (const auto& group : groups) {
    if (group.name == name) {
      return &group;
    }
  }

  return nullptr;
}

static const openwow::ui::framexml::UiAnimation* ResolveAnimationTemplate(
    const std::vector<openwow::ui::framexml::UiAnimationGroup>& groups,
    const std::string& name) {
  if (const auto* local = FindLocalAnimationTemplate(groups, name)) {
    return local;
  }

  return openwow::ui::framexml::GetVirtualAnimationTemplate(name);
}

static const openwow::ui::framexml::UiAnimationGroup* ResolveAnimationGroupTemplate(
    const std::vector<openwow::ui::framexml::UiAnimationGroup>& groups,
    const std::string& name) {
  if (const auto* local = FindLocalAnimationGroupTemplate(groups, name)) {
    return local;
  }

  return openwow::ui::framexml::GetVirtualAnimationGroupTemplate(name);
}

enum class AnimationTemplateResolution {
  Found,
  Missing,
  Recursive,
};

enum class AnimationGroupTemplateResolution {
  Found,
  Missing,
  Recursive,
};

template <typename ResolveTemplateFn>
AnimationTemplateResolution ValidateAnimationTemplateChain(
    ResolveTemplateFn&& resolve_template,
    const std::string& name,
    std::unordered_set<std::string>* recursion_stack = nullptr) {
  if (name.empty()) {
    return AnimationTemplateResolution::Missing;
  }

  std::unordered_set<std::string> local_stack;
  auto& stack = recursion_stack != nullptr ? *recursion_stack : local_stack;
  if (!stack.insert(name).second) {
    return AnimationTemplateResolution::Recursive;
  }

  const auto* spec = resolve_template(name);
  if (spec == nullptr) {
    stack.erase(name);
    return AnimationTemplateResolution::Missing;
  }

  AnimationTemplateResolution result = AnimationTemplateResolution::Found;
  if (!spec->inherits.empty()) {
    result =
        ValidateAnimationTemplateChain(resolve_template, spec->inherits, &stack);
  }

  stack.erase(name);
  return result;
}

template <typename ResolveTemplateFn>
AnimationGroupTemplateResolution ValidateAnimationGroupTemplateChain(
    ResolveTemplateFn&& resolve_template,
    const std::string& name,
    std::unordered_set<std::string>* recursion_stack = nullptr) {
  if (name.empty()) {
    return AnimationGroupTemplateResolution::Missing;
  }

  std::unordered_set<std::string> local_stack;
  auto& stack = recursion_stack != nullptr ? *recursion_stack : local_stack;
  if (!stack.insert(name).second) {
    return AnimationGroupTemplateResolution::Recursive;
  }

  const auto* spec = resolve_template(name);
  if (spec == nullptr) {
    stack.erase(name);
    return AnimationGroupTemplateResolution::Missing;
  }

  AnimationGroupTemplateResolution result = AnimationGroupTemplateResolution::Found;
  if (!spec->inherits.empty()) {
    result = ValidateAnimationGroupTemplateChain(resolve_template, spec->inherits, &stack);
  }

  stack.erase(name);
  return result;
}

static void ApplyAnimationTypeSpecificSpec(Animation* anim,
                                           const openwow::ui::framexml::UiAnimation& spec) {
  if (anim == nullptr) {
    return;
  }

  switch (anim->GetKind()) {
  case AnimKind::Animation:

    break;
  case AnimKind::Alpha: {
    auto* alpha = static_cast<AlphaAnim*>(anim);
    if (spec.from_alpha.has_value()) {
      alpha->SetFromAlpha(*spec.from_alpha);
    }
    if (spec.to_alpha.has_value()) {
      alpha->SetToAlpha(*spec.to_alpha);
    }
    if (spec.change.has_value()) {
      alpha->SetChange(*spec.change);
    }
    break;
  }
  case AnimKind::Scale: {
    auto* scale = static_cast<ScaleAnim*>(anim);
    if (spec.from_scale_x.has_value() || spec.from_scale_y.has_value()) {
      float from_x = 1.0f;
      float from_y = 1.0f;
      scale->GetFromScale(from_x, from_y);
      scale->SetFromScale(spec.from_scale_x.value_or(from_x), spec.from_scale_y.value_or(from_y));
    }
    if (spec.to_scale_x.has_value() || spec.to_scale_y.has_value()) {
      float to_x = 1.0f;
      float to_y = 1.0f;
      scale->GetToScale(to_x, to_y);
      scale->SetToScale(spec.to_scale_x.value_or(to_x), spec.to_scale_y.value_or(to_y));
    } else {
      scale->SetScaleDelta(spec.stock_scale_x.value_or(0.0f),
                           spec.stock_scale_y.value_or(0.0f));
    }
    if (!spec.origin_point.empty()) {
      scale->SetOriginPixels(
          spec.origin_point, spec.origin_x.value_or(0.0f), spec.origin_y.value_or(0.0f));
    }
    break;
  }
  case AnimKind::Translation: {
    auto* translation = static_cast<TranslationAnim*>(anim);
    if (spec.offset_x.has_value() || spec.offset_y.has_value()) {
      translation->SetOffset(spec.offset_x.value_or(0.0f), spec.offset_y.value_or(0.0f));
    }
    break;
  }
  case AnimKind::Rotation: {
    auto* rotation = static_cast<RotationAnim*>(anim);
    if (spec.radians.has_value()) {
      rotation->SetRadians(*spec.radians);
    } else if (spec.degrees.has_value()) {
      rotation->SetDegrees(*spec.degrees);
    }
    if (!spec.origin_point.empty()) {
      rotation->SetOriginPixels(
          spec.origin_point, spec.origin_x.value_or(0.0f), spec.origin_y.value_or(0.0f));
    }
    break;
  }
  case AnimKind::Path:
    if (spec.curve_type.has_value()) {
      int parsed_curve = 0;
      if (openwow::ui::ParseCurveTypeString(spec.curve_type->c_str(), &parsed_curve)) {
        static_cast<PathAnim*>(anim)->SetCurve(static_cast<uint8_t>(parsed_curve));
      }
    }
    break;
  }
}

static void MaterializePathControlPointsFromSpec(
    lua_State* L,
    int path_idx,
    const openwow::ui::framexml::UiAnimation& spec) {
  auto* path = TryGetPathPtr(L, path_idx);
  if (path == nullptr || spec.control_points.empty()) {
    return;
  }

  for (const auto& control_point_spec : spec.control_points) {
    auto* point = path->CreateControlPoint(control_point_spec.name);
    if (point == nullptr) {
      continue;
    }

    PushControlPointTemplateSpec(L, control_point_spec);
    const int spec_idx = lua_absindex(L, -1);
    int point_table_idx = 0;
    ApplyControlPointTemplateTable(L, path_idx, point, &point_table_idx, spec_idx, nullptr);
    if (point_table_idx > 0) {
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
}

template <typename ResolveTemplateFn>
static void ConfigureAnimationFromSpec(
    lua_State* L,
    int group_idx,
    int anim_idx,
    Animation* anim,
    const openwow::ui::framexml::UiAnimation& spec,
    ResolveTemplateFn&& resolve_template,
    openwow::ui::framexml::XmlScriptCache* script_cache,
    std::unordered_set<std::string>* recursion_stack = nullptr,
    bool mark_local_state = true,
    bool apply_name_from_spec = true) {
  if (anim == nullptr) {
    return;
  }

  std::unordered_set<std::string> local_stack;
  auto& stack = recursion_stack != nullptr ? *recursion_stack : local_stack;

  if (!spec.inherits.empty()) {
    if (stack.insert(spec.inherits).second) {
      if (const auto* inherited = resolve_template(spec.inherits)) {
        ConfigureAnimationFromSpec(L,
                                   group_idx,
                                   anim_idx,
                                   anim,
                                   *inherited,
                                   resolve_template,
                                   script_cache,
                                   &stack,
                                   false,
                                   false);
      }
      stack.erase(spec.inherits);
    }
  }

  if (mark_local_state) {
    anim->MarkLoadedFromXml();
    if (apply_name_from_spec && !spec.name.empty()) {
      anim->SetName(spec.name);
    }
  }

  ApplyXmlAnimationBaseAttributes(anim,
                                  spec.duration,
                                  spec.start_delay,
                                  spec.end_delay,
                                  spec.order,
                                  spec.smoothing.empty() ? nullptr : spec.smoothing.c_str(),
                                  spec.max_framerate);
  ApplyAnimationTypeSpecificSpec(anim, spec);
  if (anim->GetKind() == AnimKind::Path) {
    MaterializePathControlPointsFromSpec(L, anim_idx, spec);
  }
  RegisterAnimationParentKeyOnGroup(
      L, group_idx, anim_idx, spec.parent_key.empty() ? nullptr : spec.parent_key.c_str());
  AttachXmlScriptHandlers(L,
                          anim,
                          spec.script_handlers,
                          openwow::ui::framexml::XmlScriptOwner::Animation,
                          script_cache);
  if (mark_local_state) {
    FinalizeLoadedAnimation(anim);
  }
}

int PushRegionAnimationGroups(lua_State *L, int region_idx) {
  region_idx = lua_absindex(L, region_idx);
  if (!lua_istable(L, region_idx)) {
    return 0;
  }

  lua_getfield(L, region_idx, "__ow_anim_groups");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }

  const int array_idx = lua_absindex(L, -1);
  const lua_Integer len = luaL_len(L, array_idx);
  for (lua_Integer i = 1; i <= len; ++i) {
    lua_geti(L, array_idx, i);
  }
  lua_remove(L, array_idx);
  return static_cast<int>(len);
}

void StopRegionAnimationGroups(lua_State *L, int region_idx) {
  region_idx = lua_absindex(L, region_idx);
  if (!lua_istable(L, region_idx)) {
    return;
  }

  lua_getfield(L, region_idx, "__ow_anim_groups");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }

  const int array_idx = lua_absindex(L, -1);
  const lua_Integer len = luaL_len(L, array_idx);
  for (lua_Integer i = 1; i <= len; ++i) {
    lua_geti(L, array_idx, i);
    if (auto *group = TryGetGroupPtr(L, -1)) {
      group->Stop(true);
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
}

void UpdateRegionAnimationGroups(lua_State* L, int region_idx, const float elapsed_seconds) {
  region_idx = lua_absindex(L, region_idx);
  if (!lua_istable(L, region_idx)) {
    return;
  }

  lua_getfield(L, region_idx, "__ow_anim_groups");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }

  const int array_idx = lua_absindex(L, -1);
  const lua_Integer len = luaL_len(L, array_idx);
  std::vector<int> group_refs;
  group_refs.reserve(static_cast<std::size_t>(std::max<lua_Integer>(len, 0)));
  for (lua_Integer i = 1; i <= len; ++i) {
    lua_geti(L, array_idx, i);
    group_refs.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
  }
  lua_pop(L, 1);

  for (const int ref : group_refs) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (auto* group = TryGetGroupPtr(L, -1); group != nullptr) {
      group->Update(std::max(0.0f, elapsed_seconds));
    }
    lua_pop(L, 1);
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
  }
}

RegionAnimationState GetRegionAnimationState(lua_State* L, int region_idx) {
  region_idx = lua_absindex(L, region_idx);
  if (!lua_istable(L, region_idx)) {
    return {};
  }

  RegionAnimationState state;
  lua_getfield(L, region_idx, "__ow_anim_groups");
  if (lua_istable(L, -1)) {
    const int array_idx = lua_absindex(L, -1);
    const lua_Integer len = luaL_len(L, array_idx);
    for (lua_Integer i = 1; i <= len; ++i) {
      lua_geti(L, array_idx, i);
      if (auto* group = TryGetGroupPtr(L, -1); group != nullptr) {
        for (const auto& animation : group->GetAnimations()) {
          if (const auto* translation = dynamic_cast<const TranslationAnim*>(animation.get());
              translation != nullptr) {
            state.translation_x += translation->GetCurrentX();
            state.translation_y += translation->GetCurrentY();
          } else if (const auto* path = dynamic_cast<const PathAnim*>(animation.get());
                     path != nullptr) {
            state.translation_x += path->GetCurrentX();
            state.translation_y += path->GetCurrentY();
          } else if (const auto* scale = dynamic_cast<const ScaleAnim*>(animation.get());
                     scale != nullptr) {
            state.scale_x *= scale->GetCurrentScaleX();
            state.scale_y *= scale->GetCurrentScaleY();
          } else if (const auto* rotation = dynamic_cast<const RotationAnim*>(animation.get());
                     rotation != nullptr) {
            state.rotation_radians += rotation->GetCurrentRadians();
          } else if (const auto* alpha = dynamic_cast<const AlphaAnim*>(animation.get());
                     alpha != nullptr) {
            if (alpha->HasExplicitChange()) {
              state.alpha_change += alpha->GetCurrentChange();
            } else {
              state.alpha = alpha->GetCurrentAlpha();
            }
          }
        }
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  return state;
}

std::pair<float, float> GetRegionAnimationTranslation(lua_State* L, int region_idx) {
  const RegionAnimationState state = GetRegionAnimationState(L, region_idx);
  return {state.translation_x, state.translation_y};
}

void ApplyAnimationRegionMethods(lua_State *L) {
  const int region = lua_absindex(L, -1);

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self = ValidateAnimationRegionSelf(Ls, 1);
    const char *name = luaL_optstring(Ls, 2, "");
    const char* inherits_from =
        lua_type(Ls, 3) == LUA_TSTRING ? lua_tostring(Ls, 3) : nullptr;
    CreateAnimationGroupTableOnRegion(Ls, self, name, inherits_from);
    return 1;
  });
  lua_setfield(L, region, "CreateAnimationGroup");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self = ValidateAnimationRegionSelf(Ls, 1);
    return PushRegionAnimationGroups(Ls, self);
  });
  lua_setfield(L, region, "GetAnimationGroups");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self = ValidateAnimationRegionSelf(Ls, 1);
    StopRegionAnimationGroups(Ls, self);
    return 0;
  });
  lua_setfield(L, region, "StopAnimating");
}

static void PushSharedAnimationMethodTable(lua_State *L, const char *anim_type) {

  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(L);
  lua_getfield(L, LUA_REGISTRYINDEX, kAnimationMethodTableRegistryKey);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, kAnimationMethodTableRegistryKey);
  }

  const int cache_idx = lua_absindex(L, -1);
  lua_getfield(L, cache_idx, anim_type);
  if (lua_istable(L, -1)) {
    lua_remove(L, cache_idx);
    return;
  }
  lua_pop(L, 1);

  lua_newtable(L);
  const int methods_idx = lua_absindex(L, -1);

  InstallSharedAnimMethods(L, methods_idx);

  if (std::strcmp(anim_type, "Alpha") == 0) {
    InstallAlphaMethods(L, methods_idx);
  } else if (std::strcmp(anim_type, "Scale") == 0) {
    InstallScaleMethods(L, methods_idx);
  } else if (std::strcmp(anim_type, "Translation") == 0) {
    InstallTranslationMethods(L, methods_idx);
  } else if (std::strcmp(anim_type, "Rotation") == 0) {
    InstallRotationMethods(L, methods_idx);
  } else if (std::strcmp(anim_type, "Path") == 0) {
    InstallPathMethods(L, methods_idx);
  }

  lua_pushvalue(L, methods_idx);
  lua_setfield(L, methods_idx, "__index");

  lua_pushvalue(L, methods_idx);
  lua_setfield(L, cache_idx, anim_type);
  lua_remove(L, cache_idx);
}

static void UnregisterSharedAnimationMethodTable(lua_State* L, const char* anim_type) {
  if (L == nullptr || anim_type == nullptr || *anim_type == '\0') {
    return;
  }

  lua_getfield(L, LUA_REGISTRYINDEX, kAnimationMethodTableRegistryKey);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }

  lua_pushnil(L);
  lua_setfield(L, -2, anim_type);
  lua_pop(L, 1);
}

static void ClearRegistryCachedMethodTable(lua_State* L, const char* registry_key) {
  if (L == nullptr || registry_key == nullptr || *registry_key == '\0') {
    return;
  }

  lua_pushnil(L);
  lua_setfield(L, LUA_REGISTRYINDEX, registry_key);
}

static void AssignDefaultLuaAnimationOrder(AnimationGroup *group, Animation *anim) {
  if (group == nullptr || anim == nullptr || anim->GetOrder() >= 0) {
    return;
  }

  anim->SetOrder(std::max(group->GetMaxOrder(), 0), true, true);
}

static int PushAnimationTableForGroup(lua_State *L, int group_idx, Animation *anim) {
  group_idx = lua_absindex(L, group_idx);
  if (anim == nullptr) {
    lua_pushnil(L);
    return lua_absindex(L, -1);
  }

  const char *runtime_anim_type = anim->GetObjectTypeName();

  lua_newtable(L);
  const int tbl = lua_absindex(L, -1);

  lua_pushstring(L, "Animation");
  lua_setfield(L, tbl, "__ow_type");

  lua_pushstring(L, runtime_anim_type);
  lua_setfield(L, tbl, "__ow_anim_kind");

  lua_pushlightuserdata(L, anim);
  lua_setfield(L, tbl, "__ow_anim_ptr");

  lua_pushvalue(L, group_idx);
  lua_setfield(L, tbl, "__ow_group");

  PushSharedAnimationMethodTable(L, runtime_anim_type);
  lua_setmetatable(L, tbl);

  if (std::strcmp(runtime_anim_type, "Path") == 0) {
    lua_newtable(L);
    lua_setfield(L, tbl, "__ow_control_points");
  }

  lua_pushvalue(L, tbl);
  anim->SetLuaObjectRef(L, luaL_ref(L, LUA_REGISTRYINDEX));

  AppendToArrayField(L, group_idx, "__ow_animations", tbl);
  RegisterNamedAnimationOnGroup(L, group_idx, tbl, anim->GetName().c_str());
  return tbl;
}

static int PushOrCreateAnimationTableForGroup(lua_State *L, int group_idx, Animation *animation) {
  group_idx = lua_absindex(L, group_idx);
  if (PushAnimationTableFromGroup(L, group_idx, animation)) {
    return lua_absindex(L, -1);
  }

  return PushAnimationTableForGroup(L, group_idx, animation);
}

static int CreateAnimationTableOnGroup(lua_State *L, const char *anim_type, int group_idx,
                                       const char *name,
                                       const bool assign_default_order = true,
                                       const char* inherits_from = nullptr) {
  group_idx = lua_absindex(L, group_idx);
  const char *runtime_anim_type = CanonicalAnimationTypeName(anim_type);

  auto *group = TryGetGroupPtr(L, group_idx);
  if (!group) {
    lua_newtable(L);
    return lua_absindex(L, -1);
  }

  const openwow::ui::framexml::UiAnimation* inherited_template = nullptr;
  if (inherits_from != nullptr) {
    const std::string inherited_name = inherits_from;
    const auto resolve_template = [L, group_idx](const std::string& template_name) {
      return ResolveAnimationTemplateForGroup(L, group_idx, template_name);
    };

    switch (ValidateAnimationTemplateChain(resolve_template, inherited_name)) {
    case AnimationTemplateResolution::Missing:
      luaL_error(L,
                 "%s:CreateAnimation(): Couldn't find inherited node \"%s\"",
                 GroupDisplayName(group),
                 inherited_name.c_str());
      break;
    case AnimationTemplateResolution::Recursive:
      luaL_error(L,
                 "%s:CreateAnimation(): Recursively inherited node \"%s\"",
                 GroupDisplayName(group),
                 inherited_name.c_str());
      break;
    case AnimationTemplateResolution::Found:
      inherited_template = resolve_template(inherited_name);
      break;
    }
  }

  const bool has_requested_name = name != nullptr && name[0] != '\0';
  const std::string resolved_name =
      has_requested_name ? ResolveAnimationObjectName(L, group_idx, name) : std::string();
  Animation *anim = group->CreateAnimation(runtime_anim_type, resolved_name);
  if (!anim) {
    lua_newtable(L);
    return lua_absindex(L, -1);
  }
  const int anim_idx = PushAnimationTableForGroup(L, group_idx, anim);
  if (has_requested_name) {
    BindAnimationObjectGlobal(L, anim_idx, resolved_name);
  }

  if (inherited_template != nullptr) {
    const auto resolve_template = [L, group_idx](const std::string& template_name) {
      return ResolveAnimationTemplateForGroup(L, group_idx, template_name);
    };
    ConfigureAnimationFromSpec(
        L, group_idx, anim_idx, anim, *inherited_template, resolve_template, nullptr, nullptr,
        true, false);
  }

  if (assign_default_order) {
    AssignDefaultLuaAnimationOrder(group, anim);
  }

  return anim_idx;
}

template <typename ResolveGroupTemplateFn, typename ResolveAnimationTemplateFn>
static void ApplyAnimationGroupSpec(
    lua_State* L,
    int frame_idx,
    int group_idx,
    AnimationGroup* group,
    const openwow::ui::framexml::UiAnimationGroup& spec,
    ResolveGroupTemplateFn&& resolve_group_template,
    ResolveAnimationTemplateFn&& resolve_animation_template,
    openwow::ui::framexml::XmlScriptCache* script_cache,
    std::unordered_set<std::string>* recursion_stack = nullptr) {
  if (L == nullptr || group == nullptr) {
    return;
  }

  frame_idx = lua_absindex(L, frame_idx);
  group_idx = lua_absindex(L, group_idx);

  std::unordered_set<std::string> local_stack;
  auto& stack = recursion_stack != nullptr ? *recursion_stack : local_stack;

  const bool inserted_name = !spec.name.empty() && stack.insert(spec.name).second;
  if (!spec.inherits.empty() && stack.find(spec.inherits) == stack.end()) {
    if (const auto* inherited = resolve_group_template(spec.inherits)) {
      ApplyAnimationGroupSpec(L,
                              frame_idx,
                              group_idx,
                              group,
                              *inherited,
                              resolve_group_template,
                              resolve_animation_template,
                              script_cache,
                              &stack);
    }
  }

  RegisterAnimationGroupParentKeyOnRegion(
      L, frame_idx, group_idx, spec.parent_key.empty() ? nullptr : spec.parent_key.c_str());

  if (!spec.looping.empty()) {
    group->SetLooping(ParseLoopTypeOrDefault(spec.looping.c_str()));
  }

  group->SetInitialOffsetPixels(spec.initial_offset_x, spec.initial_offset_y);
  AttachXmlScriptHandlers(L,
                          group,
                          spec.script_handlers,
                          openwow::ui::framexml::XmlScriptOwner::AnimationGroup,
                          script_cache);

  for (const auto& anim_spec : spec.animations) {
    CreateAnimationTableOnGroup(
        L, RuntimeAnimTypeForSpec(anim_spec), group_idx, anim_spec.name.c_str(), false);
    const int anim_idx = lua_absindex(L, -1);

    if (auto* anim = GetAnimPtr(L, anim_idx)) {
      ConfigureAnimationFromSpec(L,
                                 group_idx,
                                 anim_idx,
                                 anim,
                                 anim_spec,
                                 resolve_animation_template,
                                 script_cache,
                                 nullptr,
                                 true,
                                 false);
    }

    lua_pop(L, 1);
  }

  if (inserted_name) {
    stack.erase(spec.name);
  }
}

static int CreateAnimationGroupTableOnRegion(lua_State* L,
                                             int region_idx,
                                            const char* name,
                                            const char* inherits_from) {
  region_idx = lua_absindex(L, region_idx);
  if (!lua_istable(L, region_idx)) {
    lua_newtable(L);
    return lua_absindex(L, -1);
  }

  const openwow::ui::framexml::UiAnimationGroup* inherited_template = nullptr;
  if (inherits_from != nullptr && inherits_from[0] != '\0') {
    const std::string inherited_name = inherits_from;
    const auto resolve_group_template = [L, region_idx](const std::string& template_name) {
      return ResolveAnimationGroupTemplateForFrame(L, region_idx, template_name);
    };

    switch (ValidateAnimationGroupTemplateChain(resolve_group_template, inherited_name)) {
    case AnimationGroupTemplateResolution::Missing:
      luaL_error(L,
                 "%s:CreateAnimationGroup(): Couldn't find inherited node \"%s\"",
                 FrameDisplayName(L, region_idx),
                 inherited_name.c_str());
      break;
    case AnimationGroupTemplateResolution::Recursive:
      luaL_error(L,
                 "%s:CreateAnimationGroup(): Recursively inherited node \"%s\"",
                 FrameDisplayName(L, region_idx),
                 inherited_name.c_str());
      break;
    case AnimationGroupTemplateResolution::Found:
      inherited_template = resolve_group_template(inherited_name);
      break;
    }
  }

  const int group_idx = CreateAnimationGroupOnRegion(L, region_idx, name);
  auto* group = TryGetGroupPtr(L, group_idx);
  if (group == nullptr || inherited_template == nullptr) {
    return group_idx;
  }

  openwow::ui::framexml::XmlScriptCache script_cache;
  const auto resolve_group_template = [L, region_idx](const std::string& template_name) {
    return ResolveAnimationGroupTemplateForFrame(L, region_idx, template_name);
  };
  const auto resolve_animation_template = [L, group_idx](const std::string& template_name) {
    return ResolveAnimationTemplateForGroup(L, group_idx, template_name);
  };
  ApplyAnimationGroupSpec(L,
                          region_idx,
                          group_idx,
                          group,
                          *inherited_template,
                          resolve_group_template,
                          resolve_animation_template,
                          &script_cache);
  group->FinalizeXmlLoad();
  return group_idx;
}

static void InstallSharedAnimMethods(lua_State *L, int tbl) {
  tbl = lua_absindex(L, tbl);

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (auto *a = GetAnimPtr(Ls, 1))
      a->Play();
    return 0;
  });
  lua_setfield(L, tbl, "Play");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (auto *a = GetAnimPtr(Ls, 1))
      a->Pause();
    return 0;
  });
  lua_setfield(L, tbl, "Pause");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (auto *a = GetAnimPtr(Ls, 1))
      a->Stop(true);
    return 0;
  });
  lua_setfield(L, tbl, "Stop");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    lua_pushboolean(Ls, a && a->IsPlaying());
    return 1;
  });
  lua_setfield(L, tbl, "IsPlaying");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    lua_pushboolean(Ls, a && a->IsPaused());
    return 1;
  });
  lua_setfield(L, tbl, "IsPaused");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    lua_pushboolean(Ls, a ? a->IsStopped() : 1);
    return 1;
  });
  lua_setfield(L, tbl, "IsStopped");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    lua_pushboolean(Ls, a && a->IsDone());
    return 1;
  });
  lua_setfield(L, tbl, "IsDone");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    lua_pushboolean(Ls, a && a->IsDelaying());
    return 1;
  });
  lua_setfield(L, tbl, "IsDelaying");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    lua_pushnumber(Ls, a ? a->GetProgress() : 0.0);
    return 1;
  });
  lua_setfield(L, tbl, "GetProgress");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    lua_pushnumber(Ls, a ? a->GetSmoothProgress() : 0.0);
    return 1;
  });
  lua_setfield(L, tbl, "GetSmoothProgress");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    if (!a)
      return 0;
    if (!lua_isnumber(Ls, 2)) {
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "Usage: %s:SetSmoothProgress(smoothProgress)",
                        nm[0] ? nm : "<unnamed>");
    }
    a->SetSmoothProgress(static_cast<float>(lua_tonumber(Ls, 2)));
    return 0;
  });
  lua_setfield(L, tbl, "SetSmoothProgress");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    lua_pushnumber(Ls, a ? a->GetProgressWithDelay() : 0.0);
    return 1;
  });
  lua_setfield(L, tbl, "GetProgressWithDelay");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    if (!a)
      return 0;
    if (!lua_isnumber(Ls, 2)) {
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "Usage: %s:SetMaxFramerate(framesPerSec)", nm[0] ? nm : "<unnamed>");
    }
    a->SetMaxFramerate(static_cast<float>(lua_tonumber(Ls, 2)));
    return 0;
  });
  lua_setfield(L, tbl, "SetMaxFramerate");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    lua_pushnumber(Ls, a ? a->GetElapsed() : 0.0);
    return 1;
  });
  lua_setfield(L, tbl, "GetElapsed");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    lua_pushnumber(Ls, a ? a->GetMaxFramerate() : 0.0);
    return 1;
  });
  lua_setfield(L, tbl, "GetMaxFramerate");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    lua_pushnumber(Ls, a ? a->GetDuration() : 0.0);
    return 1;
  });
  lua_setfield(L, tbl, "GetDuration");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    if (!a)
      return 0;
    if (!lua_isnumber(Ls, 2)) {
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "Usage: %s:SetDuration(durationSec)", nm[0] ? nm : "<unnamed>");
    }
    a->SetDuration(static_cast<float>(lua_tonumber(Ls, 2)));
    return 0;
  });
  lua_setfield(L, tbl, "SetDuration");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    lua_pushnumber(Ls, a ? a->GetStartDelay() : 0.0);
    return 1;
  });
  lua_setfield(L, tbl, "GetStartDelay");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    if (!a)
      return 0;
    if (!lua_isnumber(Ls, 2)) {
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "Usage: %s:SetStartDelay(delaySec)", nm[0] ? nm : "<unnamed>");
    }
    a->SetStartDelay(static_cast<float>(lua_tonumber(Ls, 2)));
    return 0;
  });
  lua_setfield(L, tbl, "SetStartDelay");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    lua_pushnumber(Ls, a ? a->GetEndDelay() : 0.0);
    return 1;
  });
  lua_setfield(L, tbl, "GetEndDelay");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    if (!a)
      return 0;
    if (!lua_isnumber(Ls, 2)) {
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "Usage: %s:SetEndDelay(delaySec)", nm[0] ? nm : "<unnamed>");
    }
    a->SetEndDelay(static_cast<float>(lua_tonumber(Ls, 2)));
    return 0;
  });
  lua_setfield(L, tbl, "SetEndDelay");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    lua_pushinteger(Ls, a ? (a->GetOrder() + 1) : 1);
    return 1;
  });
  lua_setfield(L, tbl, "GetOrder");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    if (!a)
      return 0;
    if (!lua_isnumber(Ls, 2)) {
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "Usage: %s:SetOrder(order)", nm[0] ? nm : "<unnamed>");
    }
    const int lua_order = static_cast<int>(lua_tonumber(Ls, 2));
    a->SetOrder(lua_order - 1);
    return 0;
  });
  lua_setfield(L, tbl, "SetOrder");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    float smooth_in = 0.0f;
    float smooth_out = 0.0f;
    if (a != nullptr) {
      a->GetSmoothWeights(smooth_in, smooth_out);
    }
    lua_pushstring(Ls, ui::SmoothingFloatsToString(smooth_in, smooth_out));
    return 1;
  });
  lua_setfield(L, tbl, "GetSmoothing");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    if (!a)
      return 0;
    if (!lua_isstring(Ls, 2)) {
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "Usage: %s:SetSmoothing(\"smoothingType\")", nm[0] ? nm : "<unnamed>");
    }
    const char *s = lua_tostring(Ls, 2);
    float smooth_in = 0.0f;
    float smooth_out = 0.0f;
    if (!ui::SmoothingStringToFloats(s, &smooth_in, &smooth_out)) {
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "%s:SetSmoothing(): Unknown smoothing type specified",
                        nm[0] ? nm : "<unnamed>");
    }
    a->SetSmoothControlPoint(smooth_in, smooth_out);
    return 0;
  });
  lua_setfield(L, tbl, "SetSmoothing");

  lua_pushcfunction(L, PushAnimationRegionParent);
  lua_setfield(L, tbl, "GetRegionParent");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    if (!a) {
      return 0;
    }

    const char *handler =
        RequireExistingAnimationScriptHandler(Ls, a, "SetScript(\"type\", function)");
    const int value_type = lua_type(Ls, 3);
    if (value_type == LUA_TFUNCTION) {
      lua_pushvalue(Ls, 3);
      const int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
      a->SetScriptRef(handler, Ls, ref);
    } else if (value_type == LUA_TNIL) {
      a->SetScriptRef(handler, Ls, LUA_NOREF);
    } else {
      return luaL_error(Ls, "Usage: %s:SetScript(\"type\", function)",
                        AnimationDisplayName(a));
    }
    return 0;
  });
  lua_setfield(L, tbl, "SetScript");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    if (!a) {
      return 0;
    }

    const char *handler = RequireAnimationScriptHandlerString(Ls, a, "HasScript(\"type\")");
    if (NormalizeAnimScriptHandler(handler) != nullptr) {
      lua_pushnumber(Ls, 1.0);
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  });
  lua_setfield(L, tbl, "HasScript");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    if (!a) {
      return 0;
    }

    const char *handler =
        RequireExistingAnimationScriptHandler(Ls, a, "GetScript(\"type\")");
    const int ref = a->GetScriptRef(handler);
    if (ref != LUA_NOREF) {
      lua_rawgeti(Ls, LUA_REGISTRYINDEX, ref);
      return 1;
    }

    lua_pushnil(Ls);
    return 1;
  });
  lua_setfield(L, tbl, "GetScript");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    if (!a) {
      return 0;
    }

    const char *handler =
        RequireExistingAnimationScriptHandler(Ls, a, "HookScript(\"type\", function)");
    if (lua_type(Ls, 3) != LUA_TFUNCTION) {
      return luaL_error(Ls, "Usage: %s:HookScript(\"type\", function)",
                        AnimationDisplayName(a));
    }

    const int oldRef = a->GetScriptRef(handler);
    if (oldRef != LUA_NOREF) {

      const int inherited_taint_source = a->ScriptTaintSource(handler);
      const int caller_taint =
          openwow::ui::lua_get_execution_taint_state(Ls).source;
      lua_rawgeti(Ls, LUA_REGISTRYINDEX, oldRef);
      const int original_taint = openwow::ui::lua_get_taint(Ls, -1);
      lua_pushvalue(Ls, 3);

      openwow::ui::lua_set_taint(Ls, -2, original_taint);
      openwow::ui::lua_set_taint(Ls, -1, caller_taint);
      openwow::ui::PushLuaCallOriginalThenHookClosure<
          openwow::ui::game::ProfiledPCall>(
          Ls, openwow::ui::kGameLuaErrorHandlerRegistryKey);
      openwow::ui::lua_set_taint(Ls, -1, original_taint);
      const int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
      a->SetScriptRef(handler, Ls, ref, inherited_taint_source);
    } else {
      lua_pushvalue(Ls, 3);
      const int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
      a->SetScriptRef(handler, Ls, ref);
    }
    return 0;
  });
  lua_setfield(L, tbl, "HookScript");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *animation = GetAnimPtr(Ls, 1);
    if (animation == nullptr) {
      return 0;
    }
    lua_pushstring(Ls, animation->GetObjectTypeName());
    return 1;
  });
  lua_setfield(L, tbl, "GetObjectType");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *animation = GetAnimPtr(Ls, 1);
    if (animation == nullptr) {
      return 0;
    }
    if (lua_isstring(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: %s:IsObjectType(\"type\")",
                        AnimationDisplayName(animation));
    }

    const char *query = lua_tostring(Ls, 2);
    if (animation->IsObjectType(query)) {
      lua_pushnumber(Ls, 1.0);
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  });
  lua_setfield(L, tbl, "IsObjectType");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    if (a && !a->GetName().empty()) {
      lua_pushstring(Ls, a->GetName().c_str());
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  });
  lua_setfield(L, tbl, "GetName");

  lua_pushcfunction(L, PushAnimationParent);
  lua_setfield(L, tbl, "GetParent");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtr(Ls, 1);
    if (!a)
      return 0;
    if (lua_isnoneornil(Ls, 2)) {
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "%s:SetParent(): Cannot set a 'nil' parent for animations",
                        nm[0] ? nm : "<unnamed>");
    }
    AnimationGroup *new_parent = nullptr;
    int new_parent_idx = 0;
    bool pushed_parent_table = false;
    if (lua_isstring(Ls, 2)) {
      const char *target_name = lua_tostring(Ls, 2);
      new_parent = TryPushGlobalAnimationGroup(Ls, target_name, &new_parent_idx);
      if (!new_parent) {
        const char *nm = a->GetName().c_str();
        return luaL_error(Ls, "%s:SetParent(): Couldn't find animation group named '%s'",
                          nm[0] ? nm : "<unnamed>",
                          target_name != nullptr ? target_name : "");
      }
      pushed_parent_table = true;
    } else if (lua_istable(Ls, 2)) {
      lua_getfield(Ls, 2, "__ow_type");
      const char *parent_type = lua_tostring(Ls, -1);
      const bool wrong_parent_type =
          parent_type != nullptr && std::strcmp(parent_type, "AnimationGroup") != 0;
      lua_pop(Ls, 1);
      if (wrong_parent_type) {
        const char *nm = a->GetName().c_str();
        return luaL_error(Ls, "%s:SetParent(): Wrong parent object type, expected AnimationGroup",
                          nm[0] ? nm : "<unnamed>");
      }
      new_parent = TryGetGroupPtr(Ls, 2);
      if (!new_parent) {
        const char *nm = a->GetName().c_str();
        return luaL_error(Ls, "%s:SetParent(): Couldn't find 'this' in parent object",
                          nm[0] ? nm : "<unnamed>");
      }
      new_parent_idx = lua_absindex(Ls, 2);
    } else {
      const char *target = lua_tostring(Ls, 2);
      if (!target) {
        target = luaL_typename(Ls, 2);
      }
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "%s:SetParent(): Couldn't find animation group named '%s'",
                        nm[0] ? nm : "<unnamed>", target);
    }

    AnimationGroup *old_parent = a->GetGroup();
    const bool parent_changed = old_parent != new_parent;
    const int old_order = a->GetOrder();
    if (lua_isnumber(Ls, 3)) {
      a->SetOrder(static_cast<int>(lua_tonumber(Ls, 3)) - 1, !parent_changed, false);
    } else {
      int new_order = a->GetOrder();
      if (new_order < 0) {
        new_order = new_parent->GetMaxOrder() + 1;
      }
      a->SetOrder(new_order, !parent_changed, true);
    }

    lua_getfield(Ls, 1, "__ow_group");
    const int old_group_idx = lua_absindex(Ls, -1);
    bool moved = false;
    if (old_parent != nullptr) {
      moved = old_parent->ReparentAnimation(*a, new_parent, old_order);
    }
    if (moved) {
      if (lua_istable(Ls, old_group_idx)) {
        RemoveAnimationTableFromGroup(Ls, old_group_idx, a);
        UnregisterNamedAnimationOnGroup(Ls, old_group_idx, 1, a->GetName().c_str());
      }
      lua_pushvalue(Ls, 1);
      PrependToArrayField(Ls, new_parent_idx, "__ow_animations", -1);
      lua_pop(Ls, 1);
      RegisterNamedAnimationOnGroup(Ls, new_parent_idx, 1, a->GetName().c_str());
      lua_pushvalue(Ls, new_parent_idx);
      lua_setfield(Ls, 1, "__ow_group");
    } else if (old_order != a->GetOrder() && lua_istable(Ls, old_group_idx) &&
               old_group_idx != new_parent_idx) {
      lua_pushvalue(Ls, old_group_idx);
      lua_setfield(Ls, 1, "__ow_group");
    }
    lua_pop(Ls, 1);
    if (pushed_parent_table) {
      lua_pop(Ls, 1);
    }
    return 0;
  });
  lua_setfield(L, tbl, "SetParent");
}

static void InstallAlphaMethods(lua_State *L, int tbl) {
  tbl = lua_absindex(L, tbl);

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtrChecked<AlphaAnim>(Ls, 1, AnimKind::Alpha);
    if (!a)
      return 0;
    if (!lua_isnumber(Ls, 2)) {
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "Usage: %s:SetChange(change)", nm[0] ? nm : "<unnamed>");
    }
    a->SetChange(static_cast<float>(lua_tonumber(Ls, 2)));
    return 0;
  });
  lua_setfield(L, tbl, "SetChange");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetAnimPtrChecked<AlphaAnim>(Ls, 1, AnimKind::Alpha);
    lua_pushnumber(Ls, a ? a->GetChange() : 0.0);
    return 1;
  });
  lua_setfield(L, tbl, "GetChange");
}

static void InstallScaleMethods(lua_State *L, int tbl) {
  tbl = lua_absindex(L, tbl);

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetScalePtrChecked(Ls, 1);
    if (!a)
      return 0;
    if (!lua_isstring(Ls, 2) || !lua_isnumber(Ls, 3) || !lua_isnumber(Ls, 4)) {
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "Usage: %s:SetOrigin(point, offsetX, offsetY)",
                        nm[0] ? nm : "<unnamed>");
    }
    std::uint32_t point = 4;
    float offsets[2] = {0.0f, 0.0f};
    openwow::ui::ParseFramePointFromLua(&point, 2, Ls, offsets);
    a->SetOriginStored(point, offsets[0], offsets[1]);
    return 0;
  });
  lua_setfield(L, tbl, "SetOrigin");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetScalePtrChecked(Ls, 1);
    if (a) {
      std::string point;
      float x = 0.0f;
      float y = 0.0f;
      a->GetOriginStored(point, x, y);
      lua_pushstring(Ls, point.c_str());
      lua_pushnumber(Ls, x);
      lua_pushnumber(Ls, y);
    } else {
      lua_pushstring(Ls, "CENTER");
      lua_pushnumber(Ls, 0);
      lua_pushnumber(Ls, 0);
    }
    return 3;
  });
  lua_setfield(L, tbl, "GetOrigin");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetScalePtrChecked(Ls, 1);
    if (!a)
      return 0;
    if (!lua_isnumber(Ls, 2) || !lua_isnumber(Ls, 3)) {
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "Usage: %s:SetScale(x, y)", nm[0] ? nm : "<unnamed>");
    }
    float x = static_cast<float>(lua_tonumber(Ls, 2));
    float y = static_cast<float>(lua_tonumber(Ls, 3));
    a->SetScaleDelta(x, y);
    return 0;
  });
  lua_setfield(L, tbl, "SetScale");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetScalePtrChecked(Ls, 1);
    if (!a) {
      return 0;
    }
    float x = 0.0f;
    float y = 0.0f;
    a->GetScaleDelta(x, y);
    lua_pushnumber(Ls, x);
    lua_pushnumber(Ls, y);
    return 2;
  });
  lua_setfield(L, tbl, "GetScale");

}

static void InstallTranslationMethods(lua_State *L, int tbl) {
  tbl = lua_absindex(L, tbl);

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetTranslationPtrChecked(Ls, 1);
    if (!a)
      return 0;
    if (!lua_isnumber(Ls, 2) || !lua_isnumber(Ls, 3)) {
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "Usage: %s:SetOffset(x, y)", nm[0] ? nm : "<unnamed>");
    }
    a->SetOffset(static_cast<float>(lua_tonumber(Ls, 2)), static_cast<float>(lua_tonumber(Ls, 3)));
    return 0;
  });
  lua_setfield(L, tbl, "SetOffset");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetTranslationPtrChecked(Ls, 1);
    if (!a) {
      return 0;
    }
    float x = 0.0f;
    float y = 0.0f;
    a->GetOffset(x, y);
    lua_pushnumber(Ls, x);
    lua_pushnumber(Ls, y);
    return 2;
  });
  lua_setfield(L, tbl, "GetOffset");
}

static void InstallRotationMethods(lua_State *L, int tbl) {
  tbl = lua_absindex(L, tbl);

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetRotationPtrChecked(Ls, 1);
    if (!a)
      return 0;
    if (!lua_isnumber(Ls, 2)) {
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "Usage: %s:SetDegrees(degrees)", nm[0] ? nm : "<unnamed>");
    }
    a->SetDegrees(lua_tonumber(Ls, 2));
    return 0;
  });
  lua_setfield(L, tbl, "SetDegrees");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetRotationPtrChecked(Ls, 1);
    lua_pushnumber(Ls, a ? a->GetDegrees() : 0.0);
    return 1;
  });
  lua_setfield(L, tbl, "GetDegrees");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetRotationPtrChecked(Ls, 1);
    if (!a)
      return 0;
    if (!lua_isnumber(Ls, 2)) {
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "Usage: %s:SetRadians(radians)", nm[0] ? nm : "<unnamed>");
    }
    a->SetRadians(static_cast<float>(lua_tonumber(Ls, 2)));
    return 0;
  });
  lua_setfield(L, tbl, "SetRadians");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetRotationPtrChecked(Ls, 1);
    lua_pushnumber(Ls, a ? a->GetRadians() : 0.0);
    return 1;
  });
  lua_setfield(L, tbl, "GetRadians");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetRotationPtrChecked(Ls, 1);
    if (!a)
      return 0;
    if (!lua_isstring(Ls, 2) || !lua_isnumber(Ls, 3) || !lua_isnumber(Ls, 4)) {
      const char *nm = a->GetName().c_str();
      return luaL_error(Ls, "Usage: %s:SetOrigin(point, offsetX, offsetY)",
                        nm[0] ? nm : "<unnamed>");
    }
    std::uint32_t point = 4;
    float offsets[2] = {0.0f, 0.0f};
    openwow::ui::ParseFramePointFromLua(&point, 2, Ls, offsets);
    a->SetOriginStored(point, offsets[0], offsets[1]);
    return 0;
  });
  lua_setfield(L, tbl, "SetOrigin");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *a = GetRotationPtrChecked(Ls, 1);
    if (a) {
      std::string point;
      float x = 0.0f;
      float y = 0.0f;
      a->GetOriginStored(point, x, y);
      lua_pushstring(Ls, point.c_str());
      lua_pushnumber(Ls, x);
      lua_pushnumber(Ls, y);
    } else {
      lua_pushstring(Ls, "CENTER");
      lua_pushnumber(Ls, 0);
      lua_pushnumber(Ls, 0);
    }
    return 3;
  });
  lua_setfield(L, tbl, "GetOrigin");
}

static void InstallPathMethods(lua_State *L, int tbl) {
  tbl = lua_absindex(L, tbl);

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *path = GetPathPtrChecked(Ls, 1);
    if (!path)
      return 0;
    if (!lua_isstring(Ls, 2)) {
      const char *nm = path->GetName().c_str();
      return luaL_error(Ls, "Usage: %s:SetCurve(\"curveType\")", nm[0] ? nm : "<unnamed>");
    }

    int curve_type = 0;
    const char *curve = lua_tostring(Ls, 2);
    if (!ParseCurveTypeString(curve, &curve_type)) {
      const char *nm = path->GetName().c_str();
      return luaL_error(Ls, "%s:SetCurve(): Unknown curve type specified",
                        nm[0] ? nm : "<unnamed>");
    }

    path->SetCurve(static_cast<uint8_t>(curve_type));
    return 0;
  });
  lua_setfield(L, tbl, "SetCurve");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *path = GetPathPtrChecked(Ls, 1);
    if (!path)
      return 0;
    lua_pushstring(Ls, openwow::ui::CurveTypeToString(path->GetCurve()));
    return 1;
  });
  lua_setfield(L, tbl, "GetCurve");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *path = GetPathPtrChecked(Ls, 1);
    if (!path)
      return 0;
    lua_pushnumber(Ls, path->GetMaxOrder() + 1);
    return 1;
  });
  lua_setfield(L, tbl, "GetMaxOrder");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *path = GetPathPtrChecked(Ls, 1);
    if (!path)
      return 0;

    int inherited_template_idx = 0;
    const char* inherited_name = nullptr;
    if (lua_type(Ls, 3) == LUA_TSTRING) {
      inherited_name = lua_tostring(Ls, 3);
      if (!PushNamedControlPointTemplateForPath(Ls, 1, inherited_name)) {
        return luaL_error(Ls,
                          "%s:CreateControlPoint(): Couldn't find inherited node \"%s\"",
                          PathDisplayName(path),
                          inherited_name);
      }
      inherited_template_idx = lua_absindex(Ls, -1);
    }

    const char *name = luaL_optstring(Ls, 2, "");
    auto *point = path->CreateControlPoint(name ? name : "");
    if (!point) {
      lua_pushnil(Ls);
      return 1;
    }

    PushNewControlPointTable(Ls, 1, point);
    int point_idx = lua_absindex(Ls, -1);

    if (inherited_template_idx != 0) {
      ApplyControlPointTemplateTable(
          Ls, 1, point, &point_idx, inherited_template_idx, nullptr);
      lua_remove(Ls, inherited_template_idx);
    }

    if (lua_isnumber(Ls, 4)) {
      point->SetOrder(static_cast<int>(lua_tonumber(Ls, 4)) - 1, false, false);
    } else if (point->GetOrder() == -1) {
      point->SetOrder(path->GetMaxOrder(), false, true);
    }
    path->OnControlPointOrderChanged(*point);

    AppendToArrayField(Ls, 1, "__ow_control_points", -1);
    return 1;
  });
  lua_setfield(L, tbl, "CreateControlPoint");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *path = GetPathPtrChecked(Ls, 1);
    if (!path)
      return 0;
    const int point_count = static_cast<int>(path->GetNumControlPoints());
    if (point_count <= 0) {
      return 0;
    }
    if (!EnsureScriptReturnStackCapacity(Ls, point_count)) {
      return luaL_error(Ls, "%s:GetControlPoints(): Stack overflow", PathDisplayName(path));
    }
    for (int index = 0; index < point_count; ++index) {
      auto *point = path->GetControlPoint(static_cast<size_t>(index));
      PushOrCreateControlPointTableForPath(Ls, 1, point);
    }
    return point_count;
  });
  lua_setfield(L, tbl, "GetControlPoints");
}

void PushAnimTable(lua_State *L, const char *anim_type, int group_idx) {
  CreateAnimationTableOnGroup(L, anim_type, group_idx, "", true);
}

void UnregisterAnimationScriptMethods(lua_State* L) {

  UnregisterSharedAnimationMethodTable(L, "Animation");
  UnregisterSharedAnimationMethodTable(L, "Translation");
  UnregisterSharedAnimationMethodTable(L, "Rotation");
  UnregisterSharedAnimationMethodTable(L, "Scale");
  ClearRegistryCachedMethodTable(L, kControlPointMethodTableRegistryKey);
  UnregisterSharedAnimationMethodTable(L, "Path");
  UnregisterSharedAnimationMethodTable(L, "Alpha");
  ClearRegistryCachedMethodTable(L, kAnimationGroupMethodTableRegistryKey);
}

static void InstallGroupMethods(lua_State *L, int tbl) {
  tbl = lua_absindex(L, tbl);

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    GetGroupPtrChecked(Ls, 1)->Play();
    return 0;
  });
  lua_setfield(L, tbl, "Play");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    GetGroupPtrChecked(Ls, 1)->Pause();
    return 0;
  });
  lua_setfield(L, tbl, "Pause");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    GetGroupPtrChecked(Ls, 1)->Stop(true);
    return 0;
  });
  lua_setfield(L, tbl, "Stop");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    GetGroupPtrChecked(Ls, 1)->Finish();
    return 0;
  });
  lua_setfield(L, tbl, "Finish");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    lua_pushboolean(Ls, GetGroupPtrChecked(Ls, 1)->IsPlaying());
    return 1;
  });
  lua_setfield(L, tbl, "IsPlaying");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    lua_pushboolean(Ls, GetGroupPtrChecked(Ls, 1)->IsPaused());
    return 1;
  });
  lua_setfield(L, tbl, "IsPaused");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    lua_pushboolean(Ls, GetGroupPtrChecked(Ls, 1)->IsDone());
    return 1;
  });
  lua_setfield(L, tbl, "IsDone");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    lua_pushnumber(Ls, GetGroupPtrChecked(Ls, 1)->GetProgress());
    return 1;
  });
  lua_setfield(L, tbl, "GetProgress");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    lua_pushnumber(Ls, GetGroupPtrChecked(Ls, 1)->GetDuration());
    return 1;
  });
  lua_setfield(L, tbl, "GetDuration");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *group = GetGroupPtrChecked(Ls, 1);
    if (!group) {
      return 0;
    }
    if (!lua_isstring(Ls, 2)) {
      return luaL_error(Ls, "Usage: %s:SetLooping(\"loopType\")",
                        GroupDisplayName(group));
    }

    AnimLoopType loop_type = AnimLoopType::None;
    const char *loop_name = lua_tostring(Ls, 2);
    if (!TryParseLoopType(loop_name, &loop_type)) {
      return luaL_error(Ls, "%s:SetLooping(): Unknown loop type specified",
                        GroupDisplayName(group));
    }

    group->SetLooping(loop_type);
    return 0;
  });
  lua_setfield(L, tbl, "SetLooping");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    lua_pushstring(Ls, LoopTypeToString(GetGroupPtrChecked(Ls, 1)->GetLooping()));
    return 1;
  });
  lua_setfield(L, tbl, "GetLooping");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    GetGroupPtrChecked(Ls, 1);
    const char *anim_type = luaL_optstring(Ls, 2, "Animation");
    const char *name = luaL_optstring(Ls, 3, "");
    const char* inherits_from =
        lua_type(Ls, 4) == LUA_TSTRING ? lua_tostring(Ls, 4) : nullptr;
    CreateAnimationTableOnGroup(Ls, anim_type, 1, name, true, inherits_from);
    return 1;
  });
  lua_setfield(L, tbl, "CreateAnimation");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *group = GetGroupPtrChecked(Ls, 1);
    if (!group) {
      return 0;
    }

    const int animation_count = static_cast<int>(group->GetNumAnimations());
    if (animation_count <= 0) {
      return 0;
    }

    if (!EnsureScriptReturnStackCapacity(Ls, animation_count)) {
      return luaL_error(Ls, "%s:GetAnimations(): Stack overflow", GroupDisplayName(group));
    }

    for (int index = 0; index < animation_count; ++index) {
      auto *animation = group->GetAnimation(static_cast<size_t>(index));
      PushOrCreateAnimationTableForGroup(Ls, 1, animation);
    }
    return animation_count;
  });
  lua_setfield(L, tbl, "GetAnimations");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *g = GetGroupPtrChecked(Ls, 1);
    const char *handler =
        RequireExistingGroupScriptHandler(Ls, g, "SetScript(\"type\", function)");
    const int value_type = lua_type(Ls, 3);
    if (value_type == LUA_TFUNCTION) {
      lua_pushvalue(Ls, 3);
      int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
      g->SetScriptRef(handler, Ls, ref);
    } else if (value_type == LUA_TNIL) {
      g->SetScriptRef(handler, Ls, LUA_NOREF);
    } else {
      return luaL_error(Ls, "Usage: %s:SetScript(\"type\", function)", GroupDisplayName(g));
    }
    return 0;
  });
  lua_setfield(L, tbl, "SetScript");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *g = GetGroupPtrChecked(Ls, 1);
    const char *handler = RequireGroupScriptHandlerString(Ls, g, "HasScript(\"type\")");
    if (NormalizeAnimGroupScriptHandler(handler) != nullptr) {
      lua_pushnumber(Ls, 1.0);
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  });
  lua_setfield(L, tbl, "HasScript");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *g = GetGroupPtrChecked(Ls, 1);
    const char *handler = RequireExistingGroupScriptHandler(Ls, g, "GetScript(\"type\")");
    int ref = g->GetScriptRef(handler);
    if (ref != LUA_NOREF) {
      lua_rawgeti(Ls, LUA_REGISTRYINDEX, ref);
      return 1;
    }
    lua_pushnil(Ls);
    return 1;
  });
  lua_setfield(L, tbl, "GetScript");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    GetGroupPtrChecked(Ls, 1);
    lua_getfield(Ls, 1, "__ow_parent");
    return 1;
  });
  lua_setfield(L, tbl, "GetParent");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *g = GetGroupPtrChecked(Ls, 1);
    if (g->GetName().empty()) {
      lua_pushnil(Ls);
    } else {
      lua_pushstring(Ls, g->GetName().c_str());
    }
    return 1;
  });
  lua_setfield(L, tbl, "GetName");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    GetGroupPtrChecked(Ls, 1);
    lua_pushstring(Ls, "AnimationGroup");
    return 1;
  });
  lua_setfield(L, tbl, "GetObjectType");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *g = GetGroupPtrChecked(Ls, 1);
    if (!lua_isstring(Ls, 2)) {
      return luaL_error(Ls, "Usage: %s:IsObjectType(\"type\")", GroupDisplayName(g));
    }
    const char *t = lua_tostring(Ls, 2);
    const bool match = IcaseEqual(t, "AnimationGroup") || IcaseEqual(t, "Object");
    if (match) {
      lua_pushnumber(Ls, 1.0);
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  });
  lua_setfield(L, tbl, "IsObjectType");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *g = GetGroupPtrChecked(Ls, 1);
    const char *handler =
        RequireExistingGroupScriptHandler(Ls, g, "HookScript(\"type\", function)");
    if (lua_type(Ls, 3) != LUA_TFUNCTION) {
      return luaL_error(Ls, "Usage: %s:HookScript(\"type\", function)", GroupDisplayName(g));
    }
    int oldRef = g->GetScriptRef(handler);
    if (oldRef != LUA_NOREF) {

      const int inherited_taint_source = g->ScriptTaintSource(handler);
      const int caller_taint =
          openwow::ui::lua_get_execution_taint_state(Ls).source;
      lua_rawgeti(Ls, LUA_REGISTRYINDEX, oldRef);
      const int original_taint = openwow::ui::lua_get_taint(Ls, -1);
      lua_pushvalue(Ls, 3);

      openwow::ui::lua_set_taint(Ls, -2, original_taint);
      openwow::ui::lua_set_taint(Ls, -1, caller_taint);
      openwow::ui::PushLuaCallOriginalThenHookClosure<
          openwow::ui::game::ProfiledPCall>(
          Ls, openwow::ui::kGameLuaErrorHandlerRegistryKey);
      openwow::ui::lua_set_taint(Ls, -1, original_taint);
      int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
      g->SetScriptRef(handler, Ls, ref, inherited_taint_source);
    } else {
      lua_pushvalue(Ls, 3);
      int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
      g->SetScriptRef(handler, Ls, ref);
    }
    return 0;
  });
  lua_setfield(L, tbl, "HookScript");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    lua_pushstring(Ls, LoopStateToString(GetGroupPtrChecked(Ls, 1)->GetLoopState()));
    return 1;
  });
  lua_setfield(L, tbl, "GetLoopState");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    lua_pushboolean(Ls, GetGroupPtrChecked(Ls, 1)->IsPendingFinish());
    return 1;
  });
  lua_setfield(L, tbl, "IsPendingFinish");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *g = GetGroupPtrChecked(Ls, 1);
    float x = 0.0f;
    float y = 0.0f;
    g->GetInitialOffsetPixels(x, y);
    lua_pushnumber(Ls, x);
    lua_pushnumber(Ls, y);
    return 2;
  });
  lua_setfield(L, tbl, "GetInitialOffset");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *g = GetGroupPtrChecked(Ls, 1);
    if (!lua_isnumber(Ls, 2) || !lua_isnumber(Ls, 3)) {
      return luaL_error(Ls, "Usage: %s:SetInitialOffset(x, y)", GroupDisplayName(g));
    }
    float x = static_cast<float>(lua_tonumber(Ls, 2));
    float y = static_cast<float>(lua_tonumber(Ls, 3));
    g->SetInitialOffsetPixels(x, y);
    return 0;
  });
  lua_setfield(L, tbl, "SetInitialOffset");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *g = GetGroupPtrChecked(Ls, 1);
    lua_pushnumber(Ls, g->GetMaxOrder() + 1);
    return 1;
  });
  lua_setfield(L, tbl, "GetMaxOrder");
}

static void PushSharedAnimationGroupMethodTable(lua_State *L) {

  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(L);
  lua_getfield(L, LUA_REGISTRYINDEX, kAnimationGroupMethodTableRegistryKey);
  if (lua_istable(L, -1)) {
    return;
  }
  lua_pop(L, 1);

  lua_newtable(L);
  const int methods_idx = lua_absindex(L, -1);
  InstallGroupMethods(L, methods_idx);

  lua_pushvalue(L, methods_idx);
  lua_setfield(L, methods_idx, "__index");

  lua_pushvalue(L, methods_idx);
  lua_setfield(L, LUA_REGISTRYINDEX, kAnimationGroupMethodTableRegistryKey);
}

void PushAnimGroupTable(lua_State *L, int parent_frame_idx) {
  parent_frame_idx = lua_absindex(L, parent_frame_idx);

  void *mem = lua_newuserdatauv(L, sizeof(AnimationGroup), 0);
  auto *group = new (mem) AnimationGroup();
  int ud_idx = lua_absindex(L, -1);

  lua_newtable(L);
  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    auto *g = static_cast<AnimationGroup *>(lua_touserdata(Ls, 1));
    if (g)
      g->~AnimationGroup();
    return 0;
  });
  lua_setfield(L, -2, "__gc");
  lua_setmetatable(L, ud_idx);

  lua_newtable(L);
  int tbl = lua_absindex(L, -1);

  lua_pushstring(L, "AnimationGroup");
  lua_setfield(L, tbl, "__ow_type");

  lua_pushlightuserdata(L, group);
  lua_setfield(L, tbl, "__ow_anim_group_ptr");
  AttachAnimationObjectHandle(L, tbl, group);

  lua_pushvalue(L, parent_frame_idx);
  lua_setfield(L, tbl, "__ow_parent");

  lua_pushvalue(L, ud_idx);
  lua_setfield(L, tbl, "__ow_anim_group_ud");

  lua_newtable(L);
  lua_setfield(L, tbl, "__ow_animations");

  lua_newtable(L);
  lua_setfield(L, tbl, "__ow_named_anims");

  PushSharedAnimationGroupMethodTable(L);
  lua_setmetatable(L, tbl);
  group->SetParentFrame(const_cast<void *>(lua_topointer(L, parent_frame_idx)));

  auto *script_obj = static_cast<openwow::ui::widgets::CScriptObject *>(
      openwow::ui::game::detail::GetLuaNativeScriptObjectThisPointer(L, parent_frame_idx));
  if (auto *region = dynamic_cast<openwow::ui::widgets::CScriptRegion *>(script_obj)) {
    group->SetOwnerRegion(region);
  }

  lua_pushvalue(L, tbl);
  const int object_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  group->SetLuaObjectRef(L, object_ref);
  lua_pushinteger(L, object_ref);
  lua_setfield(L, tbl, "__ow_ref");

  lua_remove(L, ud_idx);
}

void ApplyAnimationFrameMethods(lua_State *L) {
  ApplyAnimationRegionMethods(L);
}

void MaterializeAnimationGroups(lua_State *L, int frame_idx,
                                const std::vector<openwow::ui::framexml::UiAnimationGroup> &groups,
                                openwow::ui::framexml::XmlScriptCache *script_cache) {
  frame_idx = lua_absindex(L, frame_idx);
  if (!lua_istable(L, frame_idx) || groups.empty()) {
    return;
  }

  RegisterFrameAnimationGroupTemplates(L, frame_idx, groups);
  RegisterFrameAnimationTemplates(L, frame_idx, groups);
  RegisterFrameControlPointTemplates(L, frame_idx, groups);
  const auto resolve_group_template = [&groups](const std::string& name) {
    return ResolveAnimationGroupTemplate(groups, name);
  };
  const auto resolve_animation_template = [&groups](const std::string& name) {
    return ResolveAnimationTemplate(groups, name);
  };

  for (auto group_it = groups.rbegin(); group_it != groups.rend(); ++group_it) {
    const auto& group_spec = *group_it;
    const char *group_name =
        group_spec.name.empty() ? nullptr : group_spec.name.c_str();
    const int group_idx = CreateAnimationGroupOnRegion(L, frame_idx, group_name);
    if (auto *group = TryGetGroupPtr(L, group_idx)) {
      ApplyAnimationGroupSpec(L,
                              frame_idx,
                              group_idx,
                              group,
                              group_spec,
                              resolve_group_template,
                              resolve_animation_template,
                              script_cache);
      group->FinalizeXmlLoad();
    }

    lua_pop(L, 1);
  }
}

void ApplyFrameXmlLoadBehavior(lua_State* L,
                               int frame_idx,
                               int parent_idx,
                               const openwow::ui::framexml::UiFrame& frame,
                               openwow::ui::framexml::XmlScriptCache* script_cache) {
  if (L == nullptr) {
    return;
  }

  frame_idx = lua_absindex(L, frame_idx);
  if (lua_istable(L, frame_idx) == 0) {
    return;
  }

  if (!frame.parent_keys.empty() && parent_idx != 0) {
    parent_idx = lua_absindex(L, parent_idx);
    if (lua_istable(L, parent_idx) != 0) {
      for (const auto& parent_key : frame.parent_keys) {
        if (parent_key.empty()) {
          continue;
        }
        lua_pushvalue(L, frame_idx);
        lua_setfield(L, parent_idx, parent_key.c_str());
      }
    }
  }

  if (frame.animation_groups.empty()) {
    return;
  }

  lua_getfield(L, frame_idx, kXmlAnimationGroupsLoadedField);
  const bool already_loaded = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  if (already_loaded) {
    return;
  }

  MaterializeAnimationGroups(L, frame_idx, frame.animation_groups, script_cache);
  lua_pushboolean(L, 1);
  lua_setfield(L, frame_idx, kXmlAnimationGroupsLoadedField);
}

}
