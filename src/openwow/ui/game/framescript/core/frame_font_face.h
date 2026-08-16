#pragma once

#include <string>

struct lua_State;

namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::ui::game::frame_api {

enum class SetFontFailurePolicy { kReturnNil, kFontString };

[[nodiscard]] bool IsAbsoluteFontPath(const std::string& path);
[[nodiscard]] bool ValidateLuaFontObjectFace(
    const std::string& path, float stored_height,
    const openwow::vfs::VirtualFileSystem* vfs);
int SharedSetFontWorker(
    lua_State* lua, int self_index, const char* object_name,
    SetFontFailurePolicy failure_policy = SetFontFailurePolicy::kReturnNil);

}
