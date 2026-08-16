#include "openwow/render/scene/floating_text.h"
#include "openwow/render/resources/fonts/font_string_flags.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace openwow::render {

namespace {

constexpr std::size_t kFloatingTextMaxStoredChars = 63;

constexpr std::uint32_t kDamageScatterSteps = 100u;
constexpr float kDamageScatterWidth = 0.5f;

std::string TruncateFloatingText(std::string_view text) {
  if (text.size() <= kFloatingTextMaxStoredChars) {
    return std::string(text);
  }
  return std::string(text.substr(0, kFloatingTextMaxStoredChars));
}

std::uint32_t CurrentFloatingTextTick() {
  return openwow::core::GameClock::GetTickCount32();
}

struct WorldTextAnimData {
  float height_rate;
  float fade_in_frac;
  float fade_out_frac;
  float duration_sec;
  float scale_rel;
  bool  bounce_curve;
  std::uint32_t default_color;
};

static constexpr WorldTextAnimData kAnimTable[11] = {

    {  2.0f,   0.100f,  0.507f,  1.5f,  1.0f,  false, 0xFFFFFFFFu},
    {  2.0f,   0.100f,  0.060f,  1.5f,  1.0f,  false, 0xFFFFFFFFu},
    {  0.0f,   0.100f,  0.667f,  1.5f,  1.5f,  true,  0xFFFFFFFFu},
    {  2.0f,   0.100f,  0.667f,  1.5f,  1.0f,  false, 0xFFFFFFFFu},
    {  0.0f,   0.111f,  0.444f,  4.5f,  1.0f,  false, 0x8094008Bu},
    {  0.0f,   0.111f,  0.444f,  4.5f,  1.0f,  false, 0xFFE0CA0Au},
    {  2.0f,   0.100f,  0.507f,  1.5f,  1.0f,  false, 0xFF00FF00u},
    {  0.0f,   0.100f,  0.667f,  1.5f,  1.5f,  true,  0xFF00FF00u},
    {  6.0f,   0.100f,  0.507f,  1.5f,  1.5f,  false, 0xFFFFFFFFu},
    {  6.0f,   0.100f,  0.507f,  1.5f,  1.5f,  false, 0xFF000000u},
    {  2.0f,   0.100f,  0.507f,  1.5f,  1.0f,  false, 0xFFFF9900u},
};

struct ScaleCurveSegment {
  float t_start, t_end;
  float scale_start, scale_end;
};

static constexpr ScaleCurveSegment kBounceCurve[3] = {
    {0.0f, 0.1f, 0.1f, 2.0f},
    {0.1f, 0.2f, 2.0f, 1.0f},
    {0.2f, 1.0f, 1.0f, 1.0f},
};

const WorldTextAnimData& GetAnimData(std::uint32_t raw_type) {
  if (raw_type < std::size(kAnimTable)) return kAnimTable[raw_type];
  return kAnimTable[0];
}

std::uint32_t DefaultColorForType(std::uint32_t raw_type) {
  return GetAnimData(raw_type).default_color;
}

float ComputeScale(const WorldTextAnimData& anim, float progress) {
  if (!anim.bounce_curve) {
    return anim.scale_rel;
  }
  float curve_val = 1.0f;
  for (const auto& seg : kBounceCurve) {
    if (progress >= seg.t_start && progress < seg.t_end) {
      float t = (progress - seg.t_start) / (seg.t_end - seg.t_start);
      curve_val = seg.scale_start + t * (seg.scale_end - seg.scale_start);
      break;
    }
  }
  return std::max(curve_val * anim.scale_rel, 0.001f);
}

float ComputeAlpha(const WorldTextAnimData& anim, float progress) {
  if (progress < anim.fade_in_frac) {
    if (anim.fade_in_frac <= 0.0f) return 1.0f;

    return std::clamp(progress / anim.fade_in_frac, 0.0f, 1.0f);
  }
  if (progress < anim.fade_out_frac) {
    return 1.0f;
  }
  float remaining = 1.0f - anim.fade_out_frac;
  if (remaining <= 0.0f) return 0.0f;
  float fadeout = (progress - anim.fade_out_frac) / remaining;
  return std::clamp(1.0f - fadeout, 0.0f, 1.0f);
}

float ComputeHeightOffset(const WorldTextAnimData& anim, float progress) {
  return progress * anim.height_rate;
}

}

std::optional<FloatingTextEntry> BuildFloatingTextEntry(
    const FloatingTextCreateSpec& spec) {
  if (spec.text.empty()) {
    return std::nullopt;
  }

  FloatingTextEntry entry{};
  entry.world_x = spec.world_x;
  entry.world_y = spec.world_y;
  entry.world_z = spec.world_z;
  entry.text = TruncateFloatingText(spec.text);
  entry.raw_type = spec.raw_type;
  entry.creation_tick_ms = spec.creation_tick_ms;
  entry.color_override = spec.color_override;
  if (spec.raw_type == 5 && spec.auxiliary_value.has_value()) {
    entry.auxiliary_value = spec.auxiliary_value;
  }
  entry.color = spec.color_override.value_or(spec.base_color);
  entry.font_size = spec.font_size;
  entry.life = 0.0f;

  entry.max_life = GetAnimData(spec.raw_type).duration_sec;
  entry.velocity_y = spec.velocity_y;
  entry.critical = spec.critical;
  return entry;
}

float ComputeFloatingTextRenderScale(const std::uint32_t raw_type,
                                     const float font_size,
                                     const float progress) {
  return font_size * ComputeScale(GetAnimData(raw_type), progress);
}

float ComputeFloatingTextRenderAlpha(const std::uint32_t raw_type,
                                     const float progress) {
  return ComputeAlpha(GetAnimData(raw_type),
                      std::clamp(progress, 0.0f, 1.0f));
}

bool FloatingTextRenderer::Initialize() {
  if (initialized_) return true;

  initialized_ = true;
  const auto fallback_metrics =
      WorldOverlayMetrics::FromFramebuffer(
          openwow::ui::kFallbackUiViewportWidth,
          openwow::ui::kFallbackUiViewportHeight, 1.0f);
  if (!EnsureDamageTextFont(fallback_metrics)) {
    initialized_ = false;
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                        "FloatingTextRenderer: text renderer init failed");
    return false;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                      "FloatingTextRenderer: initialized");
  return true;
}

void FloatingTextRenderer::Shutdown() {
  ReleaseRendererDeviceResources();
  entries_.clear();
  font_pixel_height_ = 0;
  initialized_ = false;
}

void FloatingTextRenderer::ReleaseRendererDeviceResources() {
  text_renderer_.Shutdown();
}

bool FloatingTextRenderer::RestoreRendererDeviceResources() {
  if (!initialized_) {
    return false;
  }
  if (font_pixel_height_ > 0) {
    return text_renderer_.Init(font_pixel_height_);
  }
  return EnsureDamageTextFont(WorldOverlayMetrics::FromFramebuffer(
      openwow::ui::kFallbackUiViewportWidth,
      openwow::ui::kFallbackUiViewportHeight, 1.0f));
}

void FloatingTextRenderer::AddDamage(float wx, float wy, float wz,
                                     std::uint32_t amount, bool critical) {

  wx += (static_cast<float>(random_.Next() % kDamageScatterSteps) /
         static_cast<float>(kDamageScatterSteps) - 0.5f)
        * kDamageScatterWidth;

  const std::uint32_t raw_type = critical ? 2u : 0u;
  auto entry = BuildFloatingTextEntry(FloatingTextCreateSpec{
      .raw_type = raw_type,
      .world_x = wx,
      .world_y = wy,
      .world_z = wz,
      .text = std::to_string(amount),
      .base_color = DefaultColorForType(raw_type),
      .font_size = kNormalScale,
      .max_life = critical ? kCritLife : kDefaultLife,
      .velocity_y = critical ? kCritFloatSpeed : kFloatSpeed,
      .critical = critical,
      .creation_tick_ms = CurrentFloatingTextTick(),
  });
  if (entry.has_value()) {
    AddEntry(std::move(*entry));
  }
}

void FloatingTextRenderer::AddHealing(float wx, float wy, float wz,
                                      std::uint32_t amount, bool critical,
                                      std::uint32_t raw_type) {

  const auto resolved_raw_type = raw_type != 0 ? raw_type : (critical ? 7u : 6u);
  auto entry = BuildFloatingTextEntry(FloatingTextCreateSpec{
      .raw_type = resolved_raw_type,
      .world_x = wx,
      .world_y = wy,
      .world_z = wz,
      .text = "+" + std::to_string(amount),
      .base_color = DefaultColorForType(resolved_raw_type),
      .font_size = kNormalScale,
      .max_life = critical ? kCritLife : kDefaultLife,
      .velocity_y = critical ? kCritFloatSpeed : kFloatSpeed,
      .critical = critical,
      .creation_tick_ms = CurrentFloatingTextTick(),
  });
  if (entry.has_value()) {
    AddEntry(std::move(*entry));
  }
}

void FloatingTextRenderer::AddMiss(float wx, float wy, float wz) {
  AddText(wx, wy, wz, "MISS", DefaultColorForType(3u), 3);
}

void FloatingTextRenderer::AddDodge(float wx, float wy, float wz) {
  AddText(wx, wy, wz, "DODGE", DefaultColorForType(3u), 3);
}

void FloatingTextRenderer::AddParry(float wx, float wy, float wz) {
  AddText(wx, wy, wz, "PARRY", DefaultColorForType(3u), 3);
}

void FloatingTextRenderer::AddBlock(float wx, float wy, float wz,
                                    std::uint32_t blocked_amount) {
  AddText(wx, wy, wz, "BLOCK (" + std::to_string(blocked_amount) + ")",
          DefaultColorForType(3u), 3);
}

void FloatingTextRenderer::AddResist(float wx, float wy, float wz) {

  AddText(wx, wy, wz, "RESIST", DefaultColorForType(3u), 3);
}

void FloatingTextRenderer::AddImmune(float wx, float wy, float wz) {
  AddText(wx, wy, wz, "IMMUNE", DefaultColorForType(3u), 3);
}

void FloatingTextRenderer::AddText(float wx, float wy, float wz,
                                   const std::string& text, std::uint32_t color,
                                   std::uint32_t raw_type,
                                   std::optional<std::uint32_t> color_override,
                                   std::optional<std::uint32_t> auxiliary_value) {
  auto entry = BuildFloatingTextEntry(FloatingTextCreateSpec{
      .raw_type = raw_type,
      .world_x = wx,
      .world_y = wy,
      .world_z = wz,
      .text = text,
      .base_color = color,
      .font_size = kNormalScale,
      .max_life = kDefaultLife,
      .velocity_y = kFloatSpeed,
      .critical = false,
      .color_override = color_override,
      .auxiliary_value = auxiliary_value,
      .creation_tick_ms = CurrentFloatingTextTick(),
  });
  if (entry.has_value()) {
    AddEntry(std::move(*entry));
  }
}

void FloatingTextRenderer::AddEntry(FloatingTextEntry&& entry) {
  if (entries_.size() >= kMaxEntries) {

    entries_.erase(entries_.begin());
  }
  entries_.push_back(std::move(entry));
}

void FloatingTextRenderer::Update(float dt) {
  for (auto& e : entries_) {
    e.life += dt;

  }

  entries_.erase(
      std::remove_if(entries_.begin(), entries_.end(),
                     [](const FloatingTextEntry& e) { return e.life >= e.max_life; }),
      entries_.end());
}

void FloatingTextRenderer::Render(std::uint8_t view_id,
                                   const WorldOverlayMetrics& metrics,
                                   const float* view_mtx,
                                   const float* proj_mtx) {
  if (!initialized_ || entries_.empty()) return;
  const float screen_w = metrics.framebuffer_width;
  const float screen_h = metrics.framebuffer_height;
  if (screen_w <= 0.0f || screen_h <= 0.0f) return;
  if (!EnsureDamageTextFont(metrics)) return;

  text_renderer_.BeginFrame(view_id, screen_w, screen_h);

  for (const auto& e : entries_) {
    if (e.max_life <= 0.0f) continue;
    const float progress = std::clamp(e.life / e.max_life, 0.0f, 1.0f);
    const auto& anim = GetAnimData(e.raw_type);

    const float render_z = e.world_z + ComputeHeightOffset(anim, progress);

    float sx, sy;
    if (!WorldToScreen(e.world_x, e.world_y, render_z,
                       view_mtx, proj_mtx, metrics, sx, sy)) {
      continue;
    }

    const float alpha = ComputeFloatingTextRenderAlpha(e.raw_type, progress);

    const float scale =
        ComputeFloatingTextRenderScale(e.raw_type, e.font_size, progress);

    const auto ext = text_renderer_.Measure(e.text, scale);
    const float tx = sx - ext.width * 0.5f;
    const float ty = sy - ext.height * 0.5f;

    text_renderer_.DrawTextAlpha(view_id, tx, ty, e.text, e.color, alpha, scale);
  }
}

bool FloatingTextRenderer::EnsureDamageTextFont(
    const WorldOverlayMetrics& metrics) {
  const int pixel_height = std::max(
      1, openwow::render::ClampRetailFontPixelHeight(static_cast<int>(
             std::lround(metrics.DamageTextFontPixelHeight()))));
  if (text_renderer_.is_ready() && font_pixel_height_ == pixel_height) {
    return true;
  }

  text_renderer_.Shutdown();

  if (text_renderer_.InitFromVirtualPath(kDamageTextFontPath, pixel_height)) {
    font_pixel_height_ = pixel_height;
    return true;
  }
  if (!text_renderer_.Init(pixel_height)) {
    font_pixel_height_ = 0;
    return false;
  }
  font_pixel_height_ = pixel_height;
  return true;
}

bool FloatingTextRenderer::WorldToScreen(float wx, float wy, float wz,
                                          const float* view_mtx,
                                          const float* proj_mtx,
                                          const WorldOverlayMetrics& metrics,
                                          float& sx, float& sy) {
  if (view_mtx == nullptr || proj_mtx == nullptr) {
    return false;
  }

  const RenderVec4 world{wx, wy, wz, 1.0f};
  const RenderVec4 view = TransformRowVector4x4(
      world, RenderMatrix4x4View{view_mtx, 16u});
  const RenderVec4 clip = TransformRowVector4x4(
      view, RenderMatrix4x4View{proj_mtx, 16u});
  const float px = clip[0];
  const float py = clip[1];
  const float pw = clip[3];

  if (pw <= 0.0f) return false;

  const float inv_w = 1.0f / pw;
  const float ndc_x = px * inv_w;
  const float ndc_y = py * inv_w;

  const auto screen = metrics.NdcToFramebuffer(ndc_x, ndc_y);
  sx = screen.x;
  sy = screen.y;

  return true;
}

}
