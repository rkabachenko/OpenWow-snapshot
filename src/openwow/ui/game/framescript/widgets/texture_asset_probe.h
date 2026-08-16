#pragma once
#include <cstdint>
#include <string>
#include <string_view>
struct lua_State;
namespace openwow::ui::game::frame_api {
struct TextureValidationPerformanceCounters {
  std::uint64_t requests{0};
  std::uint64_t cache_hits{0};
  std::uint64_t source_reads{0};
};
bool CanLoadTextureRequest(lua_State* lua, std::string_view requested_path);

void QueueRegionTextureLoad(lua_State* lua, const std::string& texture_path);
void ClearLuaStringField(lua_State* lua, int self_index, const char* field_name);
[[nodiscard]] TextureValidationPerformanceCounters
GetTextureValidationPerformanceCounters(lua_State* lua);
void ResetTextureValidationPerformanceCounters(lua_State* lua);
}
