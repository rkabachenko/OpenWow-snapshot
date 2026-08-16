#pragma once

#include <cstdint>
#include <string_view>

namespace openwow::data::dbc { class DbcLoader; }

namespace openwow::ui {

enum class CursorRuntimeTextureMode : std::uint8_t {
  kNone = 0,
  kHeldTexture = 1,
  kCopiedPath = 2,
  kDirectPath = 3,
};

void ResetRuntimeCursorTextureState();

void ClearRuntimeCursorTexture();

void LoadCursorTextures();

void SetCursorFromObject(const char* name, uint64_t object_guid);

void Cursor_LoadHeldTextureByDisplayId(uint32_t display_id,
                                       const openwow::data::dbc::DbcLoader* dbc);

void Cursor_LoadRuntimeTexturePath(std::string_view texture_path,
                                   CursorRuntimeTextureMode mode);

std::string_view GetHeldTexturePath();

std::string_view GetRuntimeTexturePath();

[[nodiscard]] CursorRuntimeTextureMode GetRuntimeTextureMode();

[[nodiscard]] bool HasRuntimeCursorTexture();

void SyncRuntimeCursorPresentation();

int GxuDetectAlphaFillBug();

}
