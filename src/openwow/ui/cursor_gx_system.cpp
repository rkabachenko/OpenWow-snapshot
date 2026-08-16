
#include "openwow/ui/cursor_gx_system.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/inventory/items/item_icon_resolver.h"

#include <string>

namespace openwow::ui {

namespace {

struct RuntimeCursorState {
  CursorRuntimeTextureMode mode = CursorRuntimeTextureMode::kNone;
  std::string held_texture_path;
  std::string runtime_texture_path;
};

RuntimeCursorState& GetRuntimeCursorState() {
  static RuntimeCursorState state;
  return state;
}

void PublishRuntimeCursorState() {
  auto* const cursor = openwow::game::GetActiveCursorSurface();
  if (cursor == nullptr) {
    return;
  }

  const auto& state = GetRuntimeCursorState();
  if (state.mode == CursorRuntimeTextureMode::kNone) {
    cursor->ClearRuntimeCursorTexture();
    return;
  }
  (void)cursor->SetRuntimeCursorTexture(state.runtime_texture_path);
}

void RequestRuntimeCursorTexture(std::string texture_path,
                                 const CursorRuntimeTextureMode mode) {
  auto& state = GetRuntimeCursorState();
  state.mode = mode;
  state.runtime_texture_path = std::move(texture_path);
  state.held_texture_path =
      mode == CursorRuntimeTextureMode::kHeldTexture
          ? state.runtime_texture_path
          : std::string{};
  PublishRuntimeCursorState();
}

std::string ResolveObjectCursorTexturePath(const char* const name,
                                           const std::uint64_t object_guid) {
  if (name == nullptr || *name == '\0' || object_guid == 0u) {
    return {};
  }

  std::string path(name);
  if (path.find_first_of("/\\") == std::string::npos) {
    path.insert(0, "Interface\\Icons\\");
  }
  return path;
}

}

void ResetRuntimeCursorTextureState() {
  if (auto* const cursor = openwow::game::GetActiveCursorSurface();
      cursor != nullptr) {
    cursor->ResetRuntimeCursorTextureState();
  }
  GetRuntimeCursorState() = {};
}

void ClearRuntimeCursorTexture() {
  auto& state = GetRuntimeCursorState();
  if (state.mode == CursorRuntimeTextureMode::kNone) {
    return;
  }

  state = {};
  if (auto* const cursor = openwow::game::GetActiveCursorSurface();
      cursor != nullptr) {
    cursor->ClearRuntimeCursorTexture();
  }
}

void LoadCursorTextures() {
  ResetRuntimeCursorTextureState();
  if (auto* const cursor = openwow::game::GetActiveCursorSurface();
      cursor != nullptr) {
    cursor->SetBaseRetailCursorType(1u);
    cursor->SetImmediateCursorType(1u);
  }
}

void SetCursorFromObject(const char* name, uint64_t object_guid) {
  RequestRuntimeCursorTexture(
      ResolveObjectCursorTexturePath(name, object_guid),
      CursorRuntimeTextureMode::kHeldTexture);
}

void Cursor_LoadHeldTextureByDisplayId(uint32_t display_id,
                                       const data::dbc::DbcLoader* dbc) {
  std::string path;
  if (display_id != 0u) {
    path = ::openwow::game::ResolveItemInventoryIconTexturePath(dbc, display_id);
  }
  RequestRuntimeCursorTexture(std::move(path),
                              CursorRuntimeTextureMode::kHeldTexture);
}

void Cursor_LoadRuntimeTexturePath(const std::string_view texture_path,
                                   const CursorRuntimeTextureMode mode) {
  if (mode == CursorRuntimeTextureMode::kNone) {
    ClearRuntimeCursorTexture();
    return;
  }
  RequestRuntimeCursorTexture(std::string(texture_path), mode);
}

std::string_view GetHeldTexturePath() {
  return GetRuntimeCursorState().held_texture_path;
}

std::string_view GetRuntimeTexturePath() {
  return GetRuntimeCursorState().runtime_texture_path;
}

CursorRuntimeTextureMode GetRuntimeTextureMode() {
  return GetRuntimeCursorState().mode;
}

bool HasRuntimeCursorTexture() {
  return GetRuntimeCursorState().mode != CursorRuntimeTextureMode::kNone;
}

void SyncRuntimeCursorPresentation() {
  PublishRuntimeCursorState();
}

int GxuDetectAlphaFillBug() {
    return 0;
}

}
