#include "openwow/render/ui/ui_shaders.h"

#include "openwow/render/backend/bgfx/renderer_context_services.h"
#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/foundation/diagnostics/logging.h"

namespace openwow::render::ui {

namespace {

struct UiProgramCache {
  UiProgramHandles handles{};
  bgfx::RendererType::Enum renderer_type{bgfx::RendererType::Noop};
  std::size_t retain_count{0};
};

UiProgramCache& MutableUiProgramCache() {
  static UiProgramCache cache;
  return cache;
}

[[nodiscard]] bool UiProgramsMatch(const UiProgramHandles& a,
                                   const UiProgramHandles& b) {
  return a.program.idx == b.program.idx && a.s_tex.idx == b.s_tex.idx;
}

UiProgramHandles CreateUiProgram(bgfx::RendererType::Enum type) {
  UiProgramHandles out;

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "LoadUiProgram: compiling for renderer type "
                         + GetRendererTypeName());

  out.program = openwow::render::CreateEmbeddedProgram(
      openwow::render::ShaderProgramId::Ui, type);
  out.s_tex = bgfx::createUniform("s_uiTex", bgfx::UniformType::Sampler);

  if (!bgfx::isValid(out.program) || !bgfx::isValid(out.s_tex)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "LoadUiProgram: createProgram/uniform failed");
    if (bgfx::isValid(out.program)) bgfx::destroy(out.program);
    if (bgfx::isValid(out.s_tex)) bgfx::destroy(out.s_tex);
    out.program = BGFX_INVALID_HANDLE;
    out.s_tex = BGFX_INVALID_HANDLE;
    return out;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "LoadUiProgram: successfully created UI program");
  return out;
}

void DestroyCacheHandles(UiProgramCache& cache) {
  if (openwow::render::IsRendererContextActive()) {
    if (bgfx::isValid(cache.handles.program)) {
      bgfx::destroy(cache.handles.program);
    }
    if (bgfx::isValid(cache.handles.s_tex)) {
      bgfx::destroy(cache.handles.s_tex);
    }
  }
  cache.handles = UiProgramHandles{};
  cache.renderer_type = bgfx::RendererType::Noop;
}

UiProgramHandles EnsureUiProgramCache() {
  auto& cache = MutableUiProgramCache();
  if (!openwow::render::IsRendererContextActive()) {
    DestroyCacheHandles(cache);
    return cache.handles;
  }

  const auto type = bgfx::getRendererType();
  if (!IsValidUiProgram(cache.handles) || cache.renderer_type != type) {
    InvalidateUiProgramCache();
    cache.handles = CreateUiProgram(type);
    if (IsValidUiProgram(cache.handles)) {
      cache.renderer_type = type;
    }
  }
  return cache.handles;
}

}

UiProgramHandles LoadUiProgram() {
  auto& cache = MutableUiProgramCache();
  cache.handles = EnsureUiProgramCache();
  return cache.handles;
}

void DestroyUiProgram(UiProgramHandles& handles) {
  auto& cache = MutableUiProgramCache();
  if (IsValidUiProgram(cache.handles) && UiProgramsMatch(handles, cache.handles)) {
    handles = UiProgramHandles{};
    return;
  }

  if (openwow::render::IsRendererContextActive()) {
    if (bgfx::isValid(handles.program)) bgfx::destroy(handles.program);
    if (bgfx::isValid(handles.s_tex)) bgfx::destroy(handles.s_tex);
  }
  handles = UiProgramHandles{};
}

void DestroyUiProgram(bgfx::ProgramHandle& program, bgfx::UniformHandle& s_tex) {
  UiProgramHandles handles{program, s_tex};
  DestroyUiProgram(handles);
  program = handles.program;
  s_tex = handles.s_tex;
}

bool IsValidUiProgram(const UiProgramHandles& handles) {
  return bgfx::isValid(handles.program) && bgfx::isValid(handles.s_tex);
}

bool ReloadUiProgram(UiProgramHandles& handles) {
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "ReloadUiProgram: destroying old program and reloading");

  InvalidateUiProgramCache();
  handles = EnsureUiProgramCache();

  if (!IsValidUiProgram(handles)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                       "ReloadUiProgram: failed to reload UI program");
    return false;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "ReloadUiProgram: successfully reloaded UI program");
  return true;
}

bool PrewarmUiProgram() {
  return IsValidUiProgram(EnsureUiProgramCache());
}

bool RetainUiProgramCache() {
  auto& cache = MutableUiProgramCache();
  cache.handles = EnsureUiProgramCache();
  if (!IsValidUiProgram(cache.handles)) {
    return false;
  }

  ++cache.retain_count;
  return true;
}

void ReleaseUiProgramCache() {
  auto& cache = MutableUiProgramCache();
  if (cache.retain_count != 0) {
    --cache.retain_count;
  }
}

void InvalidateUiProgramCache() {
  auto& cache = MutableUiProgramCache();
  DestroyCacheHandles(cache);
  cache.retain_count = 0;
}

bool UiTextureStateSupported(UiTextureState state) {
  switch (state) {
    case UiTextureState::kNormal:
    case UiTextureState::kDesaturated:
      return IsValidUiProgram(MutableUiProgramCache().handles);
  }

  return false;
}

std::string GetRendererTypeName() {
  switch (bgfx::getRendererType()) {
    case bgfx::RendererType::Noop:       return "Noop";
    case bgfx::RendererType::Agc:        return "AGC";
    case bgfx::RendererType::Direct3D11: return "Direct3D11";
    case bgfx::RendererType::Direct3D12: return "Direct3D12";
    case bgfx::RendererType::Gnm:        return "GNM";
    case bgfx::RendererType::Metal:      return "Metal";
    case bgfx::RendererType::Nvn:        return "NVN";
    case bgfx::RendererType::OpenGLES:   return "OpenGLES";
    case bgfx::RendererType::OpenGL:     return "OpenGL";
    case bgfx::RendererType::Vulkan:     return "Vulkan";
    default:                             return "Unknown";
  }
}

std::string GetUiProgramDiagnostics(const UiProgramHandles& handles) {
  std::string diag;
  diag.reserve(256);
  diag += "UiProgram Diagnostics\n";
  diag += "  renderer : " + GetRendererTypeName() + "\n";
  diag += "  program  : ";
  if (bgfx::isValid(handles.program)) {
    diag += "valid (idx=" + std::to_string(handles.program.idx) + ")\n";
  } else {
    diag += "INVALID\n";
  }
  diag += "  s_tex    : ";
  if (bgfx::isValid(handles.s_tex)) {
    diag += "valid (idx=" + std::to_string(handles.s_tex.idx) + ")\n";
  } else {
    diag += "INVALID\n";
  }
  diag += "  ready    : ";
  diag += (IsValidUiProgram(handles) ? "yes" : "NO");
  diag += "\n";
  return diag;
}

void LogUiProgramInfo(const UiProgramHandles& handles) {
  const std::string diag = GetUiProgramDiagnostics(handles);
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, diag);
}

}
