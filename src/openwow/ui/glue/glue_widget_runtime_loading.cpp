#include "openwow/ui/glue/glue_widget_runtime.h"

#include "openwow/ui/game/runtime/frame_template_expander.h"
#include "openwow/data/formats/blp/texture_path.h"
#include "openwow/game/localization.h"
#include "openwow/ui/framexml/framexml_name_utils.h"
#include "openwow/ui/framexml/framexml_parser.h"
#include "openwow/ui/framexml/layout_anchor_resolution.h"
#include "openwow/ui/framexml/layout_resolver.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::ui::glue {

using openwow::data::blp::NormalizeTexturePath;
using openwow::text::ToLowerAscii;

namespace {

std::optional<std::string> ReadLooseTextFile(
    const openwow::vfs::VirtualFileSystem& vfs,
    const std::string& relative_path) {

  const std::filesystem::path relative(relative_path);
  if (relative.empty() || relative.is_absolute()) {
    return std::nullopt;
  }
  for (const auto& component : relative) {
    if (component == "..") {
      return std::nullopt;
    }
  }
  for (const auto& mount : vfs.mounts()) {
    if (!mount.enabled || mount.source_root.empty() ||
        mount.kind == openwow::vfs::MountKind::kMpqArchive) {
      continue;
    }
    std::ifstream input(mount.source_root / relative,
                        std::ios::in | std::ios::binary);
    if (!input.is_open()) {
      continue;
    }
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
  }
  return std::nullopt;
}

std::optional<std::string> ReadExternalSimpleHtml(
    const openwow::vfs::VirtualFileSystem& vfs, std::string file) {
  if (file.empty()) {
    return std::nullopt;
  }
  std::replace(file.begin(), file.end(), '\\', '/');
  while (!file.empty() && file.front() == '/') {
    file.erase(file.begin());
  }

  std::vector<std::string> candidates;
  candidates.reserve(5);
  const auto add_candidate = [&](std::string candidate) {
    if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()) {
      candidates.push_back(std::move(candidate));
    }
  };
  const auto add_locale_candidates = [&](const std::string& locale) {
    if (locale.empty() || locale == "****") {
      return;
    }
    add_candidate(locale + "/" + file);
    add_candidate("Data/" + locale + "/" + file);
  };
  add_locale_candidates(openwow::ui::game::CVarSystem::Instance().GetCVar("locale"));
  add_locale_candidates(openwow::game::Localization::Get().GetLocaleName());

  add_candidate(file);

  const auto prepare_text = [](std::optional<std::string> text) {
    if (!text.has_value()) {
      return text;
    }
    if (text->size() >= 3 &&
        static_cast<unsigned char>((*text)[0]) == 0xef &&
        static_cast<unsigned char>((*text)[1]) == 0xbb &&
        static_cast<unsigned char>((*text)[2]) == 0xbf) {
      text->erase(0, 3);
    }
    return text;
  };

  for (const auto& candidate : candidates) {
    if (auto text = ReadLooseTextFile(vfs, candidate); text.has_value()) {
      return prepare_text(std::move(text));
    }
  }

  return prepare_text(vfs.ReadTextFile(file));
}

bool HasUsableTextureAnchor(
    const openwow::ui::framexml::UiFrame& frame) {
  const auto slots = openwow::ui::framexml::detail::BuildAnchorSlots(
      frame.anchors);
  return std::any_of(
      slots.begin(), slots.end(), [](const auto* anchor) {
        return anchor != nullptr &&
               !openwow::ui::framexml::detail::AnchorHasFlag(
                   *anchor,
                   openwow::ui::framexml::detail::kAnchorHiddenBit);
      });
}

void ApplyTexturePostLoadLayout(
    openwow::ui::framexml::UiFrame* frame) {
  if (frame == nullptr || ToLowerAscii(frame->kind) != "texture" ||
      frame->parent.empty() || frame->set_all_points ||
      HasUsableTextureAnchor(*frame)) {
    return;
  }

  frame->anchors.clear();
  frame->set_all_points = true;
}

void CanonicalizeNativeTextRegion(
    openwow::ui::framexml::UiFrame* frame) {
  if (frame == nullptr || frame->parent.empty() ||
      frame->region_role !=
          openwow::ui::framexml::UiFrame::RegionRole::EditBoxText) {
    return;
  }

  frame->name = GlueEditBoxTextRegionKey(frame->parent);
  frame->publish_to_lua = false;
}

bool IsDefaultTextureCoordQuad(const openwow::ui::framexml::UiTextureCoordQuad& quad) {
  return quad.upper_left.u == 0.0F && quad.upper_left.v == 0.0F &&
         quad.lower_left.u == 0.0F && quad.lower_left.v == 1.0F &&
         quad.upper_right.u == 1.0F && quad.upper_right.v == 0.0F &&
         quad.lower_right.u == 1.0F && quad.lower_right.v == 1.0F;
}

openwow::ui::framexml::UiTextureCoordQuad ResolveFrameTextureCoords(
    const openwow::ui::framexml::UiFrame& frame) {
  const bool rect_is_default =
      frame.tex_left == 0.0F && frame.tex_right == 1.0F &&
      frame.tex_top == 0.0F && frame.tex_bottom == 1.0F;
  if (!IsDefaultTextureCoordQuad(frame.tex_coords) || rect_is_default) {
    return frame.tex_coords;
  }
  return openwow::ui::framexml::UiTextureCoordQuad::FromRect(
      frame.tex_left, frame.tex_right, frame.tex_top, frame.tex_bottom);
}

std::string SubstituteTemplateRootPrefix(std::string value,
                                        const std::string& templ_root,
                                        const std::string& inst_root) {
  if (value.empty()) return value;
  std::string out = std::move(value);
  if (!templ_root.empty()) {
    if (out == templ_root) {
      out = inst_root;
    } else if (out.rfind(templ_root + ".", 0) == 0) {
      out = inst_root + out.substr(templ_root.size());
    } else if (out.rfind(templ_root, 0) == 0) {

      out = inst_root + out.substr(templ_root.size());
    }
  }
  return out;
}

std::string SubstituteParentToken(std::string value, const std::string& parent) {
  if (value.empty()) return value;
  if (parent.empty()) return value;
  std::string out = std::move(value);
  std::string::size_type pos = 0;
  while ((pos = out.find("$parent", pos)) != std::string::npos) {
    out.replace(pos, 7, parent);
    pos += parent.size();
  }
  return out;
}

std::string ResolveTemplateFrameName(
    const std::unordered_map<std::string, const openwow::ui::framexml::UiFrame*>& templ_index,
    const std::string& templ_root,
    const std::string& inst_root,
    const std::string& templ_name,
    std::unordered_map<std::string, std::string>* resolved_cache,
    int depth = 0) {
  if (templ_name.empty()) return templ_name;
  if (templ_name == templ_root) return inst_root;
  if (resolved_cache != nullptr) {
    if (const auto it = resolved_cache->find(templ_name); it != resolved_cache->end()) {
      return it->second;
    }
  }
  if (depth > 48) {
    return SubstituteParentToken(SubstituteTemplateRootPrefix(templ_name, templ_root, inst_root), inst_root);
  }

  const auto it = templ_index.find(templ_name);
  if (it == templ_index.end() || it->second == nullptr) {
    return SubstituteParentToken(SubstituteTemplateRootPrefix(templ_name, templ_root, inst_root), inst_root);
  }
  const auto& frame = *it->second;
  const std::string templ_parent = frame.parent.empty() ? templ_root : frame.parent;
  const std::string resolved_parent =
      ResolveTemplateFrameName(templ_index, templ_root, inst_root, templ_parent, resolved_cache, depth + 1);

  std::string resolved = SubstituteTemplateRootPrefix(frame.name, templ_root, inst_root);
  resolved = SubstituteParentToken(std::move(resolved), resolved_parent);
  if (resolved_cache != nullptr) {
    resolved_cache->insert_or_assign(templ_name, resolved);
  }
  return resolved;
}

std::string ResolveTemplateValueWithParentContext(std::string value,
                                                 const std::string& resolved_parent,
                                                 const std::string& templ_root,
                                                 const std::string& inst_root) {
  std::string out = SubstituteTemplateRootPrefix(std::move(value), templ_root, inst_root);
  out = SubstituteParentToken(std::move(out), resolved_parent.empty() ? inst_root : resolved_parent);
  return out;
}

void LogFrameXmlDiagnostics(const std::string& path,
                            const std::vector<std::string>& diagnostics) {
  for (const auto& diagnostic : diagnostics) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "Glue XML warning in " + path + ": " + diagnostic);
  }
}

void MergeFromTemplate(openwow::ui::framexml::UiFrame* dst,
                       const openwow::ui::framexml::UiFrame& templ) {
  if (dst == nullptr) return;
  if (dst->parent.empty() && !templ.parent.empty()) dst->parent = templ.parent;

  if ((!dst->has_id || dst->id < 0) && templ.has_id && templ.id >= 0) {
    dst->id = templ.id;
    dst->has_id = true;
  }
  if (!dst->width.has_value() && templ.width.has_value()) dst->width = templ.width;
  if (!dst->height.has_value() && templ.height.has_value()) dst->height = templ.height;
  if (!dst->auto_focus.has_value() && templ.auto_focus.has_value()) {
    dst->auto_focus = templ.auto_focus;
  }
  if (!dst->rel_width.has_value() && templ.rel_width.has_value()) dst->rel_width = templ.rel_width;
  if (!dst->rel_height.has_value() && templ.rel_height.has_value()) dst->rel_height = templ.rel_height;
  if (dst->anchors.empty() && !templ.anchors.empty()) {
    dst->anchors = templ.anchors;
    for (auto& anchor : dst->anchors) {
      if (anchor.relative_to.empty() && !dst->parent.empty()) {

        anchor.relative_to = dst->parent;
      }
    }
  }
  if (!dst->set_all_points_explicit && templ.set_all_points_explicit) {
    dst->set_all_points = templ.set_all_points;
    dst->set_all_points_explicit = true;
  } else if (!dst->set_all_points_explicit && !dst->set_all_points &&
             templ.set_all_points) {
    dst->set_all_points = true;
  }
  if (!dst->clamped_to_screen.has_value() && templ.clamped_to_screen.has_value()) {
    dst->clamped_to_screen = templ.clamped_to_screen;
  }
  if (dst->frame_strata.empty() && !templ.frame_strata.empty()) dst->frame_strata = templ.frame_strata;
  if (!dst->has_frame_level && templ.has_frame_level) {
    dst->frame_level = templ.frame_level;
    dst->has_frame_level = true;
  }
  if (dst->depth == 0.0f && templ.depth != 0.0f) dst->depth = templ.depth;
  if (dst->draw_layer.empty() && !templ.draw_layer.empty()) dst->draw_layer = templ.draw_layer;
  if (dst->draw_sublevel == 0 && templ.draw_sublevel != 0) dst->draw_sublevel = templ.draw_sublevel;
  if (!dst->top_level && templ.top_level) dst->top_level = true;
  if (!dst->toplevel_explicit && templ.toplevel_explicit) {
    dst->toplevel = templ.toplevel;
    dst->toplevel_explicit = true;
  } else if (!dst->toplevel_explicit && !templ.toplevel_explicit && !dst->toplevel &&
             templ.toplevel) {
    dst->toplevel = true;
  }
  if (!dst->protected_explicit && templ.protected_explicit) {
    dst->protected_frame = templ.protected_frame;
    dst->protected_explicit = true;
  } else if (!dst->protected_explicit && !templ.protected_explicit &&
             !dst->protected_frame && templ.protected_frame) {
    dst->protected_frame = true;
  }
  if (!dst->enable_mouse_explicit && templ.enable_mouse_explicit) {
    dst->enable_mouse = templ.enable_mouse;
    dst->enable_mouse_explicit = true;
  } else if (!dst->enable_mouse_explicit && !templ.enable_mouse_explicit &&
             !dst->enable_mouse && templ.enable_mouse) {
    dst->enable_mouse = true;
  }
  if (!dst->has_hit_rect_insets && templ.has_hit_rect_insets) {
    dst->hit_rect_inset_left = templ.hit_rect_inset_left;
    dst->hit_rect_inset_right = templ.hit_rect_inset_right;
    dst->hit_rect_inset_top = templ.hit_rect_inset_top;
    dst->hit_rect_inset_bottom = templ.hit_rect_inset_bottom;
    dst->has_hit_rect_insets = true;
  }
  if (!dst->visibility_explicit && templ.visibility_explicit) {
    dst->visible = templ.visible;
    dst->visibility_explicit = true;
  } else if (!dst->visibility_explicit && !templ.visibility_explicit && dst->visible &&
             !templ.visible) {
    dst->visible = false;
  }
  if (!dst->scroll_child_content && templ.scroll_child_content) {
    dst->scroll_child_content = true;
  }
  if (dst->parent_keys.empty()) {
    dst->parent_keys = templ.parent_keys;
  } else if (!templ.parent_keys.empty()) {
    auto merged = templ.parent_keys;
    merged.insert(merged.end(), dst->parent_keys.begin(), dst->parent_keys.end());
    dst->parent_keys = std::move(merged);
  }

  if (dst->font_style.empty() && !templ.font_style.empty()) dst->font_style = templ.font_style;
  if (dst->justify_h.empty() && !templ.justify_h.empty()) dst->justify_h = templ.justify_h;
  if (dst->justify_v.empty() && !templ.justify_v.empty()) dst->justify_v = templ.justify_v;
  if (!dst->word_wrap.has_value() && templ.word_wrap.has_value()) dst->word_wrap = templ.word_wrap;
  if (!dst->non_space_wrap.has_value() && templ.non_space_wrap.has_value()) {
    dst->non_space_wrap = templ.non_space_wrap;
  }
  if (!dst->indented_word_wrap.has_value() && templ.indented_word_wrap.has_value()) {
    dst->indented_word_wrap = templ.indented_word_wrap;
  }
  if (!dst->auto_focus.has_value() && templ.auto_focus.has_value()) {
    dst->auto_focus = templ.auto_focus;
  }
  if (dst->max_lines == 0 && templ.max_lines != 0) {
    dst->max_lines = templ.max_lines;
  }
  if (dst->text.empty() && !templ.text.empty()) dst->text = templ.text;
  if (dst->alpha_mode.empty() && !templ.alpha_mode.empty()) dst->alpha_mode = templ.alpha_mode;
  if (dst->file.empty() && !templ.file.empty()) dst->file = templ.file;
  if (dst->quest_poi_fill_texture.empty() && !templ.quest_poi_fill_texture.empty()) {
    dst->quest_poi_fill_texture = templ.quest_poi_fill_texture;
  }
  if (dst->quest_poi_border_texture.empty() && !templ.quest_poi_border_texture.empty()) {
    dst->quest_poi_border_texture = templ.quest_poi_border_texture;
  }
  if (!openwow::ui::framexml::TextureCoordinatesWereSpecified(*dst) &&
      openwow::ui::framexml::TextureCoordinatesWereSpecified(templ)) {
    dst->tex_left = templ.tex_left;
    dst->tex_right = templ.tex_right;
    dst->tex_top = templ.tex_top;
    dst->tex_bottom = templ.tex_bottom;
    dst->tex_coords =
        openwow::ui::framexml::EffectiveTextureCoordinates(templ);
    dst->has_tex_coords = true;
  }
  const bool destination_had_texture_color =
      openwow::ui::framexml::TextureColorWasSpecified(*dst);
  if (!destination_had_texture_color &&
      openwow::ui::framexml::TextureColorWasSpecified(templ)) {
    dst->color_r = templ.color_r;
    dst->color_g = templ.color_g;
    dst->color_b = templ.color_b;
    dst->color_a = templ.color_a;
    dst->has_vertex_color = true;
  }
  if (!dst->texture_alpha.has_value() && !destination_had_texture_color &&
      templ.texture_alpha.has_value()) {
    dst->texture_alpha = templ.texture_alpha;
  }
  if (!dst->gradient.enabled && templ.gradient.enabled) {
    dst->gradient = templ.gradient;
  }
  if (!dst->backdrop.has_value() && templ.backdrop.has_value()) {
    dst->backdrop = templ.backdrop;
  }
  if (dst->button_normal_font_style.empty() && !templ.button_normal_font_style.empty()) {
    dst->button_normal_font_style = templ.button_normal_font_style;
  }
  if (dst->button_disabled_font_style.empty() && !templ.button_disabled_font_style.empty()) {
    dst->button_disabled_font_style = templ.button_disabled_font_style;
  }
  if (dst->button_highlight_font_style.empty() && !templ.button_highlight_font_style.empty()) {
    dst->button_highlight_font_style = templ.button_highlight_font_style;
  }
  if (!dst->button_normal_color.has_value() && templ.button_normal_color.has_value()) {
    dst->button_normal_color = templ.button_normal_color;
  }
  if (!dst->button_disabled_color.has_value() && templ.button_disabled_color.has_value()) {
    dst->button_disabled_color = templ.button_disabled_color;
  }
  if (!dst->button_highlight_color.has_value() && templ.button_highlight_color.has_value()) {
    dst->button_highlight_color = templ.button_highlight_color;
  }
  openwow::ui::widgets::InheritMissingStatusBarDefinition(dst->status_bar,
                                                           templ.status_bar);
  if (dst->color_wheel_texture_file.empty() && !templ.color_wheel_texture_file.empty()) {
    dst->color_wheel_texture_file = templ.color_wheel_texture_file;
  }
  if (dst->color_wheel_thumb_texture_file.empty() &&
      !templ.color_wheel_thumb_texture_file.empty()) {
    dst->color_wheel_thumb_texture_file = templ.color_wheel_thumb_texture_file;
  }
  if (dst->color_value_texture_file.empty() && !templ.color_value_texture_file.empty()) {
    dst->color_value_texture_file = templ.color_value_texture_file;
  }
  if (dst->color_value_thumb_texture_file.empty() &&
      !templ.color_value_thumb_texture_file.empty()) {
    dst->color_value_thumb_texture_file = templ.color_value_thumb_texture_file;
  }
  if (!dst->tile_x_explicit && (templ.tile_x_explicit || templ.tile_x)) {
    dst->tile_x = templ.tile_x;
    dst->tile_x_explicit = true;
  }
  if (!dst->tile_y_explicit && (templ.tile_y_explicit || templ.tile_y)) {
    dst->tile_y = templ.tile_y;
    dst->tile_y_explicit = true;
  }
  if (dst->tile_size_x == 0 && templ.tile_size_x != 0) dst->tile_size_x = templ.tile_size_x;
  if (dst->tile_size_y == 0 && templ.tile_size_y != 0) dst->tile_size_y = templ.tile_size_y;
  if (dst->slice == openwow::ui::framexml::TextureSlice::kNone && templ.slice != openwow::ui::framexml::TextureSlice::kNone) {
    dst->slice = templ.slice;
  }
  if (dst->slice_edge_size_px == 0 && templ.slice_edge_size_px != 0) dst->slice_edge_size_px = templ.slice_edge_size_px;
  if (!dst->has_fog_color && templ.has_fog_color) {
    dst->fog_r = templ.fog_r;
    dst->fog_g = templ.fog_g;
    dst->fog_b = templ.fog_b;
    dst->has_fog_color = true;
  }
  if (!dst->has_fog_near && templ.has_fog_near) {
    dst->fog_near = templ.fog_near;
    dst->has_fog_near = true;
  }
  if (!dst->has_fog_far && templ.has_fog_far) {
    dst->fog_far = templ.fog_far;
    dst->has_fog_far = true;
  }
  if (!dst->has_glow && templ.has_glow) {
    dst->glow = templ.glow;
    dst->has_glow = true;
  }
  if (!dst->has_model_scale && templ.has_model_scale) {
    dst->model_scale = templ.model_scale;
    dst->has_model_scale = true;
  }
  if (!dst->has_model_sequence && templ.has_model_sequence) {
    dst->model_sequence = templ.model_sequence;
    dst->has_model_sequence = true;
  }
  if (!dst->has_model_camera && templ.has_model_camera) {
    dst->model_camera = templ.model_camera;
    dst->has_model_camera = true;
  }
  if (!dst->has_model_sequence_time && templ.has_model_sequence_time) {
    dst->model_sequence_time_ms = templ.model_sequence_time_ms;
    dst->has_model_sequence_time = true;
  }
  if (!dst->has_model_position && templ.has_model_position) {
    dst->model_x = templ.model_x;
    dst->model_y = templ.model_y;
    dst->model_z = templ.model_z;
    dst->has_model_position = true;
  }
  if (!dst->has_model_facing && templ.has_model_facing) {
    dst->model_facing_rad = templ.model_facing_rad;
    dst->has_model_facing = true;
  }
  if (dst->initial_attributes.empty()) {
    dst->initial_attributes = templ.initial_attributes;
  } else if (!templ.initial_attributes.empty()) {
    auto merged = templ.initial_attributes;
    merged.insert(merged.end(), dst->initial_attributes.begin(), dst->initial_attributes.end());
    dst->initial_attributes = std::move(merged);
  }
  if (dst->script_handlers.empty()) {
    dst->script_handlers = templ.script_handlers;
  } else if (!templ.script_handlers.empty()) {
    auto merged = templ.script_handlers;
    merged.insert(merged.end(), dst->script_handlers.begin(), dst->script_handlers.end());
    dst->script_handlers = std::move(merged);
  }
  if (dst->animation_groups.empty()) {
    dst->animation_groups = templ.animation_groups;
  } else if (!templ.animation_groups.empty()) {
    auto merged = templ.animation_groups;
    merged.insert(merged.end(), dst->animation_groups.begin(), dst->animation_groups.end());
    dst->animation_groups = std::move(merged);
  }
}

bool IsGlueFrameLikeKind(const std::string& kind) {
  return !openwow::text::EqualsIgnoreCaseAscii(kind, "FontString") &&
         !openwow::text::EqualsIgnoreCaseAscii(kind, "Texture") &&
         !openwow::text::EqualsIgnoreCaseAscii(kind, "Line");
}

int ResolveInitialFrameLevel(
    const std::unordered_map<std::string, GlueWidgetState>& widgets,
    const openwow::ui::framexml::UiFrame& frame) {
  if (frame.has_frame_level || !IsGlueFrameLikeKind(frame.kind) || frame.parent.empty()) {
    return frame.frame_level;
  }

  const auto parent_it = widgets.find(frame.parent);
  if (parent_it == widgets.end() || !IsGlueFrameLikeKind(parent_it->second.kind)) {
    return frame.frame_level;
  }
  return parent_it->second.frame_level + 1;
}

std::string ResolveInitialFrameStrata(
    const std::unordered_map<std::string, GlueWidgetState>& widgets,
    const openwow::ui::framexml::UiFrame& frame) {
  if (!frame.frame_strata.empty() || !IsGlueFrameLikeKind(frame.kind) ||
      frame.parent.empty()) {
    return frame.frame_strata;
  }

  const auto parent_it = widgets.find(frame.parent);
  if (parent_it == widgets.end() ||
      !IsGlueFrameLikeKind(parent_it->second.kind)) {
    return frame.frame_strata;
  }

  return parent_it->second.frame_strata.empty() ? "MEDIUM"
                                                : parent_it->second.frame_strata;
}

GlueWidgetState BuildWidgetStateFromFrame(
    const openwow::ui::framexml::UiFrame& frame,
    const std::unordered_map<std::string, GlueWidgetState>& widgets,
    bool virtual_template) {
  const auto kind_lower = ToLowerAscii(frame.kind);
  return GlueWidgetState{
      .name = frame.name,
      .lua_name = frame.lua_name,
      .kind = frame.kind,
      .parent = frame.parent,
      .inherits = frame.inherits,
      .id = frame.has_id && frame.id >= 0 ? frame.id : 0,
      .password = frame.password,
      .max_letters = frame.max_letters,
      .auto_focus = frame.auto_focus.value_or(true),
      .texture_file = (kind_lower == "texture" || kind_lower == "movieframe") ? frame.file
                                                                                : std::string(),
      .model_file = (kind_lower == "model" || kind_lower == "modelffx") ? frame.file
                                                                          : std::string(),
      .font_style = frame.font_style,
      .justify_h = frame.justify_h,
      .justify_v = frame.justify_v,
      .alpha_mode = frame.alpha_mode,
      .draw_layer = frame.draw_layer,
      .draw_sublevel = frame.draw_sublevel,
      .frame_strata = ResolveInitialFrameStrata(widgets, frame),
      .frame_level = ResolveInitialFrameLevel(widgets, frame),
      .depth = frame.depth,
      .protected_frame = frame.protected_frame,
      .clamped_to_screen = frame.clamped_to_screen.value_or(false),
      .x = 0,
      .y = 0,
      .width = static_cast<int>(frame.width.value_or(0.0f)),
      .height = static_cast<int>(frame.height.value_or(0.0f)),
      .alpha = 1.0F,
      .tex_left = frame.tex_left,
      .tex_right = frame.tex_right,
      .tex_top = frame.tex_top,
      .tex_bottom = frame.tex_bottom,
      .tex_coords = ResolveFrameTextureCoords(frame),
      .color_r = frame.color_r,
      .color_g = frame.color_g,
      .color_b = frame.color_b,
      .color_a = frame.texture_alpha.value_or(frame.color_a),
      .has_vertex_color = frame.has_vertex_color,
      .gradient = frame.gradient,
      .button_normal_font_style = frame.button_normal_font_style,
      .button_disabled_font_style = frame.button_disabled_font_style,
      .button_highlight_font_style = frame.button_highlight_font_style,
      .button_normal_color = frame.button_normal_color,
      .button_disabled_color = frame.button_disabled_color,
      .button_highlight_color = frame.button_highlight_color,
      .status_bar = frame.status_bar,
      .color_wheel_texture_file = frame.color_wheel_texture_file,
      .color_wheel_thumb_texture_file = frame.color_wheel_thumb_texture_file,
      .color_value_texture_file = frame.color_value_texture_file,
      .color_value_thumb_texture_file = frame.color_value_thumb_texture_file,
      .fog_r = frame.fog_r,
      .fog_g = frame.fog_g,
      .fog_b = frame.fog_b,
      .has_fog_color = frame.has_fog_color,
      .fog_near = frame.fog_near,
      .has_fog_near = frame.has_fog_near,
      .fog_far = frame.fog_far,
      .has_fog_far = frame.has_fog_far,
      .glow = frame.glow,
      .has_glow = frame.has_glow,
      .model_scale = frame.model_scale,
      .has_model_scale = frame.has_model_scale,
      .model_sequence = frame.model_sequence,
      .has_model_sequence = frame.has_model_sequence,
      .model_camera = frame.model_camera,
      .has_model_camera = frame.has_model_camera,
      .model_sequence_time_ms = frame.model_sequence_time_ms,
      .has_model_sequence_time = frame.has_model_sequence_time,
      .model_x = frame.model_x,
      .model_y = frame.model_y,
      .model_z = frame.model_z,
      .has_model_position = frame.has_model_position,
      .model_facing_rad = frame.model_facing_rad,
      .has_model_facing = frame.has_model_facing,
      .tile_x = frame.tile_x,
      .tile_y = frame.tile_y,
      .tile_size_x = frame.tile_size_x,
      .tile_size_y = frame.tile_size_y,
      .slice = frame.slice,
      .slice_edge_size_px = frame.slice_edge_size_px,
      .enabled = true,
      .mouse_enabled = frame.enable_mouse,
      .text = frame.text,
      .text_spacing_stored = frame.text_spacing_stored,
      .text_height_stored = frame.text_height_stored,
      .max_lines = frame.max_lines,
      .visible = frame.visible,
      .virtual_template = virtual_template,
      .scroll_child_content = frame.scroll_child_content,
      .hit_rect_inset_left = frame.hit_rect_inset_left,
      .hit_rect_inset_right = frame.hit_rect_inset_right,
      .hit_rect_inset_top = frame.hit_rect_inset_top,
      .hit_rect_inset_bottom = frame.hit_rect_inset_bottom,
      .has_hit_rect_insets = frame.has_hit_rect_insets,
      .text_inset_left = frame.text_inset_left,
      .text_inset_right = frame.text_inset_right,
      .text_inset_top = frame.text_inset_top,
      .text_inset_bottom = frame.text_inset_bottom,
      .has_text_insets = frame.has_text_insets,
      .word_wrap = frame.word_wrap.value_or(true),
      .non_space_wrap = frame.non_space_wrap.value_or(false),
      .indented_word_wrap = frame.indented_word_wrap.value_or(false),
      .backdrop = frame.backdrop,
      .publish_to_lua = frame.publish_to_lua,
      .region_role = frame.region_role,
  };
}

const openwow::ui::framexml::UiFrame* FindTemplateRootFrame(
    const std::vector<openwow::ui::framexml::UiFrame>& frames,
    const std::string& template_root) {
  for (const auto& frame : frames) {
    if (frame.name == template_root) {
      return &frame;
    }
  }
  return nullptr;
}

std::unordered_map<std::string, const openwow::ui::framexml::UiFrame*> BuildTemplateIndex(
    const std::vector<openwow::ui::framexml::UiFrame>& frames) {
  std::unordered_map<std::string, const openwow::ui::framexml::UiFrame*> out;
  out.reserve(frames.size() * 2);
  for (const auto& frame : frames) {
    if (!frame.name.empty()) {
      out.insert_or_assign(frame.name, &frame);
    }
  }
  return out;
}

openwow::ui::framexml::UiFrame CloneTemplateFrameForInstance(
    const openwow::ui::framexml::UiFrame& template_frame,
    const std::unordered_map<std::string, const openwow::ui::framexml::UiFrame*>& template_index,
    const std::string& template_root,
    const std::string& instance_root) {
  openwow::ui::framexml::UiFrame clone = template_frame;
  clone.virtual_template = false;

  std::unordered_map<std::string, std::string> resolved_cache;
  resolved_cache.reserve(template_index.size() * 2);
  const std::string resolved_parent =
      ResolveTemplateFrameName(template_index, template_root, instance_root,
                               clone.parent.empty() ? template_root : clone.parent,
                               &resolved_cache);
  clone.parent = resolved_parent;
  clone.name = ResolveTemplateFrameName(template_index, template_root, instance_root,
                                        clone.name, &resolved_cache);

  clone.lua_name = ResolveTemplateFrameName(template_index, template_root, instance_root,
                                            clone.lua_name, &resolved_cache);
  clone.inherits = ResolveTemplateValueWithParentContext(std::move(clone.inherits),
                                                         resolved_parent,
                                                         template_root,
                                                         instance_root);
  clone.file = ResolveTemplateValueWithParentContext(std::move(clone.file),
                                                     resolved_parent,
                                                     template_root,
                                                     instance_root);
  clone.font_style = ResolveTemplateValueWithParentContext(std::move(clone.font_style),
                                                           resolved_parent,
                                                           template_root,
                                                           instance_root);
  clone.font_reference = ResolveTemplateValueWithParentContext(
      std::move(clone.font_reference), resolved_parent, template_root,
      instance_root);
  for (auto& anchor : clone.anchors) {
    if (anchor.relative_to.empty()) {
      anchor.relative_to = resolved_parent;
    } else {
      anchor.relative_to =
          ResolveTemplateValueWithParentContext(std::move(anchor.relative_to),
                                                resolved_parent,
                                                template_root,
                                                instance_root);
    }
  }
  for (auto& attribute : clone.initial_attributes) {
    attribute.name = ResolveTemplateValueWithParentContext(std::move(attribute.name),
                                                           resolved_parent,
                                                           template_root,
                                                           instance_root);
    attribute.value = ResolveTemplateValueWithParentContext(std::move(attribute.value),
                                                            resolved_parent,
                                                            template_root,
                                                            instance_root);
  }
  return clone;
}

void AppendInheritedTemplateFrames(
    const std::unordered_map<std::string, std::vector<openwow::ui::framexml::UiFrame>>& templates,
    const std::string& template_root,
    const std::string& instance_root,
    openwow::ui::framexml::UiFrame* instance_frame,
    std::unordered_set<std::string>* existing_names,
    std::unordered_map<std::string, std::string>* template_source_by_instance,
    std::vector<openwow::ui::framexml::UiFrame>* out,
    std::unordered_set<std::string>* stack,
    const std::unordered_set<std::string>* registered_names = nullptr) {
  if (template_root.empty() || instance_root.empty() || existing_names == nullptr ||
      template_source_by_instance == nullptr || out == nullptr || stack == nullptr) {
    return;
  }
  if (!stack->insert(template_root).second) {
    return;
  }

  const auto templ_it = templates.find(template_root);
  if (templ_it == templates.end()) {
    stack->erase(template_root);
    return;
  }

  const auto* root_frame = FindTemplateRootFrame(templ_it->second, template_root);
  if (root_frame != nullptr) {
    for (const auto& inherited_template : openwow::ui::framexml::SplitTemplateList(
             root_frame->inherits, openwow::ui::framexml::TemplateListSyntax::kCommaSeparated)) {
      AppendInheritedTemplateFrames(templates, inherited_template, instance_root, nullptr,
                                    existing_names, template_source_by_instance, out, stack,
                                    registered_names);
    }
    if (instance_frame != nullptr) {
      MergeFromTemplate(instance_frame, *root_frame);
    }
  }

  const auto template_index = BuildTemplateIndex(templ_it->second);
  std::unordered_map<std::string, std::string> template_scoped_cache;
  template_scoped_cache.reserve(templ_it->second.size() * 2);
  for (const auto& template_child : templ_it->second) {
    if (template_child.name.empty() || template_child.name == template_root) {
      continue;
    }

    auto clone = CloneTemplateFrameForInstance(template_child, template_index,
                                               template_root, instance_root);
    if (clone.name.empty()) {
      continue;
    }
    const auto clone_key = ToLowerAscii(clone.name);
    if (existing_names->find(clone_key) != existing_names->end() ||
        (registered_names != nullptr && registered_names->contains(clone_key))) {
      continue;
    }

    const std::string template_source_name =
        ResolveTemplateFrameName(template_index, template_root, template_root,
                                 template_child.name, &template_scoped_cache);
    if (!template_source_name.empty()) {
      template_source_by_instance->insert_or_assign(clone.name, template_source_name);
    }
    existing_names->insert(clone_key);
    out->push_back(std::move(clone));
  }

  stack->erase(template_root);
}

void ResolveTemplateChains(
    std::unordered_map<std::string, std::vector<openwow::ui::framexml::UiFrame>>& templates) {
  std::unordered_set<std::string> resolved;
  std::function<void(const std::string&, std::unordered_set<std::string>&)> resolve;
  resolve = [&](const std::string& root, std::unordered_set<std::string>& stack) {
    if (root.empty() || resolved.count(root) || stack.count(root)) return;
    stack.insert(root);
    auto it = templates.find(root);
    if (it == templates.end()) { stack.erase(root); return; }

    for (auto& frame : it->second) {
      if (frame.name == root && !frame.inherits.empty()) {
        const auto parent_names = openwow::ui::framexml::SplitTemplateList(
            frame.inherits,
            openwow::ui::framexml::TemplateListSyntax::kCommaSeparated);
        for (const auto& parent_name : parent_names) {
          resolve(parent_name, stack);
        }
        for (auto parent_name = parent_names.rbegin();
             parent_name != parent_names.rend(); ++parent_name) {
          auto parent_it = templates.find(*parent_name);
          if (parent_it == templates.end()) continue;
          for (const auto& parent_frame : parent_it->second) {
            if (parent_frame.name == *parent_name) {
              MergeFromTemplate(&frame, parent_frame);
              break;
            }
          }
        }
        break;
      }
    }
    resolved.insert(root);
    stack.erase(root);
  };

  for (auto& [root, _] : templates) {
    std::unordered_set<std::string> stack;
    resolve(root, stack);
  }
}

}

void GlueWidgetRuntime::SetXmlTextResolver(XmlTextResolver resolver) {
  xml_text_resolver_ = std::move(resolver);
}

void GlueWidgetRuntime::ResolveXmlText(
    openwow::ui::framexml::UiFrame* frame) const {
  if (frame == nullptr || frame->text.empty() || !xml_text_resolver_) {
    return;
  }
  if (auto resolved = xml_text_resolver_(frame->text); resolved.has_value()) {
    frame->text = std::move(*resolved);
  }
}

void GlueWidgetRuntime::LoadWidgetsFromXml(const openwow::vfs::VirtualFileSystem& vfs,
                                           const std::vector<std::string>& xml_candidates) {
  source_widget_order_.clear();
  templates_.clear();
  layout_frames_by_name_.clear();
  widgets_.clear();
  widget_names_lower_.clear();
  capability_unavailable_widgets_.clear();
  children_by_parent_.clear();
  inheritors_by_template_.clear();
  lifecycle_visibility_overrides_.clear();
  owned_text_region_by_widget_.clear();
  font_string_metric_state_.clear();
  dirty_font_string_metric_names_set_.clear();
  dirty_font_string_metric_names_.clear();
  layout_resolve_count_ = 0;
  resolved_layout_widgets_.clear();
  widget_registration_order_.clear();
  widget_props_.clear();
  animated_geometry_roots_.clear();
  template_source_by_instance_.clear();
  pending_scroll_range_changed_events_.clear();
  MarkVisibleWidgetsDirty();

  std::vector<openwow::ui::framexml::UiFrame> all_frames;
  all_frames.reserve(2048);

  for (const auto& candidate : xml_candidates) {
    const auto xml = vfs.ReadTextFile(candidate);
    if (!xml) {
      if (vfs.Exists(candidate)) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "Glue XML candidate exists but could not be read: " + candidate);
      }
      continue;
    }
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "Glue XML candidate loaded: " + candidate + " bytes="
                           + std::to_string(xml->size()));
    auto parsed = openwow::ui::framexml::ParseFrameXml(*xml);
    if (!parsed.ok) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "Glue XML parse failed: " + candidate);
      continue;
    }
    LogFrameXmlDiagnostics(candidate, parsed.diagnostics);
    if (parsed.frames.empty()) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                         "Glue XML parsed with 0 widgets: " + candidate);
      continue;
    }

    all_frames.insert(all_frames.end(), parsed.frames.begin(), parsed.frames.end());
  }

  if (all_frames.empty()) {
    layout_dirty_ = true;
    return;
  }

  std::unordered_map<std::string, std::size_t> idx;
  idx.reserve(all_frames.size());
  for (std::size_t i = 0; i < all_frames.size(); ++i) {
    if (!all_frames[i].name.empty()) {
      idx.insert_or_assign(all_frames[i].name, i);
    }
  }

  std::unordered_map<std::string, std::string> template_root_by_name;
  template_root_by_name.reserve(idx.size());
  std::unordered_set<std::string> declared_template_roots;
  declared_template_roots.reserve(idx.size());
  for (const auto& frame : all_frames) {
    if (!frame.name.empty() && frame.virtual_template) {
      declared_template_roots.insert(frame.name);
    }
  }
  auto find_template_root = [&](const openwow::ui::framexml::UiFrame& frame) -> std::string {
    if (frame.name.empty()) {
      return {};
    }
    if (declared_template_roots.find(frame.name) != declared_template_roots.end()) {
      return frame.name;
    }
    std::string cur = frame.parent;
    while (!cur.empty()) {
      const auto it = idx.find(cur);
      if (it == idx.end()) break;
      const auto& parent = all_frames[it->second];
      if (!parent.name.empty()
          && declared_template_roots.find(parent.name) != declared_template_roots.end()) {
        return parent.name;
      }
      cur = parent.parent;
    }
    return {};
  };

  for (auto& frame : all_frames) {
    if (frame.name.empty()) continue;
    const auto root = find_template_root(frame);
    if (!root.empty() && root != frame.name) {
      frame.virtual_template = true;
    }
    template_root_by_name.insert_or_assign(frame.name, root);
  }

  std::unordered_map<std::string, std::vector<openwow::ui::framexml::UiFrame>> templates;
  for (const auto& frame : all_frames) {
    if (frame.name.empty()) continue;
    const auto it = template_root_by_name.find(frame.name);
    if (it == template_root_by_name.end() || it->second.empty()) {
      continue;
    }
    templates[it->second].push_back(frame);
  }

  ResolveTemplateChains(templates);
  templates_ = templates;

  std::vector<openwow::ui::framexml::UiFrame> expanded = all_frames;
  std::unordered_set<std::string> existing_names;
  existing_names.reserve(idx.size() * 2);
  for (const auto& f : expanded) {
    if (!f.name.empty()) existing_names.insert(ToLowerAscii(f.name));
  }

  std::unordered_set<std::string> expanded_inherits_done;
  expanded_inherits_done.reserve(expanded.size() * 2);

  for (std::size_t i = 0; i < expanded.size(); ++i) {
    auto& frame = expanded[i];
    if (frame.name.empty() || frame.virtual_template || frame.inherits.empty()) {
      continue;
    }
    const std::string key = ToLowerAscii(frame.name);
    if (!expanded_inherits_done.insert(key).second) {
      continue;
    }

    std::vector<openwow::ui::framexml::UiFrame> insertions;
    insertions.reserve(64);

    const auto inherits_list = openwow::ui::framexml::SplitTemplateList(
        frame.inherits, openwow::ui::framexml::TemplateListSyntax::kCommaSeparated);
    for (auto templ_name = inherits_list.rbegin();
         templ_name != inherits_list.rend(); ++templ_name) {
      const auto templ = templates.find(*templ_name);
      if (templ == templates.end()) continue;
      if (const auto* root = FindTemplateRootFrame(templ->second, *templ_name);
          root != nullptr) {
        MergeFromTemplate(&frame, *root);
      }
    }
    for (const auto& templ_name : inherits_list) {
      std::unordered_set<std::string> stack;
      AppendInheritedTemplateFrames(templates, templ_name, frame.name, nullptr,
                                    &existing_names, &template_source_by_instance_,
                                    &insertions, &stack);
    }

    if (!insertions.empty()) {
      expanded.insert(expanded.begin() + static_cast<std::ptrdiff_t>(i + 1),
                      std::make_move_iterator(insertions.begin()),
                      std::make_move_iterator(insertions.end()));
    }
  }

  for (auto& frame : expanded) {
    ApplyTexturePostLoadLayout(&frame);
    CanonicalizeNativeTextRegion(&frame);
    ResolveXmlText(&frame);
  }

  source_widget_order_.clear();
  source_widget_order_.reserve(expanded.size());
  for (const auto& frame : expanded) {
    if (!frame.name.empty() && !frame.virtual_template) {
      source_widget_order_.push_back(frame.name);
    }
  }

  struct PendingText {
    std::string widget;
    std::string text;
  };
  std::vector<PendingText> pending_text;
  pending_text.reserve(32);

  for (const auto& frame : expanded) {
    if (frame.name.empty() || frame.virtual_template) {
      continue;
    }
    const auto kind_lower = ToLowerAscii(frame.kind);

    layout_frames_by_name_.insert_or_assign(frame.name, frame);
    RegisterWidget(BuildWidgetStateFromFrame(frame, widgets_, frame.virtual_template));

    if (frame.max_letters > 0) {
      SetMaxLetters(frame.name, frame.max_letters);
    }

    if (!frame.text.empty()
        && (kind_lower == "button" || kind_lower == "checkbutton" || kind_lower == "editbox")) {
      pending_text.push_back(PendingText{.widget = frame.name, .text = frame.text});
    } else if (kind_lower == "simplehtml" && !frame.file.empty()) {
      if (auto html = ReadExternalSimpleHtml(vfs, frame.file); html.has_value()) {
        pending_text.push_back(PendingText{.widget = frame.name, .text = std::move(*html)});
      }
    }
  }

  for (const auto& entry : pending_text) {
    SetText(entry.widget, entry.text);
  }
  layout_dirty_ = true;
}

void GlueWidgetRuntime::ClearAll() {
  const bool had_focus_owner = !focused_widget_.empty();
  source_widget_order_.clear();
  templates_.clear();
  layout_frames_by_name_.clear();
  widgets_.clear();
  widget_names_lower_.clear();
  children_by_parent_.clear();
  inheritors_by_template_.clear();
  lifecycle_visibility_overrides_.clear();
  owned_text_region_by_widget_.clear();
  font_string_metric_state_.clear();
  dirty_font_string_metric_names_set_.clear();
  dirty_font_string_metric_names_.clear();
  layout_resolve_count_ = 0;
  widget_registration_order_.clear();
  model_ffx_widgets_.clear();
  widget_props_.clear();
  template_source_by_instance_.clear();
  next_anonymous_widget_id_ = 1;
  focused_widget_.clear();
  cached_cursor_position_.reset();
  pending_cursor_events_.clear();
  pending_animation_finished_events_.clear();
  pending_scroll_range_changed_events_.clear();
  ++visibility_revision_;
  layout_dirty_ = true;
  deferred_hit_test_refresh_ = true;
  MarkVisibleWidgetsDirty();
  if (had_focus_owner && focus_owner_changed_callback_) {
    focus_owner_changed_callback_();
  }
}

std::vector<openwow::ui::framexml::UiFrame>
GlueWidgetRuntime::ExpandFrameDeclarations(
    std::vector<openwow::ui::framexml::UiFrame> frames) {
  std::unordered_map<std::string, std::size_t> frame_index;
  frame_index.reserve(frames.size());
  for (std::size_t i = 0; i < frames.size(); ++i) {
    if (!frames[i].name.empty()) {
      frame_index.insert_or_assign(frames[i].name, i);
    }
  }

  std::unordered_set<std::string> declared_template_roots;
  for (const auto& frame : frames) {
    if (!frame.name.empty() && frame.virtual_template) {
      declared_template_roots.insert(frame.name);
    }
  }

  const auto find_template_root =
      [&](const openwow::ui::framexml::UiFrame& frame) {
        if (frame.name.empty()) {
          return std::string{};
        }
        if (declared_template_roots.contains(frame.name)) {
          return frame.name;
        }

        std::string current = frame.parent;
        while (!current.empty()) {
          const auto it = frame_index.find(current);
          if (it == frame_index.end()) {
            break;
          }
          if (declared_template_roots.contains(frames[it->second].name)) {
            return frames[it->second].name;
          }
          current = frames[it->second].parent;
        }
        return std::string{};
      };

  std::unordered_map<std::string, std::string> template_root_by_name;
  for (auto& frame : frames) {
    if (frame.name.empty()) {
      continue;
    }
    const std::string root = find_template_root(frame);
    if (!root.empty() && root != frame.name) {
      frame.virtual_template = true;
    }
    template_root_by_name.insert_or_assign(frame.name, root);
  }

  for (const auto& frame : frames) {
    if (frame.name.empty()) {
      continue;
    }
    const auto root = template_root_by_name.find(frame.name);
    if (root != template_root_by_name.end() && !root->second.empty()) {
      templates_[root->second].push_back(frame);
    }
  }
  ResolveTemplateChains(templates_);

  std::unordered_set<std::string> existing_names;
  for (const auto& entry : widgets_) {
    existing_names.insert(ToLowerAscii(entry.first));
  }
  for (const auto& frame : frames) {
    if (!frame.name.empty()) {
      existing_names.insert(ToLowerAscii(frame.name));
    }
  }

  std::vector<openwow::ui::framexml::UiFrame> expanded = std::move(frames);
  std::unordered_set<std::string> expanded_inheritance;
  for (std::size_t i = 0; i < expanded.size(); ++i) {
    auto& frame = expanded[i];
    if (frame.name.empty() || frame.virtual_template || frame.inherits.empty() ||
        !expanded_inheritance.insert(ToLowerAscii(frame.name)).second) {
      continue;
    }

    std::vector<openwow::ui::framexml::UiFrame> inherited_frames;
    const auto inherited_templates = openwow::ui::framexml::SplitTemplateList(
        frame.inherits,
        openwow::ui::framexml::TemplateListSyntax::kCommaSeparated);
    for (auto template_name = inherited_templates.rbegin();
         template_name != inherited_templates.rend(); ++template_name) {
      const auto templ = templates_.find(*template_name);
      if (templ == templates_.end()) continue;
      if (const auto* root = FindTemplateRootFrame(templ->second, *template_name);
          root != nullptr) {
        MergeFromTemplate(&frame, *root);
      }
    }
    for (const auto& template_name : inherited_templates) {
      std::unordered_set<std::string> stack;
      AppendInheritedTemplateFrames(
          templates_, template_name, frame.name, nullptr, &existing_names,
          &template_source_by_instance_, &inherited_frames, &stack);
    }

    if (!inherited_frames.empty()) {
      expanded.insert(
          expanded.begin() + static_cast<std::ptrdiff_t>(i + 1),
          std::make_move_iterator(inherited_frames.begin()),
          std::make_move_iterator(inherited_frames.end()));
    }
  }

  for (auto& frame : expanded) {
    ApplyTexturePostLoadLayout(&frame);
    CanonicalizeNativeTextRegion(&frame);
  }
  return expanded;
}

PreparedXmlResult GlueWidgetRuntime::PrepareXml(
    const openwow::vfs::VirtualFileSystem& ,
    const std::string& xml_text) {
  PreparedXmlResult result;

  auto parsed = openwow::ui::framexml::ParseFrameXml(xml_text);
  if (!parsed.ok) {
    result.error = "ParseFrameXml failed";
    return result;
  }
  LogFrameXmlDiagnostics("<inline>", parsed.diagnostics);
  if (parsed.frames.empty()) {
    result.ok = true;
    return result;
  }

  auto expanded = ExpandFrameDeclarations(std::move(parsed.frames));

  std::unordered_set<std::string> local_names;
  local_names.reserve(expanded.size());
  for (const auto& f : expanded) {
    if (!f.name.empty()) local_names.insert(f.name);
  }

  std::unordered_map<std::string, std::size_t> expanded_idx;
  expanded_idx.reserve(expanded.size());
  for (std::size_t i = 0; i < expanded.size(); ++i) {
    if (!expanded[i].name.empty()) expanded_idx.insert_or_assign(expanded[i].name, i);
  }

  auto find_top_level_fast = [&](const openwow::ui::framexml::UiFrame& frame) -> std::string {
    if (frame.name.empty()) return {};
    std::string current = frame.name;
    std::unordered_set<std::string> visited;
    while (true) {
      visited.insert(current);
      auto eit = expanded_idx.find(current);
      if (eit == expanded_idx.end()) return current;
      const auto& parent_name = expanded[eit->second].parent;
      if (parent_name.empty() || !local_names.count(parent_name) || visited.count(parent_name)) {
        return current;
      }
      current = parent_name;
    }
  };

  std::vector<std::string> group_order;
  std::unordered_map<std::string, PreparedFrameGroup> group_map;
  group_order.reserve(64);

  for (const auto& frame : expanded) {
    if (frame.name.empty()) continue;
    const std::string top = find_top_level_fast(frame);
    if (top.empty()) continue;

    auto git = group_map.find(top);
    if (git == group_map.end()) {
      group_order.push_back(top);
      PreparedFrameGroup grp;
      grp.top_level_name = top;

      auto tit = expanded_idx.find(top);
      if (tit != expanded_idx.end()) {
        grp.is_virtual = expanded[tit->second].virtual_template;
      }
      grp.frames.push_back(frame);
      group_map.insert_or_assign(top, std::move(grp));
    } else {
      git->second.frames.push_back(frame);
    }
  }

  result.groups.reserve(group_order.size());
  for (const auto& name : group_order) {
    result.groups.push_back(std::move(group_map[name]));
  }

  result.ok = true;
  return result;
}

GlueWidgetRuntime::RegisteredFrameGroup GlueWidgetRuntime::RegisterFrameGroup(
    std::vector<openwow::ui::framexml::UiFrame> frames,
    const openwow::vfs::VirtualFileSystem* vfs) {
  RegisteredFrameGroup group_result;
  using Clock = std::chrono::steady_clock;
  const auto started = Clock::now();
  std::chrono::nanoseconds build_time{};
  std::chrono::nanoseconds html_time{};
  std::chrono::nanoseconds layout_insert_time{};
  std::chrono::nanoseconds widget_register_time{};

  struct PendingText { std::string widget; std::string text; };
  std::vector<PendingText> pending_text;

  for (auto& frame : frames) {
    if (frame.name.empty() || frame.virtual_template) continue;
    ApplyTexturePostLoadLayout(&frame);
    CanonicalizeNativeTextRegion(&frame);
    ResolveXmlText(&frame);
    const std::string name = frame.name;
    const bool is_new_widget = widgets_.find(name) == widgets_.end();
    const auto kind_lower = ToLowerAscii(frame.kind);
    const auto build_started = Clock::now();
    auto widget = BuildWidgetStateFromFrame(frame, widgets_, frame.virtual_template);
    build_time += Clock::now() - build_started;
    const int max_letters = frame.max_letters;

    group_result.registered.push_back(name);
    if (is_new_widget) {
      group_result.newly_created.push_back(name);
      source_widget_order_.push_back(name);
    }

    if (!frame.text.empty()
        && (kind_lower == "button" || kind_lower == "checkbutton" || kind_lower == "editbox")) {
      pending_text.push_back(PendingText{.widget = name, .text = frame.text});
    } else if (vfs != nullptr && kind_lower == "simplehtml" && !frame.file.empty()) {
      const auto html_started = Clock::now();
      if (auto html = ReadExternalSimpleHtml(*vfs, frame.file); html.has_value()) {
        pending_text.push_back(PendingText{.widget = name, .text = std::move(*html)});
      }
      html_time += Clock::now() - html_started;
    }

    const auto layout_insert_started = Clock::now();
    layout_frames_by_name_.insert_or_assign(name, std::move(frame));
    layout_insert_time += Clock::now() - layout_insert_started;
    const auto widget_register_started = Clock::now();
    RegisterWidget(std::move(widget));
    widget_register_time += Clock::now() - widget_register_started;
    if (max_letters > 0) SetMaxLetters(name, max_letters);
  }

  for (const auto& entry : pending_text) SetText(entry.widget, entry.text);
  layout_dirty_ = true;
  const auto elapsed = Clock::now() - started;
  if (elapsed >= std::chrono::milliseconds(50)) {
    const auto milliseconds = [](const auto duration) {
      return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    };
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kInfo,
        "Glue frame group timing: widgets=" + std::to_string(group_result.registered.size()) +
            " total_ms=" + std::to_string(milliseconds(elapsed)) +
            " build_ms=" + std::to_string(milliseconds(build_time)) +
            " html_ms=" + std::to_string(milliseconds(html_time)) +
            " layout_ms=" + std::to_string(milliseconds(layout_insert_time)) +
            " register_ms=" + std::to_string(milliseconds(widget_register_time)));
  }
  return group_result;
}

std::vector<std::string> GlueWidgetRuntime::InstantiateTemplate(
    const std::string& instance_root, const std::string& template_root) {
  std::vector<std::string> created;
  created.reserve(64);
  if (instance_root.empty() || template_root.empty()) {
    return created;
  }
  auto inst_frame_it = layout_frames_by_name_.find(instance_root);
  if (inst_frame_it == layout_frames_by_name_.end()) {
    return created;
  }

  const std::string parent_lua_scope =
      NearestLuaName(inst_frame_it->second.parent);

  auto root = inst_frame_it->second;
  root.inherits = template_root;
  auto plan = openwow::ui::game::runtime::BuildExpandedFramePlan(
      std::move(root), {}, templates_, parent_lua_scope);
  if (plan.frames.empty()) {
    return created;
  }

  std::vector<std::string> runtime_keys(plan.frames.size());
  runtime_keys[0] = instance_root;
  std::unordered_set<std::string> allocated_keys;
  allocated_keys.reserve(plan.frames.size() * 2);
  allocated_keys.insert(instance_root);
  for (std::size_t index = 1; index < plan.frames.size(); ++index) {
    std::string key = AllocateUniqueWidgetKey(plan.frames[index].name);
    while (!allocated_keys.insert(key).second) {
      key = AllocateUniqueWidgetKey({});
    }
    runtime_keys[index] = std::move(key);
  }

  std::unordered_map<std::string, std::string> key_by_expanded_name;
  key_by_expanded_name.reserve(plan.frames.size() * 2);
  for (std::size_t index = 0; index < plan.frames.size(); ++index) {
    key_by_expanded_name.try_emplace(plan.frames[index].name,
                                     runtime_keys[index]);
  }
  const auto resolve_runtime_key = [&](std::string* name) {
    if (name == nullptr || name->empty()) {
      return;
    }
    if (const auto found = key_by_expanded_name.find(*name);
        found != key_by_expanded_name.end()) {
      *name = found->second;
    }
  };

  inst_frame_it->second = plan.frames[0];
  inst_frame_it->second.name = instance_root;
  for (std::size_t index = 1; index < plan.frames.size(); ++index) {
    auto& clone = plan.frames[index];
    clone.name = runtime_keys[index];
    const std::size_t structural_parent = plan.parents[index];
    if (structural_parent < runtime_keys.size()) {
      clone.parent = runtime_keys[structural_parent];
    } else {
      resolve_runtime_key(&clone.parent);
    }
    for (auto& anchor : clone.anchors) {
      resolve_runtime_key(&anchor.relative_to);
    }
  }

  for (std::size_t index = 1; index < plan.frames.size(); ++index) {
    auto& clone = plan.frames[index];
    ApplyTexturePostLoadLayout(&clone);
    CanonicalizeNativeTextRegion(&clone);
    ResolveXmlText(&clone);
    created.push_back(clone.name);
    layout_frames_by_name_.insert_or_assign(clone.name, clone);
    RegisterWidget(BuildWidgetStateFromFrame(clone, widgets_, false));
    source_widget_order_.push_back(clone.name);
  }

  if (const auto widget = widgets_.find(instance_root);
      widget != widgets_.end() &&
      widget->second.parent != layout_frames_by_name_[instance_root].parent) {
    SetParent(instance_root, layout_frames_by_name_[instance_root].parent);
  }
  if (auto widget_it = widgets_.find(instance_root); widget_it != widgets_.end()) {
    auto& frame = layout_frames_by_name_[instance_root];
    ApplyTexturePostLoadLayout(&frame);
    widget_it->second.kind = frame.kind;
    widget_it->second.inherits = frame.inherits;
    {
      const auto kind = ToLowerAscii(frame.kind);
      widget_it->second.texture_file = (kind == "texture" || kind == "movieframe") ? frame.file : std::string();
      widget_it->second.model_file = (kind == "model" || kind == "modelffx") ? frame.file : std::string();
    }
    widget_it->second.font_style = frame.font_style;
    widget_it->second.justify_h = frame.justify_h;
    widget_it->second.justify_v = frame.justify_v;
    widget_it->second.text_spacing_stored = frame.text_spacing_stored;
    widget_it->second.text_height_stored = frame.text_height_stored;
    widget_it->second.max_lines = frame.max_lines;
    widget_it->second.word_wrap = frame.word_wrap.value_or(true);
    widget_it->second.non_space_wrap = frame.non_space_wrap.value_or(false);
    widget_it->second.indented_word_wrap = frame.indented_word_wrap.value_or(false);
    widget_it->second.backdrop = frame.backdrop;
    widget_it->second.alpha_mode = frame.alpha_mode;
    widget_it->second.draw_layer = frame.draw_layer;
    widget_it->second.draw_sublevel = frame.draw_sublevel;
    widget_it->second.frame_strata = frame.frame_strata;
    widget_it->second.frame_level = ResolveInitialFrameLevel(widgets_, frame);
    widget_it->second.depth = frame.depth;
    widget_it->second.protected_frame = frame.protected_frame;
    widget_it->second.mouse_enabled = frame.enable_mouse;
    const std::string kind_lower = ToLowerAscii(frame.kind);
    SetMouseEnabled(instance_root,
                    frame.enable_mouse || kind_lower == "button" ||
                        kind_lower == "checkbutton" || kind_lower == "editbox" ||
                        kind_lower == "slider");
    widget_it->second.hit_rect_inset_left = frame.hit_rect_inset_left;
    widget_it->second.hit_rect_inset_right = frame.hit_rect_inset_right;
    widget_it->second.hit_rect_inset_top = frame.hit_rect_inset_top;
    widget_it->second.hit_rect_inset_bottom = frame.hit_rect_inset_bottom;
    widget_it->second.has_hit_rect_insets = frame.has_hit_rect_insets;
    if (frame.clamped_to_screen.has_value()) {
      widget_it->second.clamped_to_screen = *frame.clamped_to_screen;
    }
    widget_it->second.width = static_cast<int>(frame.width.value_or(static_cast<float>(widget_it->second.width)));
    widget_it->second.height = static_cast<int>(frame.height.value_or(static_cast<float>(widget_it->second.height)));
    widget_it->second.button_normal_font_style = frame.button_normal_font_style;
    widget_it->second.button_disabled_font_style = frame.button_disabled_font_style;
    widget_it->second.button_highlight_font_style = frame.button_highlight_font_style;
    widget_it->second.button_normal_color = frame.button_normal_color;
    widget_it->second.button_disabled_color = frame.button_disabled_color;
    widget_it->second.button_highlight_color = frame.button_highlight_color;
    widget_it->second.status_bar = frame.status_bar;
    widget_it->second.color_wheel_texture_file = frame.color_wheel_texture_file;
    widget_it->second.color_wheel_thumb_texture_file =
        frame.color_wheel_thumb_texture_file;
    widget_it->second.color_value_texture_file = frame.color_value_texture_file;
    widget_it->second.color_value_thumb_texture_file = frame.color_value_thumb_texture_file;
    widget_it->second.fog_r = frame.fog_r;
    widget_it->second.fog_g = frame.fog_g;
    widget_it->second.fog_b = frame.fog_b;
    widget_it->second.has_fog_color = frame.has_fog_color;
    widget_it->second.fog_near = frame.fog_near;
    widget_it->second.has_fog_near = frame.has_fog_near;
    widget_it->second.fog_far = frame.fog_far;
    widget_it->second.has_fog_far = frame.has_fog_far;
    widget_it->second.glow = frame.glow;
    widget_it->second.has_glow = frame.has_glow;
    widget_it->second.model_scale = frame.model_scale;
    widget_it->second.has_model_scale = frame.has_model_scale;
    widget_it->second.model_sequence = frame.model_sequence;
    widget_it->second.has_model_sequence = frame.has_model_sequence;
    widget_it->second.model_camera = frame.model_camera;
    widget_it->second.has_model_camera = frame.has_model_camera;
    widget_it->second.model_sequence_time_ms = frame.model_sequence_time_ms;
    widget_it->second.has_model_sequence_time = frame.has_model_sequence_time;
    widget_it->second.model_x = frame.model_x;
    widget_it->second.model_y = frame.model_y;
    widget_it->second.model_z = frame.model_z;
    widget_it->second.has_model_position = frame.has_model_position;
    widget_it->second.model_facing_rad = frame.model_facing_rad;
    widget_it->second.has_model_facing = frame.has_model_facing;
    ApplyModelWidgetStateToProps(widget_it->second);
  }

  std::vector<std::string> text_roots;
  text_roots.reserve(created.size() + 1);
  text_roots.push_back(instance_root);
  text_roots.insert(text_roots.end(), created.begin(), created.end());
  for (const auto& text_root : text_roots) {
    const auto it = widgets_.find(text_root);
    if (it == widgets_.end() || it->second.text.empty()) {
      continue;
    }
    const auto kind = ToLowerAscii(it->second.kind);
    if (kind == "button" || kind == "checkbutton" || kind == "editbox") {
      SetText(text_root, it->second.text);
    }
  }

  layout_dirty_ = true;
  return created;
}

bool GlueWidgetRuntime::HasTemplate(const std::string& template_root) const {
  return !template_root.empty() && templates_.find(template_root) != templates_.end();
}

GlueTemplateValidation GlueWidgetRuntime::ValidateTemplateChain(
    const std::string& template_root) const {
  switch (openwow::ui::game::runtime::ValidateFrameTemplateChain(
      templates_, template_root)) {
    case openwow::ui::game::runtime::FrameTemplateValidation::Found:
      return GlueTemplateValidation::kFound;
    case openwow::ui::game::runtime::FrameTemplateValidation::Missing:
      return GlueTemplateValidation::kMissing;
    case openwow::ui::game::runtime::FrameTemplateValidation::Recursive:
      return GlueTemplateValidation::kRecursive;
  }
  return GlueTemplateValidation::kMissing;
}

}
