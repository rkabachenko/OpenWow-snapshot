#include "openwow/ui/game/framescript/widgets/model_widget_methods.h"

#include "openwow/foundation/math/vec3_normalize_if_length_squared_exceeds_client_epsilon.h"
#include "openwow/ui/game/framescript/core/frame_lua_receiver.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/lua_table_field.h"
#include "openwow/ui/widgets/simple_model.h"

#include <lua.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#undef lua_pushcfunction
#define lua_pushcfunction(L, ...) lua_pushcclosure(L, (__VA_ARGS__), 0)

namespace openwow::ui::game::frame_api {
namespace {

using detail::TruncateLuaNumberToSseI32;

constexpr const char* kModelLightUsageAddLight =
    "Usage: %s:AddLight(index, enabled[, omni, dirX, dirY, dirZ, "
    "ambIntensity[, ambR, ambG, ambB], dirIntensity[, dirR, dirG, dirB]])";
constexpr const char* kModelLightUsageSetLight =
    "Usage: %s:SetLight(enabled[, omni, dirX, dirY, dirZ, "
    "ambIntensity[, ambR, ambG, ambB], dirIntensity[, dirR, dirG, dirB]])";
constexpr const char* kModelLightUsageAddCharacterLight =
    "Usage: %s:AddCharacterLight(index, enabled[, omni, dirX, dirY, dirZ, "
    "ambIntensity[, ambR, ambG, ambB], dirIntensity[, dirR, dirG, dirB]])";

using ModelLightBank = openwow::ui::widgets::CSimpleModel::LightBank;

struct ParsedModelLight {
  bool enabled{false};
  bool omni{true};
  float dir_x{0.0f};
  float dir_y{0.0f};
  float dir_z{0.0f};
  float amb_r{0.0f};
  float amb_g{0.0f};
  float amb_b{0.0f};
  float dir_r{0.0f};
  float dir_g{0.0f};
  float dir_b{0.0f};
};

void NormalizeParsedModelLightDirection(ParsedModelLight &light) {
  if (light.omni) {
    return;
  }

  float direction[3] = {light.dir_x, light.dir_y, light.dir_z};
  openwow::math::vec3::NormalizeInPlaceIfLengthSquaredExceedsClientEpsilon(direction);
  light.dir_x = direction[0];
  light.dir_y = direction[1];
  light.dir_z = direction[2];
}

ParsedModelLight ToParsedModelLight(
    const openwow::ui::widgets::CSimpleModel::LightSettings& light) {
  return {
      .enabled = light.enabled,
      .omni = light.omni,
      .dir_x = light.dirX,
      .dir_y = light.dirY,
      .dir_z = light.dirZ,
      .amb_r = light.ambientR,
      .amb_g = light.ambientG,
      .amb_b = light.ambientB,
      .dir_r = light.diffuseR,
      .dir_g = light.diffuseG,
      .dir_b = light.diffuseB,
  };
}

openwow::ui::widgets::CSimpleModel::LightSettings ToModelLightSettings(
    const ParsedModelLight& light) {
  return {
      .enabled = light.enabled,
      .omni = light.omni,
      .dirX = light.dir_x,
      .dirY = light.dir_y,
      .dirZ = light.dir_z,
      .ambientR = light.amb_r,
      .ambientG = light.amb_g,
      .ambientB = light.amb_b,
      .diffuseR = light.dir_r,
      .diffuseG = light.dir_g,
      .diffuseB = light.dir_b,
  };
}

bool MatchesModelLightObjectType(const char *type_name) {
  if (type_name == nullptr || *type_name == '\0') {
    return false;
  }
  return std::strcmp(type_name, "Model") == 0 || std::strcmp(type_name, "ModelFFX") == 0 ||
         std::strcmp(type_name, "PlayerModel") == 0 ||
         std::strcmp(type_name, "DressUpModel") == 0 || std::strcmp(type_name, "TabardModel") == 0;
}

int ValidateModelLightSelf(lua_State *L) {
  if (lua_istable(L, 1) == 0) {
    luaL_error(L, "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
  }
  (void)openwow::ui::game::detail::CanonicalizeLuaScriptObjectTable(L, 1);

  const char *type_name = openwow::ui::BorrowRawLuaStringField(L, 1, "__ow_type");
  if (type_name == nullptr || *type_name == '\0') {
    luaL_error(L, "Attempt to find 'this' in non-framescript object");
  }
  if (!MatchesModelLightObjectType(type_name)) {
    luaL_error(L, "Wrong object type for member function");
  }
  return lua_absindex(L, 1);
}

openwow::ui::widgets::CSimpleModel& RequireNativeModel(lua_State* lua) {
  const int self = ValidateModelLightSelf(lua);
  auto* object = lua_adapter::BorrowNativeScriptObject(lua, self);
  if (object == nullptr) {
    luaL_error(lua, "Attempt to find 'this' in non-framescript object");
  }
  return *static_cast<openwow::ui::widgets::CSimpleModel*>(object);
}

std::string GetModelLightUsageObjectName(lua_State *L, int self_idx) {
  const char *name = openwow::ui::BorrowRawLuaStringField(L, self_idx, "__ow_name");
  if (name == nullptr) {
    return "<unnamed>";
  }
  return std::string(name);
}

bool TryReadNumericFlag(lua_State *L, int index, bool *value) {
  if (lua_isnumber(L, index) == 0) {
    return false;
  }
  *value = TruncateLuaNumberToSseI32(lua_tonumber(L, index)) != 0;
  return true;
}

float ClampUnitInterval(const double value) {
  if (value < 0.0) {
    return 0.0f;
  }
  if (value >= 1.0) {
    return 1.0f;
  }
  return static_cast<float>(value);
}

bool ParseModelLightArguments(lua_State *L, int first_arg, ParsedModelLight *out_light) {
  if (out_light == nullptr) {
    return false;
  }

  ParsedModelLight light;
  if (!TryReadNumericFlag(L, first_arg, &light.enabled)) {
    return false;
  }
  if (!light.enabled) {
    *out_light = light;
    return true;
  }

  if (!TryReadNumericFlag(L, first_arg + 1, &light.omni) || lua_isnumber(L, first_arg + 2) == 0 ||
      lua_isnumber(L, first_arg + 3) == 0 || lua_isnumber(L, first_arg + 4) == 0 ||
      lua_isnumber(L, first_arg + 5) == 0) {
    return false;
  }

  light.dir_x = static_cast<float>(lua_tonumber(L, first_arg + 2));
  light.dir_y = static_cast<float>(lua_tonumber(L, first_arg + 3));
  light.dir_z = static_cast<float>(lua_tonumber(L, first_arg + 4));
  NormalizeParsedModelLightDirection(light);

  const float ambient_intensity = static_cast<float>(lua_tonumber(L, first_arg + 5));
  int next_arg = first_arg + 6;
  float ambient_r = 1.0f;
  float ambient_g = 1.0f;
  float ambient_b = 1.0f;
  if (ambient_intensity != 0.0f && lua_isnumber(L, next_arg) != 0 &&
      lua_isnumber(L, next_arg + 1) != 0 && lua_isnumber(L, next_arg + 2) != 0) {
    ambient_r = ClampUnitInterval(lua_tonumber(L, next_arg));
    ambient_g = ClampUnitInterval(lua_tonumber(L, next_arg + 1));
    ambient_b = ClampUnitInterval(lua_tonumber(L, next_arg + 2));
    next_arg += 3;
  }
  light.amb_r = ambient_intensity * ambient_r;
  light.amb_g = ambient_intensity * ambient_g;
  light.amb_b = ambient_intensity * ambient_b;

  if (lua_isnumber(L, next_arg) != 0) {
    const float directional_intensity = static_cast<float>(lua_tonumber(L, next_arg));
    float directional_r = 1.0f;
    float directional_g = 1.0f;
    float directional_b = 1.0f;
    if (directional_intensity != 0.0f && lua_isnumber(L, next_arg + 1) != 0 &&
        lua_isnumber(L, next_arg + 2) != 0 && lua_isnumber(L, next_arg + 3) != 0) {
      directional_r = ClampUnitInterval(lua_tonumber(L, next_arg + 1));
      directional_g = ClampUnitInterval(lua_tonumber(L, next_arg + 2));
      directional_b = ClampUnitInterval(lua_tonumber(L, next_arg + 3));
    }
    light.dir_r = directional_intensity * directional_r;
    light.dir_g = directional_intensity * directional_g;
    light.dir_b = directional_intensity * directional_b;
  }

  *out_light = light;
  return true;
}

int GetModelLightSlot(lua_State *L, int index) {
  if (lua_isnumber(L, index) == 0) {
    return 0;
  }
  return static_cast<int>(lua_tonumber(L, index)) != 0 ? 1 : 0;
}

void AppendModelLight(lua_State* lua, const ModelLightBank bank,
                      const std::size_t slot,
                      const ParsedModelLight& light) {
  (void)RequireNativeModel(lua).AddLight(bank, slot,
                                           ToModelLightSettings(light));
}

}

void ApplyModelLightMethods(lua_State *L) {
  int f = lua_absindex(L, -1);

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const ParsedModelLight light =
        ToParsedModelLight(RequireNativeModel(Ls).GetLightSettings());
    lua_pushboolean(Ls, light.omni);
    lua_pushnumber(Ls, light.dir_x);
    lua_pushnumber(Ls, light.dir_y);
    lua_pushnumber(Ls, light.dir_z);
    lua_pushnumber(Ls, light.amb_r);
    lua_pushnumber(Ls, light.amb_g);
    lua_pushnumber(Ls, light.amb_b);
    lua_pushnumber(Ls, light.dir_r);
    lua_pushnumber(Ls, light.dir_g);
    lua_pushnumber(Ls, light.dir_b);
    return 10;
  });
  lua_setfield(L, f, "GetLight");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateModelLightSelf(Ls);
    ParsedModelLight light;
    if (!ParseModelLightArguments(Ls, 2, &light)) {
      const std::string name = GetModelLightUsageObjectName(Ls, self_idx);
      return luaL_error(Ls, kModelLightUsageSetLight, name.c_str());
    }
    auto& model = RequireNativeModel(Ls);
    model.SetLight(light.enabled, light.omni, light.dir_x, light.dir_y,
                   light.dir_z, light.amb_r, light.amb_g, light.amb_b,
                   light.dir_r, light.dir_g, light.dir_b);
    return 0;
  });
  lua_setfield(L, f, "SetLight");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    RequireNativeModel(Ls).ResetLights();
    return 0;
  });
  lua_setfield(L, f, "ResetLights");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateModelLightSelf(Ls);
    ParsedModelLight light;
    if (!ParseModelLightArguments(Ls, 3, &light)) {
      const std::string name = GetModelLightUsageObjectName(Ls, self_idx);
      return luaL_error(Ls, kModelLightUsageAddLight, name.c_str());
    }
    AppendModelLight(Ls, ModelLightBank::General, GetModelLightSlot(Ls, 2),
                     light);
    return 0;
  });
  lua_setfield(L, f, "AddLight");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateModelLightSelf(Ls);
    ParsedModelLight light;
    if (!ParseModelLightArguments(Ls, 3, &light)) {
      const std::string name = GetModelLightUsageObjectName(Ls, self_idx);
      return luaL_error(Ls, kModelLightUsageAddCharacterLight, name.c_str());
    }
    AppendModelLight(Ls, ModelLightBank::Character,
                     GetModelLightSlot(Ls, 2), light);
    return 0;
  });
  lua_setfield(L, f, "AddCharacterLight");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateModelLightSelf(Ls);
    ParsedModelLight light;
    if (!ParseModelLightArguments(Ls, 3, &light)) {
      const std::string name = GetModelLightUsageObjectName(Ls, self_idx);
      return luaL_error(Ls, kModelLightUsageAddCharacterLight, name.c_str());
    }
    AppendModelLight(Ls, ModelLightBank::Pet, GetModelLightSlot(Ls, 2),
                     light);
    return 0;
  });
  lua_setfield(L, f, "AddPetLight");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateModelLightSelf(Ls);
    if (lua_isnumber(Ls, 2) == 0) {
      const std::string name = GetObjectNameOrUnnamed(Ls, self_idx);
      return luaL_error(Ls, "Usage: %s:SetGlow(value)", name.c_str());
    }
    const char* type_name =
        openwow::ui::BorrowRawLuaStringField(Ls, self_idx, "__ow_type");
    if (type_name != nullptr && std::strcmp(type_name, "ModelFFX") == 0) {
      lua_pushnumber(Ls, lua_tonumber(Ls, 2));
      lua_setfield(Ls, self_idx, "__ow_glow");
    }
    return 0;
  });
  lua_setfield(L, f, "SetGlow");
}

void ApplyPlayerModelIconTextureMethods(lua_State *L) {
  int f = lua_absindex(L, -1);

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_isstring(Ls, 2)) {
      const std::string name =
          lua_istable(Ls, 1) ? GetObjectNameOrUnnamed(Ls, 1)
                             : std::string("<unnamed>");
      return luaL_error(Ls,
                        "Usage: %s:ReplaceIconTexture(\"texture\")",
                        name.c_str());
    }
    if (lua_istable(Ls, 1)) {
      const char *tex = lua_tostring(Ls, 2);
      lua_pushstring(Ls, tex ? tex : "");
      lua_setfield(Ls, 1, "__ow_replace_icon_tex");
    }
    return 0;
  });
  lua_setfield(L, f, "ReplaceIconTexture");
}

}
