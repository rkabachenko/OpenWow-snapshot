#pragma once

#include "openwow/render/ui/ui_texture_capabilities.h"

#include <bgfx/bgfx.h>
#include <string>

namespace openwow::render::ui {

struct UiProgramHandles {
  bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_tex = BGFX_INVALID_HANDLE;
};

UiProgramHandles LoadUiProgram();

void DestroyUiProgram(UiProgramHandles& handles);

void DestroyUiProgram(bgfx::ProgramHandle& program, bgfx::UniformHandle& s_tex);

bool IsValidUiProgram(const UiProgramHandles& handles);

bool ReloadUiProgram(UiProgramHandles& handles);

bool PrewarmUiProgram();

bool RetainUiProgramCache();

void ReleaseUiProgramCache();

void InvalidateUiProgramCache();

std::string GetRendererTypeName();

std::string GetUiProgramDiagnostics(const UiProgramHandles& handles);

void LogUiProgramInfo(const UiProgramHandles& handles);

}
