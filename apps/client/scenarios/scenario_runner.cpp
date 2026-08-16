#include "scenario_runner.h"

#include "openwow/ui/glue/glue_charselect_scene.h"
#include "openwow/ui/game/cvar_system.h"
#include <chrono>
#include <cstdint>
#include <ctime>
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <limits>
#include <unordered_set>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace openwow::client {

using openwow::text::ToLowerAscii;

namespace {

std::string JsonEscape(std::string_view value) {
  constexpr char kHex[] = "0123456789abcdef";
  std::string escaped;
  escaped.reserve(value.size());
  for (const unsigned char c : value) {
    switch (c) {
      case '"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\b': escaped += "\\b"; break;
      case '\f': escaped += "\\f"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (c < 0x20) {
          escaped += "\\u00";
          escaped += kHex[c >> 4];
          escaped += kHex[c & 0x0f];
        } else {
          escaped += static_cast<char>(c);
        }
        break;
    }
  }
  return escaped;
}

std::optional<openwow::ui::glue::GlueWidgetState> FirstPresentWidget(
    const openwow::ui::glue::GlueWidgetRuntime& runtime,
    std::initializer_list<const char*> names) {
  for (const auto* n : names) {
    if (!n) continue;
    const auto w = runtime.GetWidget(n);
    if (w.has_value()) {
      return w;
    }
  }
  return std::nullopt;
}

std::string DescribeLiveE2eCreateFields(
    const detail::LiveE2eCreateFields& fields) {
  return "race=" + std::to_string(fields.race) +
         " class=" + std::to_string(fields.char_class) +
         " gender=" + std::to_string(fields.gender) +
         " skin=" + std::to_string(fields.skin) +
         " face=" + std::to_string(fields.face) +
         " hair_style=" + std::to_string(fields.hair_style) +
         " hair_color=" + std::to_string(fields.hair_color) +
         " facial_hair=" + std::to_string(fields.facial_hair);
}

std::string_view RegionRoleName(
    const openwow::ui::framexml::UiFrame::RegionRole role) {
  using RegionRole = openwow::ui::framexml::UiFrame::RegionRole;
  switch (role) {
    case RegionRole::Normal: return "normal";
    case RegionRole::ButtonText: return "button-text";
    case RegionRole::EditBoxText: return "editbox-text";
    case RegionRole::MessageFontDefinition: return "message-font-definition";
    case RegionRole::EditBoxCaret: return "editbox-caret";
    case RegionRole::EditBoxHighlight: return "editbox-highlight";
  }
  return "unknown";
}

std::optional<std::string> ValidateVisibleTexture(
    const openwow::ui::glue::GlueWidgetRuntime& runtime,
    const std::string_view name) {
  const auto widget = runtime.GetResolvedWidget(std::string(name));
  if (!widget.has_value()) {
    return std::string(name) + " is missing";
  }
  if (!runtime.IsVisible(widget->name)) {
    return std::string(name) + " is not visible";
  }
  if (!openwow::text::EqualsIgnoreCaseAscii(widget->kind, "Texture")) {
    return std::string(name) + " has kind=" + widget->kind;
  }
  if (widget->texture_file.empty()) {
    return std::string(name) + " has no texture file";
  }
  if (widget->width <= 0 || widget->height <= 0) {
    return std::string(name) + " has an empty resolved rectangle";
  }
  return std::nullopt;
}

std::optional<std::string> ValidateGeneratedBackdrop(
    const openwow::ui::glue::GlueWidgetRuntime& runtime,
    const std::string_view owner, const bool require_background) {
  const std::string owner_name(owner);
  const auto frame = runtime.GetResolvedWidget(owner_name);
  if (!frame.has_value()) {
    return owner_name + " is missing";
  }
  if (!runtime.IsVisible(owner_name)) {
    return owner_name + " is not visible";
  }
  if (frame->width <= 0 || frame->height <= 0) {
    return owner_name + " has an empty resolved rectangle";
  }

  if (require_background) {
    if (const auto error =
            ValidateVisibleTexture(runtime, owner_name + ".__BackdropBackground");
        error.has_value()) {
      return error;
    }
  }
  for (const char* suffix : {
           "TopLeft", "TopRight", "BottomLeft", "BottomRight",
           "Top", "Bottom", "Left", "Right",
       }) {
    if (const auto error = ValidateVisibleTexture(
            runtime, owner_name + ".__BackdropBorder" + suffix);
        error.has_value()) {
      return error;
    }
  }
  return std::nullopt;
}

std::optional<std::string> ValidateNamedFrameEdges(
    const openwow::ui::glue::GlueWidgetRuntime& runtime,
    const std::string_view owner) {
  const std::string owner_name(owner);
  for (const char* suffix : {
           "TopLeft", "TopRight", "BottomLeft", "BottomRight",
           "Top", "Bottom", "Left", "Right",
       }) {
    if (const auto error =
            ValidateVisibleTexture(runtime, owner_name + suffix);
        error.has_value()) {
      return error;
    }
  }
  return std::nullopt;
}

std::optional<std::string> ValidateEditBoxPresentation(
    const openwow::ui::glue::GlueWidgetRuntime& runtime,
    const std::string_view owner, const std::string_view expected_text,
    const bool expect_password) {
  using RegionRole = openwow::ui::framexml::UiFrame::RegionRole;
  const std::string owner_name(owner);
  const auto editbox = runtime.GetResolvedWidget(owner_name);
  if (!editbox.has_value()) {
    return owner_name + " is missing";
  }
  if (!runtime.IsVisible(owner_name)) {
    return owner_name + " is not visible";
  }
  if (!openwow::text::EqualsIgnoreCaseAscii(editbox->kind, "EditBox")) {
    return owner_name + " has kind=" + editbox->kind;
  }
  if (editbox->password != expect_password) {
    return owner_name + " has the wrong password mode";
  }

  const std::string text_region_name = runtime.TextRegionForWidget(owner_name);
  const auto text_region = runtime.GetResolvedWidget(text_region_name);
  if (!text_region.has_value() || !runtime.IsVisible(text_region_name)) {
    return owner_name + " has no visible constructor-owned text renderer";
  }
  if (text_region->region_role != RegionRole::EditBoxText ||
      text_region->publish_to_lua) {
    return text_region_name + " has the wrong native region ownership";
  }

  std::size_t native_text_regions = 0;
  bool has_separate_label = false;
  for (const auto& name : runtime.WidgetNames()) {
    const auto widget = runtime.GetWidget(name);
    if (widget.has_value() && widget->parent == owner_name &&
        widget->region_role == RegionRole::EditBoxText) {
      ++native_text_regions;
    }
    if (widget.has_value() && widget->parent == owner_name &&
        widget->region_role == RegionRole::Normal &&
        openwow::text::EqualsIgnoreCaseAscii(widget->kind, "FontString") &&
        !widget->text.empty()) {
      has_separate_label = true;
    }
  }
  if (native_text_regions != 1u) {
    return owner_name + " has " + std::to_string(native_text_regions) +
           " native text renderers instead of one";
  }
  if (!has_separate_label) {
    return owner_name + " has no separate stock label FontString";
  }
  const std::string expected_render_text =
      expect_password ? std::string(expected_text.size(), '*')
                      : std::string(expected_text);
  if (editbox->text != expected_text ||
      text_region->text != expected_render_text) {
    return owner_name + " text was not synchronized with its native renderer";
  }
  if (text_region->font_style.empty()) {
    return text_region_name + " has no font";
  }
  if (text_region->color_r != 1.0F || text_region->color_g != 1.0F ||
      text_region->color_b != 1.0F || text_region->color_a != 1.0F) {
    return text_region_name + " is not stock GlueEditBoxFont white";
  }
  if (expect_password &&
      (openwow::text::EqualsIgnoreCaseAscii(text_region->justify_h, "CENTER") ||
       openwow::text::EqualsIgnoreCaseAscii(text_region->justify_h, "RIGHT"))) {
    return text_region_name + " is not left-aligned";
  }
  return std::nullopt;
}

std::optional<std::string> ValidateSliderPresentation(
    openwow::ui::glue::GlueWidgetRuntime* runtime,
    const std::string_view slider_name,
    const std::string_view thumb_name) {
  if (runtime == nullptr) {
    return "widget runtime is unavailable";
  }
  const std::string slider_key(slider_name);
  const std::string thumb_key(thumb_name);
  const auto slider = runtime->GetResolvedWidget(slider_key);
  if (!slider.has_value() || !runtime->IsVisible(slider_key)) {
    return slider_key + " is missing or hidden";
  }
  if (!openwow::text::EqualsIgnoreCaseAscii(slider->kind, "Slider")) {
    return slider_key + " has kind=" + slider->kind;
  }
  if (const auto error = ValidateVisibleTexture(*runtime, thumb_key);
      error.has_value()) {
    return error;
  }

  const auto [minimum, maximum] = runtime->GetMinMaxValues(slider_key);
  if (!(maximum > minimum)) {
    return slider_key + " has no usable value range";
  }
  const double original = runtime->GetValue(slider_key);
  const bool layout_was_dirty = runtime->IsLayoutDirty();
  runtime->SetValue(slider_key, minimum);
  const auto minimum_thumb = runtime->GetResolvedWidget(thumb_key);
  runtime->SetValue(slider_key, maximum);
  const auto maximum_thumb = runtime->GetResolvedWidget(thumb_key);
  runtime->SetValue(slider_key, original);
  if (!minimum_thumb.has_value() || !maximum_thumb.has_value()) {
    return slider_key + " could not resolve its live thumb";
  }
  if (runtime->IsLayoutDirty() != layout_was_dirty) {
    return slider_key + " dirtied the full layout while changing value";
  }

  const bool vertical = slider->height > slider->width;
  const bool thumb_moved = vertical
                               ? maximum_thumb->y > minimum_thumb->y
                               : maximum_thumb->x > minimum_thumb->x;
  if (!thumb_moved) {
    return slider_key + " thumb does not follow its live value";
  }
  return std::nullopt;
}

std::optional<std::string> ValidateContainedText(
    const openwow::ui::glue::GlueWidgetRuntime& runtime,
    const std::string_view container_name,
    const std::string_view text_name) {
  const auto container = runtime.GetResolvedWidget(std::string(container_name));
  const auto text = runtime.GetResolvedWidget(std::string(text_name));
  if (!container.has_value() || !runtime.IsVisible(container->name)) {
    return std::string(container_name) + " is missing or hidden";
  }
  if (!text.has_value() || !runtime.IsVisible(text->name)) {
    return std::string(text_name) + " is missing or hidden";
  }
  if (text->text.empty()) {
    return std::string(text_name) + " has no content";
  }
  if (text->font_style.empty()) {
    return std::string(text_name) + " has no font";
  }
  if (text->x < container->x || text->y < container->y ||
      text->x + text->width > container->x + container->width ||
      text->y + text->height > container->y + container->height) {
    return std::string(text_name) + " overflows " +
           std::string(container_name);
  }
  return std::nullopt;
}

}

ScenarioRunner::ScenarioRunner(ScenarioOptions options)
    : options_(std::move(options)) {}

std::filesystem::path ScenarioRunner::CaptureFramePath(
    const std::uint32_t now_ms) const {
  return options_.artifacts_dir / options_.name /
         ("frame_" + std::to_string(now_ms) + ".bmp");
}

std::filesystem::path ScenarioRunner::WorldOracleReportPath() const {
  return options_.artifacts_dir / options_.name / "world_oracle.json";
}

void ScenarioRunner::MarkWorldMilestone(std::string name,
                                        const std::uint32_t elapsed_ms) {
  const auto found = std::find_if(
      e2e_world_report_.milestones.begin(),
      e2e_world_report_.milestones.end(),
      [&name](const detail::LiveE2eWorldMilestone& milestone) {
        return milestone.name == name;
      });
  if (found == e2e_world_report_.milestones.end()) {
    e2e_world_report_.milestones.push_back(
        {.name = std::move(name), .elapsed_ms = elapsed_ms});
  }
}

void ScenarioRunner::RecordWorldSemanticSample(
    const std::uint32_t elapsed_ms, const ScenarioPlayState& state) {
  e2e_world_report_.semantic_samples.push_back({
      .elapsed_ms = elapsed_ms,
      .frame_generation = state.frame_generation,
      .final_backbuffer_ready = state.final_backbuffer_ready,
      .loading_screen_visible = state.loading_screen_visible,
      .loading_screen_sole_owner = state.loading_screen_sole_owner,
      .loading_final_backbuffer_ready =
          state.loading_final_backbuffer_ready,
      .loading_render_submissions = state.loading_render_submissions,
      .loading_self_presented_frames =
          state.loading_self_presented_frames,
      .loading_coalesced_callbacks = state.loading_coalesced_callbacks,
      .player_render_ready = detail::HasPlayerRenderProof(state),
      .world_ui_ready = state.world_ui_regions_ready &&
                        state.world_ui_anchors_valid &&
                        state.world_ui_text_contained,
      .player_frame_ready = state.world_ui_player_frame_ready,
      .portrait_ready = state.world_ui_player_portrait_ready,
      .health_power_ready = state.world_ui_health_power_ready,
      .action_bar_ready = state.world_ui_action_icon_ready,
      .chat_ready = state.world_ui_chat_ready,
      .minimap_ready = state.world_ui_minimap_ready,
      .world_map_ready = state.world_ui_world_map_ready,
      .world_map_visible = state.world_ui_world_map_visible,
      .character_panel_ready = state.world_ui_character_panel_ready,
      .character_model_ready = state.world_ui_character_model_ready,
      .character_identity_ready = state.world_ui_character_identity_ready,
      .character_panel_visible = state.world_ui_character_panel_visible,
      .nameplates_ready = state.nameplate_pipeline_ready &&
                          state.visible_nameplates > 0u,
      .visible_nameplates = state.visible_nameplates,
      .terrain_tiles_loaded = state.terrain_tiles_loaded,
      .object_instances = state.object_instances,
      .ui_traversal_entries = state.ui_traversal_entries,
      .ui_render_candidates = state.ui_render_candidates,
      .render_draw_calls = state.render_draw_calls,
      .render_cpu_time_ms = state.render_cpu_time_ms,
      .render_gpu_time_ms = state.render_gpu_time_ms,
  });
}

void ScenarioRunner::FlushWorldOracleReport(const bool completed,
                                            const bool passed,
                                            std::string failure) {
  e2e_world_report_.completed = completed;
  e2e_world_report_.passed = passed;
  e2e_world_report_.failure = std::move(failure);
  if (!detail::WriteLiveE2eWorldOracleReport(WorldOracleReportPath(),
                                             e2e_world_report_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "Scenario world oracle report write failed");
  }
}

bool ScenarioRunner::CaptureFrame(std::uint32_t now_ms, const ScenarioContext& ctx) const {
  if (!ctx.request_screenshot) {
    return false;
  }
  std::filesystem::path out_dir = options_.artifacts_dir / options_.name;
  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);
  const std::filesystem::path path = CaptureFramePath(now_ms);

  ec.clear();
  (void)std::filesystem::remove(path, ec);
  if (!ctx.request_screenshot(path)) {
    return false;
  }
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "Scenario capture requested: path=" + path.string());
  return true;
}

bool ScenarioRunner::DumpUiTree(std::uint32_t now_ms, const ScenarioContext& ctx) const {
  if ((!ctx.in_world || !ctx.dump_world_ui_json) &&
      ctx.glue_widgets == nullptr) {
    return false;
  }

  std::filesystem::path out_dir = options_.artifacts_dir / options_.name;
  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);
  const std::filesystem::path path =
      out_dir / ("ui_" + std::to_string(now_ms) + ".json");

  std::ofstream out(path);
  if (!out) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "Scenario UI dump failed to open: " + path.string());
    return false;
  }

  if (ctx.in_world && ctx.dump_world_ui_json) {
    out << ctx.dump_world_ui_json(now_ms, ctx.viewport_width,
                                  ctx.viewport_height);
    return out.good();
  }

  const auto& widgets = ctx.glue_widgets->VisibleWidgetsInRenderOrder();
  out << "{\n";
  out << "  \"now_ms\": " << now_ms << ",\n";
  out << "  \"viewport\": {\"w\": " << ctx.viewport_width << ", \"h\": " << ctx.viewport_height << "},\n";
  if (ctx.game_state != nullptr &&
      ctx.game_state->char_customize_scene != nullptr) {
    out << "  \"characterCreate\": {\"race\": "
        << ctx.game_state->create_race << ", \"class\": "
        << ctx.game_state->create_class << ", \"sex\": "
        << ctx.game_state->create_sex << ", \"model\": \""
        << JsonEscape(ctx.game_state->char_customize_scene
                          ->selected_character_model_path())
        << "\"},\n";
  }
  out << "  \"widgets\": [\n";
  for (std::size_t i = 0; i < widgets.size(); ++i) {
    const auto& w = widgets[i];
    const bool is_visible = ctx.glue_widgets->IsVisible(w.name);
    out << "    {";
    out << "\"name\": \"" << JsonEscape(w.name) << "\"";
    out << ", \"kind\": \"" << JsonEscape(w.kind) << "\"";
    out << ", \"parent\": \"" << JsonEscape(w.parent) << "\"";
    out << ", \"x\": " << w.x << ", \"y\": " << w.y;
    out << ", \"w\": " << w.width << ", \"h\": " << w.height;
    out << ", \"layer\": \"" << JsonEscape(w.draw_layer) << "\"";
    out << ", \"sub\": " << w.draw_sublevel;
    out << ", \"strata\": \"" << JsonEscape(w.frame_strata) << "\"";
    out << ", \"level\": " << w.frame_level;
    out << ", \"texture\": \"" << JsonEscape(w.texture_file) << "\"";
    out << ", \"alphaMode\": \"" << JsonEscape(w.alpha_mode) << "\"";
    out << ", \"font\": \"" << JsonEscape(w.font_style) << "\"";
    out << ", \"justifyH\": \"" << JsonEscape(w.justify_h) << "\"";
    out << ", \"justifyV\": \"" << JsonEscape(w.justify_v) << "\"";
    out << ", \"color\": [" << w.color_r << ", " << w.color_g << ", "
        << w.color_b << ", " << w.color_a << "]";
    out << ", \"password\": " << (w.password ? "true" : "false");
    out << ", \"publishToLua\": "
        << (w.publish_to_lua ? "true" : "false");
    out << ", \"regionRole\": \"" << RegionRoleName(w.region_role) << "\"";
    out << ", \"hasBackdrop\": " << (w.backdrop.has_value() ? "true" : "false");
    out << ", \"text\": \"" << JsonEscape(w.text) << "\"";
    out << ", \"visible\": " << (is_visible ? "true" : "false");
    const auto presentation =
        ctx.glue_widgets->ResolveScrollPresentation(w);
    if (w.scroll_child_content) {
      out << ", \"scrollChild\": true";
      out << ", \"presentedX\": " << presentation.widget.x;
      out << ", \"presentedY\": " << presentation.widget.y;
      if (presentation.clip.has_value()) {
        out << ", \"clip\": {\"x\": " << presentation.clip->x
            << ", \"y\": " << presentation.clip->y
            << ", \"w\": " << presentation.clip->width
            << ", \"h\": " << presentation.clip->height << "}";
      }
    }
    if (openwow::text::EqualsIgnoreCaseAscii(w.kind, "ScrollFrame")) {
      out << ", \"horizontalScroll\": "
          << ctx.glue_widgets->GetHorizontalScroll(w.name);
      out << ", \"horizontalRange\": "
          << ctx.glue_widgets->GetHorizontalScrollRange(w.name);
      out << ", \"verticalScroll\": "
          << ctx.glue_widgets->GetVerticalScroll(w.name);
      out << ", \"verticalRange\": "
          << ctx.glue_widgets->GetVerticalScrollRange(w.name);
    }
    if (openwow::text::EqualsIgnoreCaseAscii(w.kind, "Slider")) {
      const auto [minimum, maximum] =
          ctx.glue_widgets->GetMinMaxValues(w.name);
      out << ", \"minimum\": " << minimum;
      out << ", \"maximum\": " << maximum;
      out << ", \"value\": " << ctx.glue_widgets->GetValue(w.name);
    }
    if (w.kind == "CheckButton") {
      out << ", \"checked\": "
          << (ctx.glue_widgets->Checked(w.name) ? "true" : "false");
    }
    out << "}";
    if (i + 1 != widgets.size()) out << ",";
    out << "\n";
  }
  out << "  ]\n";
  out << "}\n";
  return true;
}

namespace {

[[nodiscard]] double MainThreadCpuMs() noexcept {
#if defined(_WIN32)
  FILETIME creation_time{};
  FILETIME exit_time{};
  FILETIME kernel_time{};
  FILETIME user_time{};
  if (::GetThreadTimes(::GetCurrentThread(), &creation_time, &exit_time,
                       &kernel_time, &user_time) == FALSE) {
    return 0.0;
  }

  const auto to_ticks = [](const FILETIME &value) -> std::uint64_t {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) |
           static_cast<std::uint64_t>(value.dwLowDateTime);
  };

  constexpr double kMillisecondsPerFileTimeTick = 1.0e-4;
  return static_cast<double>(to_ticks(kernel_time) + to_ticks(user_time)) *
         kMillisecondsPerFileTimeTick;
#else
  timespec ts{};
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
    return 0.0;
  }
  return static_cast<double>(ts.tv_sec) * 1000.0 +
         static_cast<double>(ts.tv_nsec) / 1.0e6;
#endif
}
}

void ScenarioRunner::ReportBenchmark() {
  if (benchmark_frame_ms_.empty()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "Benchmark: no frames timed");
    return;
  }
  std::vector<double> sorted = benchmark_frame_ms_;
  std::sort(sorted.begin(), sorted.end());
  const auto pick = [&sorted](const double q) {
    const std::size_t index = static_cast<std::size_t>(
        q * static_cast<double>(sorted.size() - 1));
    return sorted[index];
  };
  double total = 0.0;
  for (const double v : sorted) total += v;
  const double mean = total / static_cast<double>(sorted.size());
  const auto fmt = [](const double v) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", v);
    return std::string(buffer);
  };

  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "Benchmark result: frames=" + std::to_string(sorted.size()) +
          " mean_ms=" + fmt(mean) +
          " p50_ms=" + fmt(pick(0.50)) +
          " p95_ms=" + fmt(pick(0.95)) +
          " p99_ms=" + fmt(pick(0.99)) +
          " min_ms=" + fmt(sorted.front()) +
          " max_ms=" + fmt(sorted.back()) +
          " mean_fps=" + fmt(mean > 0.0 ? 1000.0 / mean : 0.0) +
          " p50_fps=" + fmt(pick(0.50) > 0.0 ? 1000.0 / pick(0.50) : 0.0));

  std::vector<double> cpu = benchmark_cpu_ms_;
  std::sort(cpu.begin(), cpu.end());
  if (!cpu.empty()) {
    double cpu_total = 0.0;
    for (const double v : cpu) cpu_total += v;
    const double cpu_mean = cpu_total / static_cast<double>(cpu.size());
    const auto cpu_pick = [&cpu](const double q) {
      return cpu[static_cast<std::size_t>(q * static_cast<double>(cpu.size() - 1))];
    };
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kInfo,
        "Benchmark main-thread CPU: mean_ms=" + fmt(cpu_mean) +
            " p50_ms=" + fmt(cpu_pick(0.50)) +
            " p95_ms=" + fmt(cpu_pick(0.95)) +
            " p99_ms=" + fmt(cpu_pick(0.99)) +
            " implied_cpu_ceiling_fps=" +
            fmt(cpu_mean > 0.0 ? 1000.0 / cpu_mean : 0.0));
  }
}

bool ScenarioRunner::Tick(Stage stage, std::uint32_t now_ms, ScenarioContext* ctx) {
  if (ctx == nullptr) {
    return false;
  }
  if (options_.name.empty()) {
    return true;
  }
  if (!start_ms_.has_value()) {
    start_ms_ = now_ms;
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "Scenario started: name=" + options_.name
                           + " artifacts=" + options_.artifacts_dir.string());
  }

  const std::uint32_t elapsed = now_ms - *start_ms_;

  const std::string scenario = ToLowerAscii(options_.name);
  if (scenario != "glue_login_typing_and_click") {
    if (scenario != "glue_login_idle_30s" &&
        scenario != "glue_ui_regression" &&
        scenario != "glue_character_create_regression" &&
        scenario != "glue_delete_dialog_regression" &&
        scenario != "glue_nightelf_lighting_regression" &&
        scenario != "glue_live_e2e" &&
        scenario != "world_offline_play_regression") {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "Unknown scenario: " + options_.name);
      return false;
    }
  }

  if (ctx->glue_widgets == nullptr || ctx->glue_runtime == nullptr) {
    return true;
  }

  if (stage == Stage::kPostRender) {

    if (scenario == "glue_live_e2e" && step_ == 6 && !pending_capture_ &&
        !e2e_world_frame_pending_validation_.has_value() &&
        ctx->query_play_state &&
        std::none_of(
            e2e_world_report_.captures.begin(),
            e2e_world_report_.captures.end(), [](const auto& record) {
              return record.purpose ==
                         detail::LiveE2eCapturePurpose::kLoadingScreen &&
                     record.validation.status ==
                         detail::LiveE2eFrameStatus::kPlayable;
            })) {
      const ScenarioPlayState loading = ctx->query_play_state();
      if (detail::HasLoadingCompositorCaptureProof(loading)) {
        RecordWorldSemanticSample(elapsed, loading);
        MarkWorldMilestone("loading_screen_semantic_ready", elapsed);
        e2e_next_capture_purpose_ =
            detail::LiveE2eCapturePurpose::kLoadingScreen;
        e2e_validate_next_capture_ = true;
        pending_capture_ = true;
      }
    }
    if (pending_capture_) {
      std::optional<ScenarioPlayState> capture_state;
      const bool needs_final_oracle_frame =
          (scenario == "glue_live_e2e" ||
           scenario == "world_offline_play_regression") &&
          e2e_validate_next_capture_;
      const detail::LiveE2eCapturePurpose capture_purpose =
          e2e_next_capture_purpose_.value_or(
              detail::LiveE2eCapturePurpose::kGameplayBaseline);
      if (needs_final_oracle_frame) {
        if (!ctx->query_play_state) {
          return true;
        }
        capture_state = ctx->query_play_state();
        const bool compositor_ready =
            capture_purpose == detail::LiveE2eCapturePurpose::kLoadingScreen
                ? detail::HasLoadingCompositorCaptureProof(*capture_state)
                : ctx->in_world &&
                      detail::HasFinalCompositorCaptureProof(*capture_state);

        if (!compositor_ready) {
          return true;
        }
      }
      if (CaptureFrame(now_ms, *ctx)) {
        if (needs_final_oracle_frame) {
          e2e_world_frame_pending_validation_ = CaptureFramePath(now_ms);
          e2e_pending_capture_purpose_ = capture_purpose;
          e2e_pending_capture_generation_ = capture_state->frame_generation;
          e2e_next_capture_purpose_.reset();
          e2e_validate_next_capture_ = false;
        }
        DumpUiTree(now_ms, *ctx);
        ++captures_;
        pending_capture_ = false;
      }
    }
    if (should_exit_ && !pending_capture_) {

      if (options_.benchmark_frames > 0) {
        const auto now = std::chrono::steady_clock::now();
        if (!benchmark_active_) {
          benchmark_active_ = true;
          benchmark_last_ = now;
          benchmark_last_cpu_ms_ = MainThreadCpuMs();
          benchmark_cpu_ms_.reserve(
              static_cast<std::size_t>(options_.benchmark_frames));
          benchmark_frame_ms_.reserve(
              static_cast<std::size_t>(options_.benchmark_frames));
          openwow::diagnostics::Log(
              openwow::diagnostics::LogLevel::kInfo,
              "Benchmark: warm state reached, timing " +
                  std::to_string(options_.benchmark_frames) + " frames");
          return true;
        }
        const double frame_ms =
            std::chrono::duration<double, std::milli>(now - benchmark_last_).count();
        benchmark_last_ = now;
        const double cpu_now = MainThreadCpuMs();
        const double cpu_ms = cpu_now - benchmark_last_cpu_ms_;
        benchmark_last_cpu_ms_ = cpu_now;

        if (benchmark_frames_seen_ > 0) {
          benchmark_frame_ms_.push_back(frame_ms);
          benchmark_cpu_ms_.push_back(cpu_ms);
        }
        ++benchmark_frames_seen_;
        if (benchmark_frames_seen_ <= options_.benchmark_frames) {
          return true;
        }
        ReportBenchmark();
      }
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                         "Scenario completed: name=" + options_.name);
      return false;
    }
    return true;
  }

  if (pending_capture_) {
    return true;
  }

  auto dispatch = [&](const std::string& widget_name,
                      const std::string& event_name,
                      const std::vector<openwow::ui::glue::GlueLuaValue>& args) {
    if (ctx->glue_runtime == nullptr) {
      return;
    }
    (void)ctx->glue_runtime->RunWidgetEvent(widget_name,
                                           event_name,
                                           widget_name + "." + event_name,
                                           args);
  };

  auto make_lua_string = [](std::string value) {
    openwow::ui::glue::GlueLuaValue v;
    v.kind = openwow::ui::glue::GlueLuaValue::Kind::kString;
    v.string_value = std::move(value);
    return v;
  };

  auto request_capture = [&]() {
    if (captures_ >= 32) {
      return;
    }
    pending_capture_ = true;
  };

  auto run_lua = [&](const std::string& script, const std::string& source) {
    const auto result = ctx->glue_runtime->ExecuteString(script, source);
    if (!result.ok) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "Scenario Lua failed: source=" + source +
                             " error=" + result.error);
    }
    return result.ok;
  };

  if (scenario == "world_offline_play_regression") {
    const auto describe_play_state = [](const ScenarioPlayState& state) {
      return "ready=" + std::to_string(state.ready) +
             " connected=" + std::to_string(state.connected) +
             " mover_present=" + std::to_string(state.mover_guid != 0u) +
             " tiles=" + std::to_string(state.terrain_tiles_loaded) +
             " objects=" + std::to_string(state.object_instances) +
             " player_render=" + std::to_string(state.player_render_ready) +
             " player_draw=" +
             std::to_string(state.player_visible_draw_submitted) +
             " camera_distance=" +
             std::to_string(state.camera_resolved_distance) + "/" +
             std::to_string(state.camera_desired_distance) +
             " player_alpha=" +
             std::to_string(state.player_camera_alpha_visible) +
             " ui_loaded=" + std::to_string(state.game_ui_loaded) +
             " ui_frames=" + std::to_string(state.game_ui_frames) +
             " ui_candidates=" +
             std::to_string(state.ui_render_candidates) + "/" +
             std::to_string(state.ui_traversal_entries) +
             " main_menu=" + std::to_string(state.main_menu_visible) +
             " world_ui=" + std::to_string(state.world_ui_regions_ready) +
             " ui_anchors=" +
             std::to_string(state.world_ui_anchors_valid) +
             " ui_text=" +
             std::to_string(state.world_ui_text_contained) +
             " portrait=" +
             std::to_string(state.world_ui_player_portrait_ready) +
             " health_power=" +
             std::to_string(state.world_ui_health_power_ready) +
             " unit_frames=" +
             std::to_string(state.world_ui_unit_frames_ready) +
             " action_icon=" +
             std::to_string(state.world_ui_action_icon_ready) +
             " chat=" + std::to_string(state.world_ui_chat_ready) +
             " minimap=" + std::to_string(state.world_ui_minimap_ready) +
             " draws=" + std::to_string(state.render_draw_calls) +
             " render_ms=" + std::to_string(state.render_cpu_time_ms) + "/" +
             std::to_string(state.render_gpu_time_ms) +
             " forward=" + std::to_string(state.forward_active) +
             " starts=" + std::to_string(state.forward_start_packets_sent) +
             " heartbeats=" +
             std::to_string(state.movement_heartbeat_packets_sent) +
             " stops=" + std::to_string(state.movement_stop_packets_sent);
    };
    const auto current_state = [&]() {
      return ctx->query_play_state ? ctx->query_play_state()
                                   : ScenarioPlayState{};
    };
    const auto fail_offline = [&](const std::string& reason) {
      if (e2e_forward_binding_held_ && ctx->control_forward_movement) {
        (void)ctx->control_forward_movement(
            ScenarioForwardMovementCommand::kStop);
        e2e_forward_binding_held_ = false;
      }
      const auto state = current_state();
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kError,
          "Offline world regression failed: " + reason + " (" +
              describe_play_state(state) + ")");
      failed_ = true;
      request_capture();
      should_exit_ = true;
    };
    const auto advance_step = [&](const int next_step) {
      step_ = next_step;
      e2e_step_started_ms_ = now_ms;
    };

    if (!e2e_step_started_ms_.has_value()) {
      e2e_step_started_ms_ = now_ms;
    }
    if (elapsed >= 90000u) {
      fail_offline("global 90 second timeout");
      return true;
    }

    if (e2e_world_frame_pending_validation_.has_value()) {
      const auto validation = detail::ValidateLiveE2eWorldFrame(
          *e2e_world_frame_pending_validation_);
      if (validation.status == detail::LiveE2eFrameStatus::kPending) {
        return true;
      }
      e2e_world_frame_pending_validation_.reset();
      if (validation.status != detail::LiveE2eFrameStatus::kPlayable) {
        e2e_world_visual_failure_ =
            "rendered frame is not playable: " + validation.reason +
            " colors=" + std::to_string(validation.quantized_color_count) +
            " luma=" + std::to_string(validation.minimum_luma) + ".." +
            std::to_string(validation.maximum_luma);
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kWarn,
            "Offline world regression deferring visual failure until after "
            "movement proof: " + *e2e_world_visual_failure_);
      } else {
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kInfo,
            "Offline world regression proved non-degenerate rendering colors=" +
                std::to_string(validation.quantized_color_count) + " luma=" +
                std::to_string(validation.minimum_luma) + ".." +
                std::to_string(validation.maximum_luma));
      }
      e2e_world_visual_validated_ = true;
    }

    if (step_ == 0) {
      if (!ctx->enter_offline_world || !ctx->enter_offline_world()) {
        fail_offline("could not create the deterministic local world snapshot");
        return true;
      }
      advance_step(1);
      return true;
    }

    if (step_ == 1) {
      const auto state = current_state();
      if (now_ms - *e2e_step_started_ms_ >= 60000u) {
        fail_offline("world assets did not become render-ready within 60 seconds");
        return true;
      }
      if (!ctx->in_world || !state.ready || !state.connected ||
          state.terrain_tiles_loaded == 0u || state.object_instances == 0u ||
          !detail::HasPlayerRenderProof(state) || !state.game_ui_loaded ||
          state.game_ui_frames == 0u) {
        return true;
      }
      if (!e2e_offline_chat_probe_generation_.has_value()) {
        if (!ctx->exercise_world_ui) {
          fail_offline("stock world-UI interaction callback is unavailable");
          return true;
        }
        const auto chat = ctx->exercise_world_ui(
            ScenarioWorldUiAction::kInjectChatProbe);
        if (!chat.handled || !chat.state_changed) {
          fail_offline(chat.error.empty()
                           ? "stock ChatFrame interaction failed"
                           : chat.error);
          return true;
        }
        e2e_offline_chat_probe_generation_ = state.frame_generation;
        return true;
      }
      if (!detail::HasNewerWorldUiSubmission(
              state, *e2e_offline_chat_probe_generation_) ||
          !detail::HasWorldUiRenderProof(state)) {
        return true;
      }
      e2e_play_baseline_ = state;
      e2e_validate_next_capture_ = true;
      request_capture();
      advance_step(2);
      return true;
    }

    if (step_ == 2) {
      if (!e2e_world_visual_validated_) {
        return true;
      }
      if (!e2e_play_baseline_.has_value() ||
          !ctx->control_forward_movement ||
          !ctx->control_forward_movement(
              ScenarioForwardMovementCommand::kStart)) {
        fail_offline("stock MOVEFORWARD key-down was not handled");
        return true;
      }
      e2e_forward_binding_held_ = true;
      advance_step(3);
      return true;
    }

    if (step_ == 3) {
      if (!e2e_play_baseline_.has_value() ||
          !ctx->control_forward_movement) {
        fail_offline("lost movement-start proof state");
        return true;
      }
      const auto moving = current_state();
      if (!detail::HasForwardStartProof(*e2e_play_baseline_, moving)) {
        return true;
      }
      e2e_play_moving_ = moving;
      if (now_ms - *e2e_step_started_ms_ < 650u ||
          !detail::HasSustainedForwardProof(*e2e_play_baseline_, moving)) {
        return true;
      }
      if (!ctx->control_forward_movement(
              ScenarioForwardMovementCommand::kStop)) {
        fail_offline("stock MOVEFORWARD key-up was not handled");
        return true;
      }
      e2e_forward_binding_held_ = false;
      advance_step(4);
      return true;
    }

    if (step_ == 4) {
      if (!e2e_play_moving_.has_value()) {
        fail_offline("lost movement-stop proof state");
        return true;
      }
      const auto stopped = current_state();
      if (!detail::HasForwardStopProof(*e2e_play_moving_, stopped)) {
        return true;
      }
      if (e2e_world_visual_failure_.has_value()) {
        fail_offline(*e2e_world_visual_failure_ +
                     "; movement start/heartbeat/stop proof passed");
        return true;
      }
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kInfo,
          "Offline world regression proved terrain/player rendering plus "
          "start, heartbeat, and stop movement packets (" +
              describe_play_state(stopped) + ")");
      request_capture();
      should_exit_ = true;
      advance_step(5);
      return true;
    }

    return true;
  }

  if (scenario == "glue_live_e2e") {
    const auto scrub_live_password = [&]() {
      if (ctx->clear_login_password) {
        ctx->clear_login_password();
      } else {
        ctx->glue_widgets->SetText("AccountLoginPasswordEdit", "");
      }
      openwow::ui::glue::GlueGameState::SecureClearString(options_.password);
    };
    const auto request_live_capture = [&]() {
      scrub_live_password();

      if (ctx->set_login_credentials) {
        ctx->set_login_credentials("", "");
      } else {
        ctx->glue_widgets->SetText("AccountLoginAccountEdit", "");
      }
      openwow::ui::glue::GlueGameState::SecureClearString(options_.account);
      request_capture();
    };

    if (step_ == 0 && (options_.account.empty() || options_.password.empty())) {
      scrub_live_password();
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                         "Live E2E scenario requires in-memory account credentials");
      failed_ = true;
      return false;
    }
    if (ctx->game_state == nullptr) {
      return true;
    }

    auto live_state = [&]() {
      std::string state =
          "step=" + std::to_string(step_) +
          " phase=" + std::to_string(ctx->flow_phase) +
          " screen=" + ctx->game_state->current_screen +
          " realms=" + std::to_string(ctx->game_state->realms.size()) +
          " selected_realm=" +
          std::to_string(ctx->game_state->selected_realm_index) +
          " characters=" +
          std::to_string(ctx->game_state->characters.size()) +
          (ctx->flow_status_text.empty()
               ? std::string{}
               : " status=\"" + ctx->flow_status_text + "\"");
      if (ctx->query_play_state) {
        const ScenarioPlayState play = ctx->query_play_state();
        state += " play_ready=" + std::to_string(play.ready) +
                 " connected=" + std::to_string(play.connected) +
                 " cinematic_playing=" +
                 std::to_string(play.cinematic_playing) +
                 " cinematic_presenting=" +
                 std::to_string(play.cinematic_presenting) +
                 " forward_binding=" +
                 std::to_string(play.forward_binding_available) +
                 " mover_present=" +
                 std::to_string(play.mover_guid != 0u) +
                 " player_render=" +
                 std::to_string(play.player_render_ready) +
                 " player_draw=" +
                 std::to_string(play.player_visible_draw_submitted) +
                 " camera_distance=" +
                 std::to_string(play.camera_resolved_distance) + "/" +
                 std::to_string(play.camera_desired_distance) +
                 " player_alpha=" +
                 std::to_string(play.player_camera_alpha_visible) +
                 " unit_frames=" +
                 std::to_string(play.world_ui_unit_frames_ready) +
                 " chat=" + std::to_string(play.world_ui_chat_ready) +
                 " minimap=" +
                 std::to_string(play.world_ui_minimap_ready) +
                 " character_model=" +
                 std::to_string(play.world_ui_character_model_ready) +
                 " character_identity=" +
                 std::to_string(play.world_ui_character_identity_ready) +
                 " character_name_lengths=" +
                 std::to_string(
                     play.world_ui_character_runtime_name_length) + "/" +
                 std::to_string(
                     play.world_ui_character_expected_name_length) +
                 " nameplates=" +
                 std::to_string(play.visible_nameplates) +
                 " loading_visible=" +
                 std::to_string(play.loading_screen_visible) +
                 " loading_owner=" +
                 std::to_string(play.loading_screen_sole_owner) +
                 " loading_final=" +
                 std::to_string(play.loading_final_backbuffer_ready) +
                 " final_frame=" +
                 std::to_string(play.final_backbuffer_ready) +
                 " forward=" + std::to_string(play.forward_active) +
                 " start_packets=" +
                 std::to_string(play.forward_start_packets_sent) +
                 " heartbeat_packets=" +
                 std::to_string(play.movement_heartbeat_packets_sent) +
                 " stop_packets=" +
                 std::to_string(play.movement_stop_packets_sent);
      }
      return state;
    };
    auto fail_live = [&](const std::string& reason) {
      if (e2e_forward_binding_held_ && ctx->control_forward_movement) {
        (void)ctx->control_forward_movement(
            ScenarioForwardMovementCommand::kStop);
        e2e_forward_binding_held_ = false;
      }
      if (ctx->in_world && ctx->exercise_world_ui) {
        (void)ctx->exercise_world_ui(
            ScenarioWorldUiAction::kRestoreTransientState);
      }
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                         "Live E2E scenario failed: " + reason + " (" +
                             live_state() + ")");
      failed_ = true;
      FlushWorldOracleReport(true, false, reason);
      request_live_capture();
      should_exit_ = true;
    };
    auto advance_step = [&](const int next_step) {
      step_ = next_step;
      e2e_step_started_ms_ = now_ms;
    };

    if (!e2e_step_started_ms_.has_value()) {
      e2e_step_started_ms_ = now_ms;
    }

    if (elapsed >= 180000) {
      fail_live("global 180 second timeout");
      return true;
    }

    const std::uint32_t step_timeout_ms = [&]() -> std::uint32_t {
      switch (step_) {
        case 1: return 10000;
        case 2: return 45000;

        case 3: return 30000;
        case 4: return 10000;
        case 5: return 25000;

        case 6: return 90000;
        case 7: return 10000;
        case 8: return 10000;
        case 9: return 10000;

        case 12: return 10000;
        case 13: return 5000;
        case 14: return 5000;
        case 15: return 5000;
        case 16: return 5000;
        case 19: return 5000;
        case 17: return 15000;

        case 18: return 35000;
        default: return 10000;
      }
    }();
    if (now_ms - *e2e_step_started_ms_ >= step_timeout_ms) {
      fail_live("step timeout after " + std::to_string(step_timeout_ms) + " ms");
      return true;
    }

    if (step_ >= 2 && ctx->show_error && ctx->flow_phase == 0) {
      fail_live("glue flow reported an error");
      return true;
    }

    if (e2e_world_frame_pending_validation_.has_value()) {
      const std::filesystem::path capture_path =
          *e2e_world_frame_pending_validation_;
      const detail::LiveE2eCapturePurpose purpose =
          e2e_pending_capture_purpose_.value_or(
              detail::LiveE2eCapturePurpose::kFailureDiagnostic);
      const std::uint64_t frame_generation =
          e2e_pending_capture_generation_.value_or(0u);
      const auto validation =
          purpose == detail::LiveE2eCapturePurpose::kLoadingScreen
              ? detail::ValidateLiveE2eLoadingFrame(capture_path)
              : detail::ValidateLiveE2eWorldFrame(capture_path);
      if (validation.status == detail::LiveE2eFrameStatus::kPending) {
        return true;
      }
      e2e_world_frame_pending_validation_.reset();
      e2e_pending_capture_purpose_.reset();
      e2e_pending_capture_generation_.reset();

      std::optional<detail::LiveE2eFrameComparison> comparison;
      if (e2e_world_baseline_frame_.has_value() &&
          purpose != detail::LiveE2eCapturePurpose::kLoadingScreen &&
          purpose != detail::LiveE2eCapturePurpose::kGameplayBaseline) {
        comparison = detail::CompareLiveE2eWorldFrames(
            *e2e_world_baseline_frame_, capture_path);
      }
      const bool generation_advanced =
          e2e_world_report_.captures.empty() ||
          frame_generation >
              e2e_world_report_.captures.back().frame_generation;
      e2e_world_report_.captures.push_back({
          .purpose = purpose,
          .filename = capture_path.string(),
          .elapsed_ms = elapsed,
          .frame_generation = frame_generation,
          .validation = validation,
          .comparison_to_baseline = comparison,
      });
      if (!generation_advanced) {
        fail_live("final-backbuffer capture generation did not advance");
        return true;
      }
      if (validation.status != detail::LiveE2eFrameStatus::kPlayable) {
        fail_live(
            std::string(purpose ==
                                detail::LiveE2eCapturePurpose::kLoadingScreen
                            ? "loading render proof failed: "
                            : "in-world render proof failed: ") +
            validation.reason +
            " colors=" + std::to_string(validation.quantized_color_count) +
            " luma=" + std::to_string(validation.minimum_luma) + ".." +
            std::to_string(validation.maximum_luma));
        return true;
      }
      if (purpose == detail::LiveE2eCapturePurpose::kGameplayBaseline) {
        e2e_world_baseline_frame_ = capture_path;
        e2e_world_visual_validated_ = true;
      } else if (purpose == detail::LiveE2eCapturePurpose::kPostMovement) {
        e2e_post_movement_frame_ = capture_path;
      } else if (purpose == detail::LiveE2eCapturePurpose::kWorldMapOpen) {
        e2e_world_map_open_frame_ = capture_path;
      }

      if (comparison.has_value() && !comparison->comparable) {
        fail_live("in-world final frames could not be compared: " +
                  comparison->reason);
        return true;
      }
      if (comparison.has_value() &&
          purpose == detail::LiveE2eCapturePurpose::kWorldUiInteractions &&
          (comparison->changed_fraction < 0.001 ||
           comparison->world_viewport_changed_fraction < 0.0005 ||
           comparison->minimap_corner_changed_fraction < 0.002 ||
           comparison->bottom_ui_changed_fraction < 0.0005 ||
           comparison->mean_absolute_channel_delta < 0.1)) {
        fail_live("chat/minimap/nameplate interactions did not reach the final compositor");
        return true;
      }
      if (comparison.has_value() &&
          (purpose == detail::LiveE2eCapturePurpose::kWorldMapOpen ||
           purpose == detail::LiveE2eCapturePurpose::kWorldMapReopened) &&
          (comparison->changed_fraction < 0.12 ||
           comparison->mean_absolute_channel_delta < 3.0)) {
        fail_live("WorldMapFrame visibility did not materially change the final compositor");
        return true;
      }
      if (purpose == detail::LiveE2eCapturePurpose::kWorldMapReopened) {
        if (!e2e_world_map_open_frame_.has_value()) {
          fail_live("reopened WorldMapFrame has no first-open reference");
          return true;
        }
        const auto reopen_comparison = detail::CompareLiveE2eWorldFrames(
            *e2e_world_map_open_frame_, capture_path);
        if (!detail::HasStableWorldMapReopen(reopen_comparison)) {
          fail_live("reopened WorldMapFrame did not preserve the first-open map surface");
          return true;
        }
      }
      if (comparison.has_value() &&
          purpose == detail::LiveE2eCapturePurpose::kCharacterPanelOpen &&
          (comparison->changed_fraction < 0.025 ||
           comparison->mean_absolute_channel_delta < 1.0)) {
        fail_live("CharacterFrame visibility did not reach the final compositor");
        return true;
      }
      if (comparison.has_value() &&
          purpose == detail::LiveE2eCapturePurpose::kPostMovement &&
          comparison->changed_fraction < 0.002) {
        fail_live("movement/camera update did not change the final compositor");
        return true;
      }
      MarkWorldMilestone(
          std::string("capture_") +
              std::string(detail::LiveE2eCapturePurposeName(purpose)),
          elapsed);
      FlushWorldOracleReport(false, false);
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kInfo,
          "Live E2E proved final-backbuffer rendering purpose=" +
              std::string(detail::LiveE2eCapturePurposeName(purpose)) +
              " colors=" +
              std::to_string(validation.quantized_color_count) + " luma=" +
              std::to_string(validation.minimum_luma) + ".." +
              std::to_string(validation.maximum_luma));
    }

    const auto has_validated_capture = [&](const detail::LiveE2eCapturePurpose purpose) {
      return std::any_of(
          e2e_world_report_.captures.begin(), e2e_world_report_.captures.end(),
          [purpose](const detail::LiveE2eWorldCaptureRecord& record) {
            return record.purpose == purpose &&
                   record.validation.status ==
                       detail::LiveE2eFrameStatus::kPlayable;
          });
    };

    if (step_ == 0) {
      if (ctx->set_login_credentials) {
        ctx->set_login_credentials(options_.account, options_.password);
      } else {
        ctx->glue_widgets->SetText("AccountLoginAccountEdit", options_.account);
        ctx->glue_widgets->SetText("AccountLoginPasswordEdit", options_.password);
      }
      e2e_character_name_ =
          detail::MakeLiveE2eScenarioCharacterName(now_ms);
      MarkWorldMilestone("credentials_staged_in_memory", elapsed);
      advance_step(1);
      return true;
    }

    if (step_ == 1 && now_ms - *e2e_step_started_ms_ >= 150) {
      const auto login_button = FirstPresentWidget(
          *ctx->glue_widgets,
          {"AccountLoginLoginButton", "AccountLoginButton", "LoginButton"});
      if (!login_button.has_value()) {
        fail_live("could not find the stock login button");
        return true;
      }
      dispatch(login_button->name, "OnClick", {make_lua_string("LeftButton")});

      if (!ctx->game_state->login_request.pending ||
          !ctx->game_state->wants_login) {
        fail_live("stock login handler did not stage a login request");
        return true;
      }
      scrub_live_password();
      MarkWorldMilestone("login_request_staged", elapsed);
      advance_step(2);
      return true;
    }

    auto continue_from_character_select = [&]() {
      const auto plan = detail::PlanLiveE2eCharacterSelection(
          ctx->game_state->characters);
      switch (plan.action) {
        case detail::LiveE2eCharacterAction::kDeleteScenarioCharacter:
          openwow::diagnostics::Log(
              openwow::diagnostics::LogLevel::kInfo,
              "Live E2E scenario removing its prior character at index " +
                  std::to_string(plan.one_based_index));
          if (!run_lua("DeleteCharacter(" +
                           std::to_string(plan.one_based_index) + ")",
                       "ScenarioDeleteOwnedCharacter.lua")) {
            fail_live("stock DeleteCharacter call failed");
            return;
          }
          advance_step(3);
          return;

        case detail::LiveE2eCharacterAction::kCreateCharacter:
          if (ctx->glue_widgets->GlobalTransitionOverlayVisible()) {
            return;
          }
          request_live_capture();

          advance_step(10);
          return;

        case detail::LiveE2eCharacterAction::kAccountFull:
          fail_live(
              "account has ten non-scenario characters; refusing to delete user data");
          return;
      }
    };

    if (step_ == 2) {
      if (!e2e_realm_requested_ && !ctx->game_state->realms.empty() &&
          ctx->game_state->selected_realm_index < 0) {
        e2e_realm_requested_ = run_lua(
            "local category = RealmList and RealmList.selectedCategory or "
            "GetSelectedCategory(); "
            "if not category or category == 0 then category = 1 end; "
            "local realm = RealmList and RealmList.currentRealm or 0; "
            "if not realm or realm <= 0 then realm = 1 end; "
            "if not RealmList or type(RealmList_OnOk) ~= 'function' then "
            "error('stock RealmList acceptance handler is unavailable') end; "
            "RealmList.selectedCategory = category; "
            "RealmList.currentRealm = realm; "
            "RealmList_OnOk()",
            "ScenarioSelectRealm.lua");
        if (!e2e_realm_requested_) {
          fail_live("stock RealmList acceptance handler failed");
          return true;
        }
        MarkWorldMilestone("realm_selection_requested", elapsed);
      }
      if (detail::HasStableLiveE2eCharacterList(*ctx->game_state,
                                                ctx->flow_phase)) {
        continue_from_character_select();
      }
      return true;
    }

    if (step_ == 3 && detail::HasStableLiveE2eCharacterList(
                          *ctx->game_state, ctx->flow_phase)) {

      continue_from_character_select();
      return true;
    }

    if (step_ == 10) {
      if (ctx->glue_widgets->GlobalTransitionOverlayVisible()) {
        return true;
      }
      if (!run_lua(
              "if not CharacterSelect or not CharacterSelect.createIndex or "
              "CharacterSelect.createIndex <= 0 then "
              "error('stock create slot is unavailable') end; "
              "CharacterSelect_SelectCharacter(CharacterSelect.createIndex)",
              "ScenarioOpenCharacterCreate.lua")) {
        fail_live("stock character-create navigation failed");
        return true;
      }
      advance_step(4);
      return true;
    }

    if (step_ == 4 && ctx->game_state->current_screen == "charcreate") {
      const auto set_name = ctx->glue_runtime->SetEditBoxTextProgrammatically(
          "CharacterCreateNameEdit", e2e_character_name_);
      if (!set_name.ok ||
          ctx->glue_widgets->GetText("CharacterCreateNameEdit") != e2e_character_name_) {
        fail_live("could not populate the stock character-name edit box");
        return true;
      }
      if (run_lua(detail::BuildLiveE2eCharacterCreationScript(),
                  "ScenarioCreateCharacter.lua")) {
        const auto& request = ctx->game_state->char_create_request;
        if (!request.pending || request.name != e2e_character_name_) {
          fail_live("stock character-create handler did not stage the expected request");
          return true;
        }
        e2e_create_fields_ = detail::LiveE2eCreateFieldsFromRequest(request);
        MarkWorldMilestone("character_create_requested", elapsed);
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kInfo,
            "Live E2E staged CMSG_CHAR_CREATE fields " +
                DescribeLiveE2eCreateFields(*e2e_create_fields_));
        advance_step(5);
      } else {
        fail_live("stock CharacterCreate_Okay call failed");
      }
      return true;
    }

    if (step_ == 5 && detail::HasStableLiveE2eCharacterList(
                          *ctx->game_state, ctx->flow_phase)) {
      const auto& characters = ctx->game_state->characters;
      const auto created = std::find_if(
          characters.begin(), characters.end(), [&](const auto& character) {
            return character.name == e2e_character_name_;
          });
      if (created != characters.end()) {
        if (!e2e_create_fields_.has_value()) {
          fail_live("created character appeared without a staged field snapshot");
          return true;
        }
        const auto actual_fields =
            detail::LiveE2eCreateFieldsFromSummary(*created);
        if (created->id == 0 || actual_fields != *e2e_create_fields_) {
          openwow::diagnostics::Log(
              openwow::diagnostics::LogLevel::kError,
              "Live E2E SMSG_CHAR_ENUM field mismatch expected={" +
                  DescribeLiveE2eCreateFields(*e2e_create_fields_) +
                  "} actual={" + DescribeLiveE2eCreateFields(actual_fields) +
                  "} guid_present=" + std::to_string(created->id != 0));
          fail_live("server CHAR_ENUM did not preserve the created character fields");
          return true;
        }
        if (ctx->glue_widgets->GlobalTransitionOverlayVisible()) {
          return true;
        }
        e2e_selected_character_index_ =
            static_cast<std::size_t>(created - characters.begin()) + 1u;
        MarkWorldMilestone("character_enumerated", elapsed);
        request_live_capture();
        advance_step(11);
      }
      return true;
    }

    if (step_ == 11) {
      if (!e2e_selected_character_index_.has_value() ||
          !detail::HasStableLiveE2eCharacterList(*ctx->game_state,
                                                 ctx->flow_phase) ||
          ctx->glue_widgets->GlobalTransitionOverlayVisible()) {
        return true;
      }
      if (!run_lua(
              "CharacterSelect_SelectCharacter(" +
                  std::to_string(*e2e_selected_character_index_) +
                  ", 1); CharacterSelect_EnterWorld()",
              "ScenarioEnterWorld.lua")) {
        fail_live("stock character selection/EnterWorld call failed");
        return true;
      }
      MarkWorldMilestone("enter_world_requested", elapsed);
      advance_step(6);
      return true;
    }

    if (step_ == 6 && !has_validated_capture(
                          detail::LiveE2eCapturePurpose::kLoadingScreen)) {
      if (!ctx->query_play_state) {
        fail_live("loading compositor proof callback is unavailable");
        return true;
      }
      if (ctx->in_world) {
        fail_live(
            "world entry bypassed the final retail loading compositor proof");
      }
      return true;
    }

    if (step_ == 6 && ctx->in_world) {
      if (!ctx->control_forward_movement || !ctx->query_play_state ||
          !ctx->skip_cinematic || !ctx->exercise_world_ui) {
        fail_live("play-mode cinematic/movement proof callbacks are unavailable");
        return true;
      }

      const ScenarioPlayState current = ctx->query_play_state();
      if (!current.ready || !current.connected || current.mover_guid == 0) {
        return true;
      }
      MarkWorldMilestone("local_player_ready", elapsed);

      if (!e2e_cinematic_started_) {
        if (!current.cinematic_playing || !current.cinematic_presenting) {
          return true;
        }
        if (!current.cinematic_can_skip || !ctx->skip_cinematic()) {
          fail_live("first-login cinematic started but could not be skipped");
          return true;
        }
        e2e_cinematic_started_ = true;
        e2e_cinematic_skip_requested_ = true;
        e2e_step_started_ms_ = now_ms;
        MarkWorldMilestone("cinematic_started", elapsed);
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kInfo,
            "Live E2E proved first-login CINEMATIC_START and requested the stock skip lifecycle");
        return true;
      }
      if (current.cinematic_playing) {
        return true;
      }
      if (!e2e_cinematic_skip_requested_) {
        fail_live("first-login cinematic stopped without the requested skip");
        return true;
      }

      if (!detail::HasPlayerRenderProof(current)) {
        return true;
      }
      if (!e2e_live_chat_probe_generation_.has_value()) {
        if (!current.game_ui_loaded || current.game_ui_frames == 0u) {
          return true;
        }
        const auto chat = ctx->exercise_world_ui(
            ScenarioWorldUiAction::kInjectChatProbe);
        if (!chat.handled || !chat.state_changed) {
          fail_live(chat.error.empty() ? "stock ChatFrame interaction failed"
                                       : chat.error);
          return true;
        }
        e2e_live_chat_probe_generation_ = current.frame_generation;
        return true;
      }
      if (!detail::HasCurrentChatProbePaint(
              current, *e2e_live_chat_probe_generation_)) {
        return true;
      }
      if (!detail::HasWorldUiRenderProof(current)) {
        return true;
      }

      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kInfo,
          "Live E2E proved first-login CINEMATIC_STOP before world render validation");
      e2e_play_baseline_ = current;
      RecordWorldSemanticSample(elapsed, current);
      MarkWorldMilestone("cinematic_stopped", elapsed);
      MarkWorldMilestone("world_ui_semantic_ready", elapsed);
      const auto enter_requested = std::find_if(
          e2e_world_report_.milestones.begin(),
          e2e_world_report_.milestones.end(),
          [](const detail::LiveE2eWorldMilestone& milestone) {
            return milestone.name == "enter_world_requested";
          });
      if (enter_requested == e2e_world_report_.milestones.end() ||
          elapsed - enter_requested->elapsed_ms > 30000u) {
        fail_live("world entry did not reach the playable stock UI within 30 seconds");
        return true;
      }
      e2e_next_capture_purpose_ =
          detail::LiveE2eCapturePurpose::kGameplayBaseline;
      e2e_validate_next_capture_ = true;
      request_live_capture();
      advance_step(12);
      return true;
    }

    if (step_ == 12) {
      if (!e2e_play_baseline_.has_value() || !ctx->query_play_state ||
          !ctx->exercise_world_ui || !e2e_world_visual_validated_) {
        return true;
      }
      const auto action_button = ctx->exercise_world_ui(
          ScenarioWorldUiAction::kClickActionButton);
      if (!action_button.handled || !action_button.state_changed) {
        if (now_ms - *e2e_step_started_ms_ < 9000u) {
          return true;
        }
        fail_live(action_button.error.empty()
                      ? "stock ActionButton interaction failed"
                      : action_button.error);
        return true;
      }
      const auto minimap = ctx->exercise_world_ui(
          ScenarioWorldUiAction::kZoomMinimap);
      const auto nameplates = ctx->exercise_world_ui(
          ScenarioWorldUiAction::kEnableNameplates);
      if (!minimap.handled || !minimap.state_changed) {
        fail_live(minimap.error.empty() ? "stock Minimap interaction failed"
                                        : minimap.error);
        return true;
      }
      if (!nameplates.handled) {
        fail_live(nameplates.error.empty()
                      ? "deterministic nearby-unit nameplate staging failed"
                      : nameplates.error);
        return true;
      }
      if (action_button.fallback_used) {
        MarkWorldMilestone("action_target_fallback_used", elapsed);
      }
      e2e_nameplate_probe_enabled_ = true;
      MarkWorldMilestone("chat_message_interaction", elapsed);
      MarkWorldMilestone("action_button_interaction", elapsed);
      MarkWorldMilestone("minimap_zoom_interaction", elapsed);
      MarkWorldMilestone("nameplate_probe_staged", elapsed);
      advance_step(13);
      return true;
    }

    if (step_ == 13) {
      if (!ctx->query_play_state || !ctx->exercise_world_ui) {
        fail_live("world interaction callbacks are unavailable");
        return true;
      }
      const ScenarioPlayState current = ctx->query_play_state();
      if (!detail::HasCurrentNameplatePaint(current)) {
        return true;
      }
      RecordWorldSemanticSample(elapsed, current);
      MarkWorldMilestone("nameplate_visible", elapsed);
      if (!has_validated_capture(
              detail::LiveE2eCapturePurpose::kWorldUiInteractions)) {
        e2e_next_capture_purpose_ =
            detail::LiveE2eCapturePurpose::kWorldUiInteractions;
        e2e_validate_next_capture_ = true;
        request_live_capture();
        return true;
      }
      const auto opened = ctx->exercise_world_ui(
          ScenarioWorldUiAction::kOpenWorldMap);
      if (!opened.handled || !opened.state_changed) {
        fail_live(opened.error.empty() ? "stock world-map toggle failed"
                                       : opened.error);
        return true;
      }
      e2e_world_map_mutation_generation_ = current.frame_generation;
      MarkWorldMilestone("world_map_opened", elapsed);
      advance_step(14);
      return true;
    }

    if (step_ == 14) {
      if (!ctx->query_play_state || !ctx->exercise_world_ui) {
        fail_live("world-map proof callbacks are unavailable");
        return true;
      }
      if (!has_validated_capture(
              detail::LiveE2eCapturePurpose::kWorldMapOpen)) {
        const ScenarioPlayState current = ctx->query_play_state();
        if (!e2e_world_map_mutation_generation_.has_value() ||
            !detail::HasCurrentWorldMapPaint(
                current, *e2e_world_map_mutation_generation_)) {
          return true;
        }
        RecordWorldSemanticSample(elapsed, current);
        MarkWorldMilestone("world_map_visible", elapsed);
        e2e_next_capture_purpose_ =
            detail::LiveE2eCapturePurpose::kWorldMapOpen;
        e2e_validate_next_capture_ = true;
        request_live_capture();
        return true;
      }
      const ScenarioPlayState mutation_state = ctx->query_play_state();
      const auto closed = ctx->exercise_world_ui(
          ScenarioWorldUiAction::kCloseWorldMap);
      const auto reopened = ctx->exercise_world_ui(
          ScenarioWorldUiAction::kOpenWorldMap);
      if (!closed.handled || !closed.state_changed || !reopened.handled ||
          !reopened.state_changed) {
        fail_live(!closed.error.empty() ? closed.error : reopened.error);
        return true;
      }
      e2e_world_map_mutation_generation_ = mutation_state.frame_generation;
      MarkWorldMilestone("world_map_reopened", elapsed);
      advance_step(19);
      return true;
    }

    if (step_ == 19) {
      if (!ctx->query_play_state || !ctx->exercise_world_ui) {
        fail_live("reopened world-map proof callbacks are unavailable");
        return true;
      }
      if (!has_validated_capture(
              detail::LiveE2eCapturePurpose::kWorldMapReopened)) {
        const ScenarioPlayState current = ctx->query_play_state();
        if (!e2e_world_map_mutation_generation_.has_value() ||
            !detail::HasCurrentWorldMapPaint(
                current, *e2e_world_map_mutation_generation_)) {
          return true;
        }
        RecordWorldSemanticSample(elapsed, current);
        MarkWorldMilestone("world_map_reopened_visible", elapsed);
        e2e_next_capture_purpose_ =
            detail::LiveE2eCapturePurpose::kWorldMapReopened;
        e2e_validate_next_capture_ = true;
        request_live_capture();
        return true;
      }
      const ScenarioPlayState mutation_state = ctx->query_play_state();
      const auto closed = ctx->exercise_world_ui(
          ScenarioWorldUiAction::kCloseWorldMap);
      const auto opened = ctx->exercise_world_ui(
          ScenarioWorldUiAction::kOpenCharacterPanel);
      if (!closed.handled || !closed.state_changed || !opened.handled ||
          !opened.state_changed) {
        fail_live(!closed.error.empty() ? closed.error : opened.error);
        return true;
      }
      e2e_character_panel_mutation_generation_ =
          mutation_state.frame_generation;
      MarkWorldMilestone("character_panel_opened", elapsed);
      advance_step(15);
      return true;
    }

    if (step_ == 15) {
      if (!ctx->query_play_state || !ctx->exercise_world_ui) {
        fail_live("character-panel proof callbacks are unavailable");
        return true;
      }
      if (!has_validated_capture(
              detail::LiveE2eCapturePurpose::kCharacterPanelOpen)) {
        const ScenarioPlayState current = ctx->query_play_state();
        if (!e2e_character_panel_mutation_generation_.has_value() ||
            !detail::HasCurrentCharacterPanelPaint(
                current, *e2e_character_panel_mutation_generation_)) {
          return true;
        }
        RecordWorldSemanticSample(elapsed, current);
        MarkWorldMilestone("character_panel_visible", elapsed);
        e2e_next_capture_purpose_ =
            detail::LiveE2eCapturePurpose::kCharacterPanelOpen;
        e2e_validate_next_capture_ = true;
        request_live_capture();
        return true;
      }
      const auto closed = ctx->exercise_world_ui(
          ScenarioWorldUiAction::kCloseCharacterPanel);
      const auto restored = ctx->exercise_world_ui(
          ScenarioWorldUiAction::kRestoreTransientState);
      if (!closed.handled || !closed.state_changed || !restored.handled ||
          !restored.state_changed) {
        fail_live(!closed.error.empty() ? closed.error : restored.error);
        return true;
      }
      e2e_nameplate_probe_enabled_ = false;
      e2e_world_interactions_proved_ = true;
      MarkWorldMilestone("stock_world_ui_interactions_complete", elapsed);
      advance_step(16);
      return true;
    }

    if (step_ == 16) {
      if (!ctx->query_play_state || !ctx->control_forward_movement) {
        fail_live("movement proof callbacks are unavailable");
        return true;
      }
      const ScenarioPlayState current = ctx->query_play_state();
      if (!current.ready || !current.connected ||
          !detail::HasPlayerRenderProof(current) ||
          current.world_ui_world_map_visible ||
          current.world_ui_character_panel_visible) {
        return true;
      }
      e2e_play_baseline_ = current;
      e2e_play_moving_.reset();
      RecordWorldSemanticSample(elapsed, current);
      if (!ctx->control_forward_movement(
              ScenarioForwardMovementCommand::kStart)) {
        fail_live("active stock MOVEFORWARD key-down was not handled");
        return true;
      }
      e2e_forward_binding_held_ = true;
      MarkWorldMilestone("movement_start_requested", elapsed);
      advance_step(7);
      return true;
    }

    if (step_ == 7) {
      if (!e2e_play_baseline_.has_value() || !ctx->query_play_state ||
          !ctx->control_forward_movement) {
        fail_live("lost movement-start proof state");
        return true;
      }

      const ScenarioPlayState moving = ctx->query_play_state();
      if (!detail::HasForwardStartProof(*e2e_play_baseline_, moving)) {
        return true;
      }

      if (!e2e_play_moving_.has_value()) {
        e2e_play_moving_ = moving;
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kInfo,
            "Live E2E proved MSG_MOVE_START_FORWARD socket send and local forward state");
      }
      if (now_ms - *e2e_step_started_ms_ < 650 ||
          !detail::HasSustainedForwardProof(*e2e_play_baseline_, moving)) {
        return true;
      }
      if (!detail::HasCameraMotionProof(*e2e_play_baseline_, moving)) {
        return true;
      }
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kInfo,
          "Live E2E proved sustained local movement and the retail 500 ms heartbeat send");
      if (!ctx->control_forward_movement(
              ScenarioForwardMovementCommand::kStop)) {
        fail_live("active stock MOVEFORWARD key-up was not handled");
        return true;
      }
      e2e_forward_binding_held_ = false;
      advance_step(8);
      return true;
    }

    if (step_ == 8) {
      if (!e2e_play_moving_.has_value() || !ctx->query_play_state) {
        fail_live("lost movement-stop proof state");
        return true;
      }

      const ScenarioPlayState stopped = ctx->query_play_state();
      if (!detail::HasForwardStopProof(*e2e_play_moving_, stopped)) {
        return true;
      }

      RecordWorldSemanticSample(elapsed, stopped);
      MarkWorldMilestone("movement_camera_and_stop_proved", elapsed);
      e2e_next_capture_purpose_ =
          detail::LiveE2eCapturePurpose::kPostMovement;
      e2e_validate_next_capture_ = true;
      request_live_capture();
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kInfo,
          "Live E2E proved MSG_MOVE_STOP socket send and cleared local forward state");
      e2e_world_entered_ms_ = now_ms;
      advance_step(9);
      return true;
    }

    if (step_ == 9 && e2e_world_entered_ms_.has_value()) {
      if (!has_validated_capture(
              detail::LiveE2eCapturePurpose::kPostMovement) ||
          now_ms - *e2e_world_entered_ms_ < 1000u) {
        return true;
      }
      e2e_next_capture_purpose_ =
          detail::LiveE2eCapturePurpose::kStableGameplay;
      e2e_validate_next_capture_ = true;
      request_live_capture();
      advance_step(17);
      return true;
    }

    if (step_ == 17) {
      if (!has_validated_capture(
              detail::LiveE2eCapturePurpose::kStableGameplay)) {
        return true;
      }
      if (!ctx->exercise_world_ui || !ctx->query_play_state ||
          !e2e_world_interactions_proved_) {
        fail_live("final UI/logout proof callbacks are unavailable");
        return true;
      }
      const ScenarioPlayState final_state = ctx->query_play_state();
      RecordWorldSemanticSample(elapsed, final_state);
      const auto restored = ctx->exercise_world_ui(
          ScenarioWorldUiAction::kRestoreTransientState);
      if (!restored.handled || !restored.state_changed) {
        fail_live(restored.error.empty() ? "temporary UI state restore failed"
                                         : restored.error);
        return true;
      }
      const auto logout = ctx->exercise_world_ui(
          ScenarioWorldUiAction::kRequestLogout);
      if (!logout.handled || !logout.state_changed) {
        fail_live(logout.error.empty() ? "stock Logout interaction failed"
                                       : logout.error);
        return true;
      }
      e2e_logout_requested_ = true;
      MarkWorldMilestone("graceful_logout_requested", elapsed);
      advance_step(18);
      return true;
    }

    if (step_ == 18) {
      if (!e2e_logout_requested_) {
        fail_live("lost graceful logout request state");
        return true;
      }
      if (ctx->in_world) {
        if (ctx->query_play_state) {
          const ScenarioPlayState logout_state = ctx->query_play_state();
          if (logout_state.logout_request_pending) {
            MarkWorldMilestone("logout_request_pending", elapsed);
          }
          if (logout_state.logout_countdown_visible &&
              logout_state.logout_countdown_seconds > 0.0f &&
              logout_state.logout_countdown_seconds <= 20.0f) {
            e2e_logout_countdown_observed_ = true;
            MarkWorldMilestone("stock_logout_countdown_visible", elapsed);
          }
        }
        return true;
      }
      if (ctx->game_state->current_screen != "charselect") {
        return true;
      }
      if (!e2e_logout_countdown_observed_) {
        fail_live("stock CAMP countdown was never visible during graceful logout");
        return true;
      }
      MarkWorldMilestone("graceful_logout_complete", elapsed);
      FlushWorldOracleReport(true, true);
      request_live_capture();
      should_exit_ = true;
      return true;
    }
    return true;
  }

  if (scenario == "glue_nightelf_lighting_regression") {

    if (step_ == 0) {
      if (!run_lua(R"LUA(
        local names = {
          "TOSFrame", "EULAFrame", "TerminationFrame", "ScanningFrame",
          "ContestFrame", "SurveyFrame", "ConnectionHelpFrame", "GlueDialog",
          "OptionsSelectFrame", "AccountLoginUI"
        }
        for _, name in ipairs(names) do
          local frame = _G[name]
          if frame and frame.Hide then frame:Hide() end
        end
        SetGlueScreen("charcreate")
      )LUA", "ScenarioNightElfBoot.lua")) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
            "glue_nightelf_lighting_regression: boot Lua failed");
        failed_ = true;
        should_exit_ = true;
        return true;
      }
      step_ = 1;
      return true;
    }
    if (step_ == 1 && elapsed >= 1500) {
      if (!run_lua(R"LUA(
        for index = 1, CharacterCreate.numRaces or 10 do
          SetSelectedRace(index)
          SetCharacterRace(index)
          local _, fileString = GetNameForRace()
          if fileString and string.upper(fileString) == "NIGHTELF" then
            break
          end
        end
      )LUA", "ScenarioNightElfSelect.lua")) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
            "glue_nightelf_lighting_regression: race select failed");
        failed_ = true;
        should_exit_ = true;
        return true;
      }
      step_ = 2;
      return true;
    }
    if (step_ == 2 && elapsed >= 4000) {
      request_capture();
      step_ = 3;
      return true;
    }
    if (step_ == 3 && elapsed >= 5000) {
      should_exit_ = true;
    }
    return true;
  }

  if (scenario == "glue_delete_dialog_regression") {

    if (step_ == 0) {
      if (!run_lua(R"LUA(
        local names = {
          "TOSFrame", "EULAFrame", "TerminationFrame", "ScanningFrame",
          "ContestFrame", "SurveyFrame", "ConnectionHelpFrame", "GlueDialog",
          "OptionsSelectFrame", "VideoOptionsFrame", "AudioOptionsFrame",
          "GlueTooltip", "AccountLoginUI"
        }
        for _, name in ipairs(names) do
          local frame = _G[name]
          if frame and frame.Hide then frame:Hide() end
        end
        CharacterDeleteText1:SetFormattedText(
            CONFIRM_CHAR_DELETE, "Diagnostic", 55, "Warrior")
        CharacterDeleteDialog:Show()
      )LUA", "ScenarioDeleteDialog.lua")) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
            "glue_delete_dialog_regression: setup Lua failed");
        failed_ = true;
        should_exit_ = true;
        return true;
      }
      step_ = 1;
      return true;
    }
    if (step_ == 1 && elapsed >= 700) {
      request_capture();
      step_ = 2;
      return true;
    }
    if (step_ == 2 && elapsed >= 1400) {
      should_exit_ = true;
    }
    return true;
  }

  if (scenario == "glue_ui_regression") {
    constexpr const char* kHideScreens = R"LUA(
      local names = {
        "TOSFrame", "EULAFrame", "TerminationFrame", "ScanningFrame",
        "ContestFrame", "SurveyFrame", "ConnectionHelpFrame", "GlueDialog",
        "OptionsSelectFrame", "VideoOptionsFrame", "AudioOptionsFrame",
        "GlueTooltip"
      }
      for _, name in ipairs(names) do
        local frame = _G[name]
        if frame and frame.Hide then frame:Hide() end
      end
    )LUA";

    const auto fail = [&](const std::string& reason) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                         "Glue UI regression scenario failed: " + reason);
      failed_ = true;
      should_exit_ = true;
      request_capture();
    };
    const auto fail_if = [&](const std::string_view area,
                             const std::optional<std::string>& error) {
      if (!error.has_value()) {
        return false;
      }
      fail(std::string(area) + ": " + *error);
      return true;
    };

    if (step_ == 0) {
      if (!run_lua(std::string(kHideScreens) + R"LUA(
        if AccountLoginUI then AccountLoginUI:Show() end
        AccountLoginAccountEdit:SetText("diagnostic@example.test")
        AccountLoginPasswordEdit:SetText("diagnostic-password")
        AccountLoginAccountEdit:SetFocus()
      )LUA", "ScenarioGlueLogin.lua")) {
        fail("login setup Lua failed");
        return true;
      }
      request_capture();
      step_ = 1;
      return true;
    }
    if (step_ == 1 && elapsed >= 450) {
      if (fail_if("account edit box",
                  ValidateEditBoxPresentation(
                      *ctx->glue_widgets, "AccountLoginAccountEdit",
                      "diagnostic@example.test", false)) ||
          fail_if("account edit box backdrop",
                  ValidateGeneratedBackdrop(
                      *ctx->glue_widgets, "AccountLoginAccountEdit", true)) ||
          fail_if("password edit box",
                  ValidateEditBoxPresentation(
                      *ctx->glue_widgets, "AccountLoginPasswordEdit",
                      "diagnostic-password", true)) ||
          fail_if("password edit box backdrop",
                  ValidateGeneratedBackdrop(
                      *ctx->glue_widgets, "AccountLoginPasswordEdit", true))) {
        return true;
      }
      if (!run_lua(std::string(kHideScreens) + R"LUA(
        if AccountLoginUI then AccountLoginUI:Hide() end
        if VideoOptionsFrame then VideoOptionsFrame:Show() end
      )LUA", "ScenarioVideoOptions.lua")) {
        fail("video-options setup Lua failed");
        return true;
      }
      request_capture();
      step_ = 2;
      return true;
    }
    if (step_ == 2 && elapsed >= 900) {
      if (fail_if("video-options backdrop",
                  ValidateGeneratedBackdrop(
                      *ctx->glue_widgets, "VideoOptionsFrame", true)) ||
          fail_if("video-options category edges",
                  ValidateNamedFrameEdges(
                      *ctx->glue_widgets,
                      "VideoOptionsFrameCategoryFrame")) ||
          fail_if("video-options panel edges",
                  ValidateGeneratedBackdrop(
                      *ctx->glue_widgets,
                      "VideoOptionsFramePanelContainer", false)) ||
          fail_if("video-options gamma slider",
                  ValidateSliderPresentation(
                      ctx->glue_widgets,
                      "VideoOptionsResolutionPanelGammaSlider",
                      "VideoOptionsResolutionPanelGammaSliderThumb"))) {
        return true;
      }
      if (!run_lua(R"LUA(
        local capabilityLockedControls = {
          "VideoOptionsEffectsPanelTerrainDetail",
          "VideoOptionsEffectsPanelClutterDensity",
          "VideoOptionsEffectsPanelClutterRadius",
          "VideoOptionsEffectsPanelTextureResolution",
          "VideoOptionsEffectsPanelTextureFiltering",
          "AudioOptionsSoundPanelUseHardware",
          "AudioOptionsSoundPanelHRTF",
        }
        for _, name in ipairs(capabilityLockedControls) do
          local control = assert(_G[name], name .. " is missing")
          assert(not control:IsEnabled(), name .. " must be capability-disabled")
          control:Enable()
          assert(not control:IsEnabled(), name .. " escaped its capability lock")
        end
        assert(GetCVar("Sound_EnableHardware") == "0",
               "hardware audio voices must be forced off")
        assert(GetCVar("Sound_EnableSoftwareHRTF") == "0",
               "software HRTF must be forced off")

        SetCVar("gxWindow", "1")
        local resolutions = { GetScreenResolutions() }
        assert(#resolutions > 1, "display exposes fewer than two resolutions")
        local currentResolution = GetCurrentResolution()
        local targetResolution = currentResolution == 1 and #resolutions or 1
        SetScreenResolution(targetResolution)
        SetCVar("gxVSync", "0")
        SetCVar("gxMultisample", "2")
        SetCVar("gxTripleBuffer", "1")
        SetCVar("gxFixLag", "0")
        RestartGx()
        SetCVar("Sound_OutputQuality", "0")
        SetCVar("Sound_NumChannels", "24")
        Sound_GameSystem_RestartSoundSystem()
      )LUA", "ScenarioApplyRuntimeSettings.lua")) {
        fail("runtime video/audio settings application failed");
        return true;
      }
      if (!run_lua(std::string(kHideScreens) + R"LUA(
        if AccountLoginUI then AccountLoginUI:Hide() end
        if AudioOptionsFrame then AudioOptionsFrame:Show() end
      )LUA", "ScenarioAudioOptions.lua")) {
        fail("audio-options setup Lua failed");
        return true;
      }
      request_capture();
      step_ = 3;
      return true;
    }
    if (step_ == 3 && elapsed >= 1350) {
      const std::string resolution =
          openwow::ui::game::CVarSystem::Instance().GetCVar("gxResolution");
      int expected_width = 0;
      int expected_height = 0;
      if (std::sscanf(resolution.c_str(), "%dx%d", &expected_width,
                      &expected_height) != 2) {
        fail("gxRestart committed an invalid gxResolution: " + resolution);
        return true;
      }
      if (ctx->viewport_width != expected_width ||
          ctx->viewport_height != expected_height) {
        if (elapsed < 4000) {
          return true;
        }
        fail("gxRestart did not apply the requested " + resolution +
             " drawable size; got " +
             std::to_string(ctx->viewport_width) + "x" +
             std::to_string(ctx->viewport_height));
        return true;
      }
      if (fail_if("audio-options backdrop",
                  ValidateGeneratedBackdrop(
                      *ctx->glue_widgets, "AudioOptionsFrame", true)) ||
          fail_if("audio-options category edges",
                  ValidateNamedFrameEdges(
                      *ctx->glue_widgets,
                      "AudioOptionsFrameCategoryFrame")) ||
          fail_if("audio-options panel edges",
                  ValidateGeneratedBackdrop(
                      *ctx->glue_widgets,
                      "AudioOptionsFramePanelContainer", false)) ||
          fail_if("audio-options master slider",
                  ValidateSliderPresentation(
                      ctx->glue_widgets,
                      "AudioOptionsSoundPanelMasterVolume",
                      "AudioOptionsSoundPanelMasterVolumeThumb"))) {
        return true;
      }
      if (!run_lua(std::string(kHideScreens) + R"LUA(
        SetCVar("gxFixLag", "1")
        if AccountLoginUI then AccountLoginUI:Show() end
      )LUA", "ScenarioDialogBackground.lua")) {
        fail("status-dialog setup Lua failed");
        return true;
      }
      std::vector<openwow::ui::glue::GlueLuaValue> args;
      args.push_back(make_lua_string("OKAY"));
      args.push_back(make_lua_string(
          "The information you have entered is not valid. Please check the account name "
          "and password, then try again. If the problem persists, visit account support."));
      const auto dialog =
          ctx->glue_runtime->DispatchRegisteredEvent("OPEN_STATUS_DIALOG", args);
      if (!dialog.ok) {
        fail("stock OPEN_STATUS_DIALOG dispatch failed: " + dialog.error);
        return true;
      }
      request_capture();
      step_ = 4;
      return true;
    }
    if (step_ == 4 && elapsed >= 1900) {
      if (fail_if("status-dialog backdrop",
                  ValidateGeneratedBackdrop(
                      *ctx->glue_widgets, "GlueDialogBackground", true)) ||
          fail_if("status-dialog wrapped text",
                  ValidateContainedText(
                      *ctx->glue_widgets, "GlueDialogBackground",
                      "GlueDialogText"))) {
        return true;
      }
      if (!run_lua(std::string(kHideScreens) + R"LUA(
        if AccountLoginUI then AccountLoginUI:Hide() end
        local legalChildren = {
          "TOSScrollFrame", "EULAScrollFrame", "TerminationScrollFrame",
          "ScanningScrollFrame", "ContestScrollFrame", "TOSText", "EULAText",
          "TerminationText", "ScanningText", "ContestText"
        }
        for _, name in ipairs(legalChildren) do
          local frame = _G[name]
          if frame and frame.Hide then frame:Hide() end
        end
        TOSFrame.noticeType = "EULA"
        TOSFrameTitle:SetText(EULA_FRAME_TITLE)
        TOSFrameHeader:SetWidth(TOSFrameTitle:GetWidth())
        EULAScrollFrame:SetVerticalScroll(0)
        EULAScrollFrameScrollBar:SetValue(0)
        EULAScrollFrame:Show()
        EULAText:Show()
        TOSFrame:Show()
      )LUA", "ScenarioEulaTop.lua")) {
        fail("EULA setup Lua failed");
        return true;
      }
      request_capture();
      step_ = 5;
      return true;
    }
    if (step_ == 5 && elapsed >= 2400) {
      const auto scroll_frame =
          ctx->glue_widgets->GetResolvedWidget("EULAScrollFrame");
      const auto content = ctx->glue_widgets->GetResolvedWidget("EULAText");
      const double range =
          ctx->glue_widgets->GetVerticalScrollRange("EULAScrollFrame");
      if (!scroll_frame.has_value() ||
          !ctx->glue_widgets->IsVisible("EULAScrollFrame") ||
          !content.has_value() || !ctx->glue_widgets->IsVisible("EULAText") ||
          !content->scroll_child_content || content->text.empty() ||
          content->height <= scroll_frame->height || range <= 0.0 ||
          ctx->glue_widgets->GetVerticalScroll("EULAScrollFrame") != 0.0) {
        fail("EULA top has no visible, scrollable stock SimpleHTML content");
        return true;
      }
      const auto top_presentation =
          ctx->glue_widgets->ResolveScrollPresentation(*content);
      if (!top_presentation.clip.has_value() ||
          top_presentation.widget.y != content->y ||
          top_presentation.clip->x != scroll_frame->x ||
          top_presentation.clip->y != scroll_frame->y ||
          top_presentation.clip->width != scroll_frame->width ||
          top_presentation.clip->height != scroll_frame->height) {
        fail("EULA top does not share the ScrollFrame viewport clip");
        return true;
      }
      if (fail_if("EULA scrollbar",
                  ValidateSliderPresentation(
                      ctx->glue_widgets, "EULAScrollFrameScrollBar",
                      "EULAScrollFrameScrollBarThumbTexture"))) {
        return true;
      }
      const bool layout_was_dirty = ctx->glue_widgets->IsLayoutDirty();
      ctx->glue_widgets->SetVerticalScroll("EULAScrollFrame", range);
      ctx->glue_widgets->SetValue("EULAScrollFrameScrollBar", range);
      if (ctx->glue_widgets->IsLayoutDirty() != layout_was_dirty) {
        fail("EULA scrolling dirtied the full Glue layout");
        return true;
      }
      request_capture();
      step_ = 6;
      return true;
    }
    if (step_ == 6 && elapsed >= 2900) {
      const auto content = ctx->glue_widgets->GetResolvedWidget("EULAText");
      const auto slider =
          ctx->glue_widgets->GetResolvedWidget("EULAScrollFrameScrollBar");
      const auto thumb = ctx->glue_widgets->GetResolvedWidget(
          "EULAScrollFrameScrollBarThumbTexture");
      const double range =
          ctx->glue_widgets->GetVerticalScrollRange("EULAScrollFrame");
      if (!content.has_value() || !slider.has_value() || !thumb.has_value() ||
          ctx->glue_widgets->GetVerticalScroll("EULAScrollFrame") != range ||
          ctx->glue_widgets->GetValue("EULAScrollFrameScrollBar") != range) {
        fail("EULA bottom did not retain the requested scroll position");
        return true;
      }
      const auto bottom_presentation =
          ctx->glue_widgets->ResolveScrollPresentation(*content);
      if (!bottom_presentation.clip.has_value() ||
          bottom_presentation.widget.y >= content->y ||
          thumb->y <= slider->y) {
        fail("EULA content or scrollbar thumb did not visibly move to the bottom");
        return true;
      }
      if (!run_lua(std::string(kHideScreens) + R"LUA(
        if AccountLoginUI then AccountLoginUI:Show() end
        GlueTooltip:ClearAllPoints()
        GlueTooltip_SetText(
          "A stock Glue tooltip with correctly measured text and backdrop.",
          GlueTooltip, 1.0, 0.82, 0.0)
        GlueTooltip_SetOwner(AccountLoginLoginButton, GlueTooltip,
                             0, 12, "BOTTOM", "TOP")
      )LUA", "ScenarioGlueTooltip.lua")) {
        fail("tooltip setup Lua failed");
        return true;
      }
      request_capture();
      step_ = 7;
      return true;
    }
    if (step_ == 7 && elapsed >= 3400) {
      if (fail_if("tooltip backdrop",
                  ValidateGeneratedBackdrop(
                      *ctx->glue_widgets, "GlueTooltip", true)) ||
          fail_if("tooltip measured text",
                  ValidateContainedText(
                      *ctx->glue_widgets, "GlueTooltip",
                      "GlueTooltipTextLeft1"))) {
        return true;
      }
      should_exit_ = true;
      return true;
    }
    return true;
  }

  if (scenario == "glue_character_create_regression") {
    const auto fail = [&](const std::string& reason) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                         "Character-create scenario failed: " + reason);
      failed_ = true;
      should_exit_ = true;
    };
    const auto validate_icon_regions = [&](const char* prefix,
                                           const int maximum)
        -> std::optional<std::string> {
      int visible_buttons = 0;
      std::unordered_set<std::string> normal_icon_rects;
      for (int index = 1; index <= maximum; ++index) {
        const std::string button_name =
            std::string(prefix) + std::to_string(index);
        const auto button = ctx->glue_widgets->GetWidget(button_name);
        if (!button.has_value() || !ctx->glue_widgets->IsVisible(button_name)) {
          continue;
        }
        ++visible_buttons;
        for (const char* suffix : {"NormalTexture", "PushedTexture"}) {
          const auto texture = ctx->glue_widgets->GetWidget(button_name + suffix);
          if (!texture.has_value()) {
            return button_name + suffix + " is missing";
          }
          if (!openwow::text::EqualsIgnoreCaseAscii(texture->kind, "Texture")) {
            return button_name + suffix + " has kind=" + texture->kind;
          }
          if (texture->texture_file.empty()) {
            return button_name + suffix + " has no texture file";
          }
          if (std::string_view(suffix) == "NormalTexture") {
            normal_icon_rects.insert(
                std::to_string(texture->tex_left) + ":" +
                std::to_string(texture->tex_right) + ":" +
                std::to_string(texture->tex_top) + ":" +
                std::to_string(texture->tex_bottom));
          }
        }
      }
      if (visible_buttons != maximum) {
        return std::string(prefix) + " has only " +
               std::to_string(visible_buttons) + " of " +
               std::to_string(maximum) + " visible button(s)";
      }
      if (normal_icon_rects.size() != static_cast<std::size_t>(maximum)) {
        return std::string(prefix) + " has only " +
               std::to_string(normal_icon_rects.size()) + " of " +
               std::to_string(maximum) + " distinct atlas rectangles";
      }
      return std::nullopt;
    };

    if (step_ == 0) {
      const bool entered = run_lua(R"LUA(
        local overlays = {
          "TOSFrame", "EULAFrame", "TerminationFrame", "ScanningFrame",
          "ContestFrame", "SurveyFrame", "ConnectionHelpFrame", "GlueDialog",
          "OptionsSelectFrame", "VideoOptionsFrame", "AudioOptionsFrame",
          "GlueTooltip", "CharacterCreateTooltip"
        }
        for _, name in ipairs(overlays) do
          local frame = _G[name]
          if frame and frame.Hide then frame:Hide() end
        end
        SetGlueScreen("charcreate")
      )LUA", "ScenarioCharacterCreateEnter.lua");
      if (!entered) {
        fail("stock SetGlueScreen(charcreate) failed");
        return true;
      }
      step_ = 1;
      return true;
    }

    if (step_ == 1 && elapsed >= 1500) {
      if (ctx->game_state == nullptr ||
          ctx->game_state->current_screen != "charcreate") {
        fail("Glue state did not enter charcreate");
        return true;
      }
      if (const auto icon_error =
              validate_icon_regions("CharacterCreateRaceButton", 10);
          icon_error.has_value()) {
        fail("race icon regions: " + *icon_error);
        return true;
      }
      if (const auto icon_error =
              validate_icon_regions("CharacterCreateClassButton", 10);
          icon_error.has_value()) {
        fail("class icon regions: " + *icon_error);
        return true;
      }

      const auto* create_scene = ctx->game_state->char_customize_scene;
      if (create_scene == nullptr ||
          create_scene->selected_character_model_path().empty()) {
        fail("character-create attached model path is empty (race=" +
             std::to_string(ctx->game_state->create_race) + " sex=" +
             std::to_string(ctx->game_state->create_sex) + " class=" +
             std::to_string(ctx->game_state->create_class) + " skin=" +
             std::to_string(ctx->game_state->create_skin) + " face=" +
             std::to_string(ctx->game_state->create_face) + " hair=" +
             std::to_string(ctx->game_state->create_hair_style) + ")");
        return true;
      }
      if (ctx->game_state->char_customize_frame_name != "CharacterCreate") {
        fail("CharacterCreate ModelFFX was not bound as the customize viewport");
        return true;
      }

      const auto tooltip = ctx->glue_runtime->RunWidgetEvent(
          "CharacterCreateRaceButton1", "OnEnter",
          "ScenarioCharacterCreateRaceTooltip.OnEnter", {});
      if (!tooltip.ok) {
        fail("stock race tooltip dispatch failed: " + tooltip.error);
        return true;
      }

      if (!run_lua(R"LUA(
        local checked = 0
        for index = 1, CharacterCreate.numRaces or 0 do
          if _G["CharacterCreateRaceButton" .. index]:GetChecked() then
            checked = checked + 1
          end
        end
        assert(checked == 1, "expected exactly one selected race, got " .. checked)
      )LUA", "ScenarioCharacterCreateExclusiveRaceSelection.lua")) {
        fail("race checkbuttons are not mutually exclusive");
        return true;
      }

      request_capture();
      step_ = 2;
      return true;
    }

    if (step_ == 2 && elapsed >= 5000) {

      request_capture();
      step_ = 3;
      return true;
    }

    if (step_ == 3 && elapsed >= 5500) {
      if (!run_lua(R"LUA(
        CharacterCreateTooltip:Hide()
        if CharacterCreate.numRaces and CharacterCreate.numRaces >= 2 then
          -- A real CheckButton click toggles before its OnClick handler.  Use
          -- the widget path so this regression exercises that native order.
          CharacterCreateRaceButton2:Click()
        end
        if CharacterCreate.numClasses and CharacterCreate.numClasses >= 2 then
          CharacterCreateClassButton2:Click()
        end
      )LUA", "ScenarioCharacterCreateAlternateSelection.lua")) {
        fail("stock alternate race/class selection failed");
        return true;
      }
      step_ = 4;
      return true;
    }

    if (step_ == 4 && elapsed >= 9000) {
      const auto* create_scene = ctx->game_state->char_customize_scene;
      if (create_scene == nullptr ||
          create_scene->selected_character_model_path().empty()) {
        fail("alternate race selection cleared the attached model path");
        return true;
      }
      if (ctx->game_state->create_race != 3 ||
          create_scene->selected_character_model_path().find("/Dwarf/") ==
              std::string::npos) {
        fail("second displayed race did not publish the Dwarf preview (race=" +
             std::to_string(ctx->game_state->create_race) + " model=" +
             create_scene->selected_character_model_path() + ")");
        return true;
      }
      request_capture();
      step_ = 5;
      return true;
    }

    if (step_ == 5 && elapsed >= 9500) {

      if (!run_lua(R"LUA(
        for index = 1, CharacterCreate.numRaces or 0 do
          SetSelectedRace(index)
          SetCharacterRace(index)
          CharacterCreateEnumerateClasses(GetAvailableClasses())
        end
        SetSelectedRace(1)
        SetCharacterRace(1)
        CharacterCreateEnumerateClasses(GetAvailableClasses())
        local _, _, classIndex = GetSelectedClass()
        SetCharacterClass(classIndex)
      )LUA", "ScenarioCharacterCreateCycleAll.lua")) {
        fail("cycling all stock race/class atlas slots failed");
        return true;
      }
      step_ = 6;
      return true;
    }

    if (step_ == 6 && elapsed >= 13500) {
      const auto* create_scene = ctx->game_state->char_customize_scene;
      if (create_scene == nullptr ||
          create_scene->selected_character_model_path().empty()) {
        fail("all-race cycle left the attached model path empty");
        return true;
      }
      request_capture();
      should_exit_ = true;
      step_ = 7;
      return true;
    }
    return true;
  }

  if (scenario == "glue_login_idle_30s") {
    constexpr std::uint32_t kCaptureIntervalMs = 5000u;
    constexpr std::uint32_t kFinalCaptureBucket = 6u;

    const std::uint32_t current_bucket =
        std::min(elapsed / kCaptureIntervalMs, kFinalCaptureBucket);
    if (static_cast<std::uint32_t>(step_) <= current_bucket) {
      request_capture();
      step_ = static_cast<int>(current_bucket + 1u);
      if (current_bucket == kFinalCaptureBucket) {
        should_exit_ = true;
      }
    }
    return true;
  }

  if (step_ == 0) {

    if (ctx->set_login_credentials) {
      ctx->set_login_credentials("test", "test");
    } else {
      ctx->glue_widgets->SetText("AccountLoginAccountEdit", "test");
      ctx->glue_widgets->SetText("AccountLoginPasswordEdit", "test");
    }
    request_capture();
    step_ = 1;
    return true;
  }

  if (step_ == 1 && elapsed >= 100) {

    auto w = FirstPresentWidget(*ctx->glue_widgets,
                                {"AccountLoginSaveAccountName",
                                 "AccountLoginRememberAccountName",
                                 "AccountLoginRemember",
                                 "RememberAccountName"});
    if (w.has_value() && ToLowerAscii(w->kind) == "checkbutton") {
      ctx->glue_widgets->SetChecked(w->name, !ctx->glue_widgets->Checked(w->name));
      dispatch(w->name, "OnClick", {make_lua_string("LeftButton")});
    }
    request_capture();
    step_ = 2;
    return true;
  }

  if (step_ == 2 && elapsed >= 200) {

    auto w = FirstPresentWidget(*ctx->glue_widgets,
                                {"AccountLoginLoginButton",
                                 "AccountLoginButton",
                                 "LoginButton"});
    if (w.has_value() && (ToLowerAscii(w->kind) == "button")) {
      ctx->glue_widgets->SetButtonState(w->name, "PUSHED");
      dispatch(w->name, "OnMouseDown", {make_lua_string("LeftButton")});
      ctx->glue_widgets->SetButtonState(w->name, "NORMAL");
      dispatch(w->name, "OnMouseUp", {make_lua_string("LeftButton")});
      dispatch(w->name, "OnClick", {make_lua_string("LeftButton")});
    }
    request_capture();
    step_ = 3;
    return true;
  }

  if (step_ == 3 && elapsed >= 1200) {
    request_capture();
    should_exit_ = true;
    return true;
  }

  return true;
}

}
