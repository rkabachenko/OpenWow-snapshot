#include "openwow/ui/game/runtime/frame_template_expander.h"

#include "openwow/foundation/text/ascii.h"
#include "openwow/ui/framexml/framexml_name_utils.h"
#include "openwow/ui/widgets/status_bar_definition.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace openwow::ui::game::runtime {
namespace {

std::string SubstituteTemplateRootPrefix(std::string value, const std::string &template_root,
                                         const std::string &instance_root) {
  if (value.empty()) return value;
  if (value == template_root) return instance_root;
  const std::string dotted_prefix = template_root + ".";
  if (value.rfind(dotted_prefix, 0) == 0 || value.rfind(template_root, 0) == 0) {
    return instance_root + value.substr(template_root.size());
  }
  return value;
}

std::string SubstituteParentToken(std::string value, const std::string &parent_name) {
  if (value.empty() || parent_name.empty()) return value;
  std::string::size_type offset = 0;
  while ((offset = value.find("$parent", offset)) != std::string::npos) {
    value.replace(offset, 7, parent_name);
    offset += parent_name.size();
  }
  return value;
}

std::string ResolveTemplateFrameName(
    const std::unordered_map<std::string, const openwow::ui::framexml::UiFrame *> &template_index,
    const std::string &template_root, const std::string &instance_root,
    const std::string &template_name, std::unordered_map<std::string, std::string> *resolved_cache,
    const int depth = 0) {
  if (template_name.empty()) return template_name;
  if (template_name == template_root) return instance_root;
  if (resolved_cache != nullptr) {
    if (const auto it = resolved_cache->find(template_name); it != resolved_cache->end()) {
      return it->second;
    }
  }
  if (depth > 48) {
    return SubstituteParentToken(
        SubstituteTemplateRootPrefix(template_name, template_root, instance_root), instance_root);
  }
  const auto it = template_index.find(template_name);
  if (it == template_index.end() || it->second == nullptr) {
    return SubstituteParentToken(
        SubstituteTemplateRootPrefix(template_name, template_root, instance_root), instance_root);
  }
  const auto &frame = *it->second;
  const std::string template_parent = frame.parent.empty() ? template_root : frame.parent;
  const std::string resolved_parent = ResolveTemplateFrameName(
      template_index, template_root, instance_root, template_parent, resolved_cache, depth + 1);
  std::string resolved = SubstituteTemplateRootPrefix(frame.name, template_root, instance_root);
  resolved = SubstituteParentToken(std::move(resolved), resolved_parent);
  if (resolved_cache != nullptr) resolved_cache->insert_or_assign(template_name, resolved);
  return resolved;
}

std::string ResolveTemplateValueWithParentContext(std::string value,
                                                  const std::string &resolved_parent,
                                                  const std::string &template_root,
                                                  const std::string &instance_root) {
  value = SubstituteTemplateRootPrefix(std::move(value), template_root, instance_root);
  return SubstituteParentToken(std::move(value),
                               resolved_parent.empty() ? instance_root : resolved_parent);
}

bool FontStringStyleNamesInheritedTemplate(
    const openwow::ui::framexml::UiFrame &frame,
    const openwow::ui::framexml::UiFrame &inherited_template) {
  if (openwow::text::ToLowerAscii(frame.kind) != "fontstring" || inherited_template.name.empty()) {
    return false;
  }
  if (frame.font_style == inherited_template.name) return true;
  if (frame.font_style != frame.inherits) return false;
  const auto inherited_names = openwow::ui::framexml::SplitTemplateList(
      frame.inherits, openwow::ui::framexml::TemplateListSyntax::kCreateFrame);
  return std::find(inherited_names.begin(), inherited_names.end(), inherited_template.name) !=
         inherited_names.end();
}

const openwow::ui::framexml::UiFrame *FindTemplateRootFrame(
    const std::unordered_map<std::string, std::vector<openwow::ui::framexml::UiFrame>> &templates,
    const std::string &template_name) {
  const auto it = templates.find(template_name);
  if (it == templates.end()) return nullptr;
  for (const auto &frame : it->second) {
    if (frame.name == template_name) return &frame;
  }
  return nullptr;
}

FrameTemplateValidation ValidateFrameTemplateChainImpl(
    const std::unordered_map<std::string, std::vector<openwow::ui::framexml::UiFrame>> &templates,
    const std::string &template_name,
    std::unordered_set<std::string> &stack) {
  if (template_name.empty()) return FrameTemplateValidation::Missing;
  if (!stack.insert(template_name).second) return FrameTemplateValidation::Recursive;
  const auto *root = FindTemplateRootFrame(templates, template_name);
  if (root == nullptr) {
    stack.erase(template_name);
    return FrameTemplateValidation::Missing;
  }
  for (const auto &inherited_name : openwow::ui::framexml::SplitTemplateList(
           root->inherits, openwow::ui::framexml::TemplateListSyntax::kCreateFrame)) {
    const auto result = ValidateFrameTemplateChainImpl(templates, inherited_name, stack);
    if (result != FrameTemplateValidation::Found) {
      stack.erase(template_name);
      return result;
    }
  }
  stack.erase(template_name);
  return FrameTemplateValidation::Found;
}

openwow::ui::framexml::UiFrame CloneTemplateFrameForInstance(
    const openwow::ui::framexml::UiFrame &template_frame, const std::string &template_root,
    const std::string &structural_instance_root, const std::string &instance_name_root,
    const std::string &instance_lua_root,
    const std::unordered_map<std::string, const openwow::ui::framexml::UiFrame *> &template_index,
    std::unordered_map<std::string, std::string> *resolved_cache) {
  openwow::ui::framexml::UiFrame clone = template_frame;
  clone.virtual_template = false;
  const std::string template_lua_name{template_frame.LuaName()};
  const std::string template_parent = clone.parent.empty() ? template_root : clone.parent;
  std::string resolved_parent = ResolveTemplateFrameName(
      template_index, template_root, instance_name_root, template_parent, resolved_cache);
  if (template_parent == template_root) {

    resolved_parent = structural_instance_root;
  }
  std::string resolved_lua_parent = instance_lua_root;
  if (template_parent != template_root) {
    if (const auto parent = template_index.find(template_parent);
        parent != template_index.end() && parent->second != nullptr) {
      resolved_lua_parent = SubstituteTemplateRootPrefix(std::string(parent->second->LuaName()),
                                                         template_root, instance_lua_root);
    }
  }
  clone.parent = resolved_parent;
  clone.name = ResolveTemplateFrameName(template_index, template_root, instance_name_root,
                                        clone.name, resolved_cache);
  if (!template_lua_name.empty()) {
    clone.lua_name = SubstituteTemplateRootPrefix(template_lua_name, template_root,
                                                  instance_lua_root);
  }
  clone.inherits = ResolveTemplateValueWithParentContext(
      std::move(clone.inherits), resolved_lua_parent, template_root, instance_lua_root);
  clone.file = ResolveTemplateValueWithParentContext(std::move(clone.file), resolved_lua_parent,
                                                     template_root, instance_lua_root);
  clone.font_style = ResolveTemplateValueWithParentContext(
      std::move(clone.font_style), resolved_lua_parent, template_root, instance_lua_root);
  clone.font_reference = ResolveTemplateValueWithParentContext(
      std::move(clone.font_reference), resolved_lua_parent, template_root, instance_lua_root);
  for (auto &anchor : clone.anchors) {
    if (anchor.relative_to_explicit) {
      anchor.relative_to = ResolveTemplateValueWithParentContext(
          std::move(anchor.relative_to), resolved_lua_parent, template_root, instance_lua_root);
    } else {
      anchor.relative_to = resolved_parent;
    }
  }
  for (auto &attribute : clone.initial_attributes) {
    attribute.name = ResolveTemplateValueWithParentContext(
        std::move(attribute.name), resolved_lua_parent, template_root, instance_lua_root);
    attribute.value = ResolveTemplateValueWithParentContext(
        std::move(attribute.value), resolved_lua_parent, template_root, instance_lua_root);
  }
  return clone;
}

}

void MergeInheritedFrameDefinition(openwow::ui::framexml::UiFrame &dst,
                                   const openwow::ui::framexml::UiFrame &src) {
  if (!dst.width.has_value() && src.width.has_value()) dst.width = src.width;
  if (!dst.height.has_value() && src.height.has_value()) dst.height = src.height;
  if (!dst.rel_width.has_value() && src.rel_width.has_value()) dst.rel_width = src.rel_width;
  if (!dst.rel_height.has_value() && src.rel_height.has_value()) dst.rel_height = src.rel_height;
  if (dst.anchors.empty() && !src.anchors.empty()) dst.anchors = src.anchors;
  if (!dst.set_all_points_explicit && src.set_all_points_explicit) {
    dst.set_all_points = src.set_all_points;
    dst.set_all_points_explicit = true;
  } else if (!dst.set_all_points_explicit && !dst.set_all_points &&
             src.set_all_points) {
    dst.set_all_points = true;
  }
  if (!dst.clamped_to_screen.has_value() && src.clamped_to_screen.has_value())
    dst.clamped_to_screen = src.clamped_to_screen;
  if (!dst.cooldown_reverse.has_value() && src.cooldown_reverse.has_value())
    dst.cooldown_reverse = src.cooldown_reverse;
  if (!dst.cooldown_draw_edge.has_value() && src.cooldown_draw_edge.has_value())
    dst.cooldown_draw_edge = src.cooldown_draw_edge;
  if (dst.frame_strata.empty() && !src.frame_strata.empty()) dst.frame_strata = src.frame_strata;
  if (!dst.has_frame_level && src.has_frame_level) {
    dst.frame_level = src.frame_level;
    dst.has_frame_level = true;
  }
  if (dst.depth == 0.0f && src.depth != 0.0f) dst.depth = src.depth;
  if (dst.draw_layer.empty() && !src.draw_layer.empty()) dst.draw_layer = src.draw_layer;
  if (dst.draw_sublevel == 0 && src.draw_sublevel != 0) dst.draw_sublevel = src.draw_sublevel;
  if (!dst.top_level && src.top_level) dst.top_level = true;
  if (!dst.toplevel_explicit && src.toplevel_explicit) {
    dst.toplevel = src.toplevel;
    dst.toplevel_explicit = true;
  } else if (!dst.toplevel_explicit && !src.toplevel_explicit && !dst.toplevel &&
             src.toplevel) {

    dst.toplevel = true;
  }
  if (!dst.protected_explicit && src.protected_explicit) {
    dst.protected_frame = src.protected_frame;
    dst.protected_explicit = true;
  } else if (!dst.protected_explicit && !src.protected_explicit &&
             !dst.protected_frame && src.protected_frame) {
    dst.protected_frame = true;
  }
  if (!dst.movable.has_value() && src.movable.has_value()) dst.movable = src.movable;
  if (!dst.resizable.has_value() && src.resizable.has_value()) dst.resizable = src.resizable;
  if (!dst.auto_focus.has_value() && src.auto_focus.has_value())
    dst.auto_focus = src.auto_focus;

  if (!dst.backdrop.has_value() && src.backdrop.has_value()) dst.backdrop = src.backdrop;
  if (!dst.visibility_explicit && src.visibility_explicit) {
    dst.visible = src.visible;
    dst.visibility_explicit = true;
  } else if (!dst.visibility_explicit && !src.visibility_explicit && dst.visible &&
             !src.visible) {
    dst.visible = false;
  }
  if (dst.parent.empty() && !src.parent.empty()) dst.parent = src.parent;
  if (!dst.has_id && src.has_id) {
    dst.id = src.id;
    dst.has_id = true;
  }
  if (dst.parent_keys.empty()) {
    dst.parent_keys = src.parent_keys;
  } else if (!src.parent_keys.empty()) {
    auto merged = src.parent_keys;
    merged.insert(merged.end(), dst.parent_keys.begin(), dst.parent_keys.end());
    dst.parent_keys = std::move(merged);
  }
  if (dst.texture_role == openwow::ui::framexml::UiFrame::TextureRole::Normal &&
      src.texture_role != openwow::ui::framexml::UiFrame::TextureRole::Normal)
    dst.texture_role = src.texture_role;

  if ((dst.font_style.empty() || FontStringStyleNamesInheritedTemplate(dst, src)) &&
      !src.font_style.empty())
    dst.font_style = src.font_style;
  if (dst.font_reference.empty() && !src.font_reference.empty())
    dst.font_reference = src.font_reference;
  if (!dst.has_font_height && src.has_font_height) {
    dst.text_height_stored = src.text_height_stored;
    dst.has_font_height = true;
  }
  if (dst.font_outline.empty() && !src.font_outline.empty()) dst.font_outline = src.font_outline;
  if (!dst.font_monochrome.has_value() && src.font_monochrome.has_value())
    dst.font_monochrome = src.font_monochrome;
  if (dst.justify_h.empty() && !src.justify_h.empty()) dst.justify_h = src.justify_h;
  if (dst.justify_v.empty() && !src.justify_v.empty()) dst.justify_v = src.justify_v;
  if (!dst.word_wrap.has_value() && src.word_wrap.has_value()) dst.word_wrap = src.word_wrap;
  if (!dst.non_space_wrap.has_value() && src.non_space_wrap.has_value())
    dst.non_space_wrap = src.non_space_wrap;
  if (!dst.indented_word_wrap.has_value() && src.indented_word_wrap.has_value())
    dst.indented_word_wrap = src.indented_word_wrap;
  if (dst.text.empty() && !src.text.empty()) dst.text = src.text;
  if (!dst.has_text_spacing && src.has_text_spacing) {
    dst.text_spacing_stored = src.text_spacing_stored;
    dst.has_text_spacing = true;
  }
  if (dst.max_lines == 0 && src.max_lines != 0) dst.max_lines = src.max_lines;
  if (!dst.has_text_color && src.has_text_color) {
    dst.color_r = src.color_r; dst.color_g = src.color_g; dst.color_b = src.color_b;
    dst.color_a = src.color_a; dst.has_text_color = true;
  }
  if (!dst.has_text_shadow && src.has_text_shadow) {
    dst.text_shadow_r = src.text_shadow_r; dst.text_shadow_g = src.text_shadow_g;
    dst.text_shadow_b = src.text_shadow_b; dst.text_shadow_a = src.text_shadow_a;
    dst.text_shadow_x = src.text_shadow_x; dst.text_shadow_y = src.text_shadow_y;
    dst.has_text_shadow = true;
  }
  if (dst.message_font_style.empty() && !src.message_font_style.empty())
    dst.message_font_style = src.message_font_style;
  if (!dst.message_fading.has_value() && src.message_fading.has_value())
    dst.message_fading = src.message_fading;
  if (!dst.message_display_duration.has_value() && src.message_display_duration.has_value())
    dst.message_display_duration = src.message_display_duration;
  if (!dst.message_fade_duration.has_value() && src.message_fade_duration.has_value())
    dst.message_fade_duration = src.message_fade_duration;
  if (!dst.message_max_lines.has_value() && src.message_max_lines.has_value())
    dst.message_max_lines = src.message_max_lines;
  if (dst.message_insert_mode.empty() && !src.message_insert_mode.empty())
    dst.message_insert_mode = src.message_insert_mode;
  if (dst.alpha_mode.empty() && !src.alpha_mode.empty()) dst.alpha_mode = src.alpha_mode;
  if (dst.file.empty() && !src.file.empty()) dst.file = src.file;
  if (!openwow::ui::framexml::TextureCoordinatesWereSpecified(dst) &&
      openwow::ui::framexml::TextureCoordinatesWereSpecified(src)) {
    dst.tex_left = src.tex_left; dst.tex_right = src.tex_right; dst.tex_top = src.tex_top;
    dst.tex_bottom = src.tex_bottom;
    dst.tex_coords = openwow::ui::framexml::EffectiveTextureCoordinates(src);
    dst.has_tex_coords = true;
  }
  const bool destination_had_texture_color =
      openwow::ui::framexml::TextureColorWasSpecified(dst);
  if (!destination_had_texture_color && openwow::ui::framexml::TextureColorWasSpecified(src)) {
    dst.color_r = src.color_r; dst.color_g = src.color_g; dst.color_b = src.color_b;
    dst.color_a = src.color_a; dst.has_vertex_color = true;
  }
  if (!dst.texture_alpha.has_value() && !destination_had_texture_color &&
      src.texture_alpha.has_value())
    dst.texture_alpha = src.texture_alpha;
  if (!dst.gradient.enabled && src.gradient.enabled) dst.gradient = src.gradient;
  if (dst.quest_poi_fill_texture.empty() && !src.quest_poi_fill_texture.empty())
    dst.quest_poi_fill_texture = src.quest_poi_fill_texture;
  if (dst.quest_poi_border_texture.empty() && !src.quest_poi_border_texture.empty())
    dst.quest_poi_border_texture = src.quest_poi_border_texture;
  if (!dst.password && src.password) dst.password = true;
  if (dst.max_letters < 0 && src.max_letters >= 0) dst.max_letters = src.max_letters;
  if (!dst.has_text_insets && src.has_text_insets) {
    dst.text_inset_left = src.text_inset_left; dst.text_inset_right = src.text_inset_right;
    dst.text_inset_top = src.text_inset_top; dst.text_inset_bottom = src.text_inset_bottom;
    dst.has_text_insets = true;
  }
  if (!dst.has_hit_rect_insets && src.has_hit_rect_insets) {
    dst.hit_rect_inset_left = src.hit_rect_inset_left;
    dst.hit_rect_inset_right = src.hit_rect_inset_right;
    dst.hit_rect_inset_top = src.hit_rect_inset_top;
    dst.hit_rect_inset_bottom = src.hit_rect_inset_bottom;
    dst.has_hit_rect_insets = true;
  }
  if (!dst.user_placed && src.user_placed) dst.user_placed = true;
  if (!dst.dont_save_position && src.dont_save_position) dst.dont_save_position = true;
  if (dst.scale == 1.0F && src.scale != 1.0F) dst.scale = src.scale;
  if (!dst.enable_mouse_explicit && src.enable_mouse_explicit) {
    dst.enable_mouse = src.enable_mouse;
    dst.enable_mouse_explicit = true;
  } else if (!dst.enable_mouse_explicit && !src.enable_mouse_explicit &&
             !dst.enable_mouse && src.enable_mouse) {
    dst.enable_mouse = true;
  }
  if (!dst.enable_keyboard_explicit && src.enable_keyboard_explicit) {
    dst.enable_keyboard = src.enable_keyboard; dst.enable_keyboard_explicit = true;
  }
  if (!dst.tile_x_explicit && (src.tile_x_explicit || src.tile_x)) {
    dst.tile_x = src.tile_x; dst.tile_x_explicit = true;
  }
  if (!dst.tile_y_explicit && (src.tile_y_explicit || src.tile_y)) {
    dst.tile_y = src.tile_y; dst.tile_y_explicit = true;
  }
  if (dst.tile_size_x == 0 && src.tile_size_x != 0) dst.tile_size_x = src.tile_size_x;
  if (dst.tile_size_y == 0 && src.tile_size_y != 0) dst.tile_size_y = src.tile_size_y;
  if (dst.slice == openwow::ui::framexml::TextureSlice::kNone &&
      src.slice != openwow::ui::framexml::TextureSlice::kNone) dst.slice = src.slice;
  if (dst.slice_edge_size_px == 0 && src.slice_edge_size_px != 0)
    dst.slice_edge_size_px = src.slice_edge_size_px;
  if (dst.button_normal_font_style.empty() && !src.button_normal_font_style.empty())
    dst.button_normal_font_style = src.button_normal_font_style;
  if (dst.button_disabled_font_style.empty() && !src.button_disabled_font_style.empty())
    dst.button_disabled_font_style = src.button_disabled_font_style;
  if (dst.button_highlight_font_style.empty() && !src.button_highlight_font_style.empty())
    dst.button_highlight_font_style = src.button_highlight_font_style;
  if (!dst.button_normal_color.has_value() && src.button_normal_color.has_value())
    dst.button_normal_color = src.button_normal_color;
  if (!dst.button_disabled_color.has_value() && src.button_disabled_color.has_value())
    dst.button_disabled_color = src.button_disabled_color;
  if (!dst.button_highlight_color.has_value() && src.button_highlight_color.has_value())
    dst.button_highlight_color = src.button_highlight_color;
  openwow::ui::widgets::InheritMissingStatusBarDefinition(dst.status_bar, src.status_bar);
  if (dst.color_wheel_texture_file.empty() && !src.color_wheel_texture_file.empty())
    dst.color_wheel_texture_file = src.color_wheel_texture_file;
  if (dst.color_wheel_thumb_texture_file.empty() && !src.color_wheel_thumb_texture_file.empty())
    dst.color_wheel_thumb_texture_file = src.color_wheel_thumb_texture_file;
  if (dst.color_value_texture_file.empty() && !src.color_value_texture_file.empty())
    dst.color_value_texture_file = src.color_value_texture_file;
  if (dst.color_value_thumb_texture_file.empty() && !src.color_value_thumb_texture_file.empty())
    dst.color_value_thumb_texture_file = src.color_value_thumb_texture_file;
  if (!dst.has_fog_color && src.has_fog_color) {
    dst.fog_r = src.fog_r; dst.fog_g = src.fog_g; dst.fog_b = src.fog_b;
    dst.has_fog_color = true;
  }
  if (!dst.has_fog_near && src.has_fog_near) { dst.fog_near = src.fog_near; dst.has_fog_near = true; }
  if (!dst.has_fog_far && src.has_fog_far) { dst.fog_far = src.fog_far; dst.has_fog_far = true; }
  if (!dst.has_glow && src.has_glow) { dst.glow = src.glow; dst.has_glow = true; }
  if (!dst.has_model_scale && src.has_model_scale) {
    dst.model_scale = src.model_scale; dst.has_model_scale = true;
  }
  if (!dst.has_model_sequence && src.has_model_sequence) {
    dst.model_sequence = src.model_sequence;
    dst.has_model_sequence = true;
  }
  if (!dst.has_model_camera && src.has_model_camera) {
    dst.model_camera = src.model_camera;
    dst.has_model_camera = true;
  }
  if (!dst.has_model_sequence_time && src.has_model_sequence_time) {
    dst.model_sequence_time_ms = src.model_sequence_time_ms;
    dst.has_model_sequence_time = true;
  }
  if (!dst.has_model_position && src.has_model_position) {
    dst.model_x = src.model_x;
    dst.model_y = src.model_y;
    dst.model_z = src.model_z;
    dst.has_model_position = true;
  }
  if (!dst.has_model_facing && src.has_model_facing) {
    dst.model_facing_rad = src.model_facing_rad;
    dst.has_model_facing = true;
  }
  if (dst.initial_attributes.empty()) {
    dst.initial_attributes = src.initial_attributes;
  } else if (!src.initial_attributes.empty()) {
    auto merged = src.initial_attributes;
    merged.insert(merged.end(), dst.initial_attributes.begin(), dst.initial_attributes.end());
    dst.initial_attributes = std::move(merged);
  }
  if (dst.script_handlers.empty()) {
    dst.script_handlers = src.script_handlers;
  } else if (!src.script_handlers.empty()) {
    auto merged = src.script_handlers;
    merged.insert(merged.end(), dst.script_handlers.begin(), dst.script_handlers.end());
    dst.script_handlers = std::move(merged);
  }
  if (dst.animation_groups.empty()) {
    dst.animation_groups = src.animation_groups;
  } else if (!src.animation_groups.empty()) {
    auto merged = src.animation_groups;
    merged.insert(merged.end(), dst.animation_groups.begin(), dst.animation_groups.end());
    dst.animation_groups = std::move(merged);
  }
}

FrameTemplateValidation ValidateFrameTemplateChain(
    const std::unordered_map<std::string, std::vector<openwow::ui::framexml::UiFrame>> &templates,
    const std::string &template_name) {
  std::unordered_set<std::string> stack;
  return ValidateFrameTemplateChainImpl(templates, template_name, stack);
}

std::optional<std::string> ResolveInheritedParentName(
    const std::unordered_map<std::string, std::vector<openwow::ui::framexml::UiFrame>> &templates,
    const std::string &inherits) {
  auto current_tokens = openwow::ui::framexml::SplitTemplateList(
      inherits, openwow::ui::framexml::TemplateListSyntax::kCreateFrame);
  if (current_tokens.empty()) return std::nullopt;
  while (!current_tokens.empty()) {
    const openwow::ui::framexml::UiFrame *last_root = nullptr;
    std::optional<std::string> resolved_parent;
    for (const auto &template_name : current_tokens) {
      last_root = FindTemplateRootFrame(templates, template_name);
      if (last_root == nullptr) return std::nullopt;
      if (!last_root->parent.empty()) resolved_parent = last_root->parent;
    }
    if (resolved_parent.has_value()) return resolved_parent;
    if (last_root == nullptr) return std::nullopt;
    current_tokens = openwow::ui::framexml::SplitTemplateList(
        last_root->inherits, openwow::ui::framexml::TemplateListSyntax::kCreateFrame);
  }
  return std::nullopt;
}

void CollectFrameTemplates(const std::vector<openwow::ui::framexml::UiFrame> &parsed_frames,
                           std::unordered_map<std::string,
                                              std::vector<openwow::ui::framexml::UiFrame>>
                               &out_templates) {
  if (parsed_frames.empty()) return;
  std::unordered_map<std::string, std::size_t> index_by_name;
  index_by_name.reserve(parsed_frames.size());
  for (std::size_t index = 0; index < parsed_frames.size(); ++index) {
    if (!parsed_frames[index].name.empty())
      index_by_name.insert_or_assign(parsed_frames[index].name, index);
  }
  std::unordered_set<std::string> declared_roots;
  declared_roots.reserve(parsed_frames.size());
  for (const auto &frame : parsed_frames) {
    if (!frame.name.empty() && frame.virtual_template) declared_roots.insert(frame.name);
  }
  for (const auto &frame : parsed_frames) {
    if (frame.name.empty()) continue;
    std::string template_root;
    if (declared_roots.find(frame.name) != declared_roots.end()) {
      template_root = frame.name;
    } else {
      std::string current_parent = frame.parent;
      while (!current_parent.empty()) {
        const auto it = index_by_name.find(current_parent);
        if (it == index_by_name.end()) break;
        const auto &parent = parsed_frames[it->second];
        if (declared_roots.find(parent.name) != declared_roots.end()) {
          template_root = parent.name;
          break;
        }
        current_parent = parent.parent;
      }
    }
    if (!template_root.empty()) {
      auto clone = frame;
      if (clone.name != template_root) clone.virtual_template = true;
      out_templates[template_root].push_back(std::move(clone));
    }
  }
}

ExpandedFramePlan BuildExpandedFramePlan(openwow::ui::framexml::UiFrame root_frame,
                                         std::vector<openwow::ui::framexml::UiFrame> local_children,
                                         const std::unordered_map<
                                             std::string,
                                             std::vector<openwow::ui::framexml::UiFrame>> &templates,
                                         const std::string &root_parent_lua_scope) {
  const std::size_t source_node_count = local_children.size() + 1u;
  std::size_t inherited_node_count = 0u;
  std::deque<openwow::ui::framexml::UiFrame> expanded_children;

  std::deque<bool> expanded_children_is_template_sourced;
  struct TemplateExpansionView {
    const openwow::ui::framexml::UiFrame *root{nullptr};
    std::unordered_map<std::string, const openwow::ui::framexml::UiFrame *> frames_by_name;
  };
  std::unordered_map<std::string, TemplateExpansionView> template_views;
  template_views.reserve(32);
  const auto get_template_view = [&](const std::string &template_name) -> const TemplateExpansionView * {
    if (const auto cached = template_views.find(template_name); cached != template_views.end())
      return cached->second.root != nullptr ? &cached->second : nullptr;
    TemplateExpansionView view;
    if (const auto source = templates.find(template_name); source != templates.end()) {
      view.frames_by_name.reserve(source->second.size() * 2);
      for (const auto &template_frame : source->second) {
        if (!template_frame.name.empty())
          view.frames_by_name.insert_or_assign(template_frame.name, &template_frame);
        if (template_frame.name == template_name) view.root = &template_frame;
      }
    }
    const auto [inserted, unused] = template_views.emplace(template_name, std::move(view));
    (void)unused;
    return inserted->second.root != nullptr ? &inserted->second : nullptr;
  };

  std::function<void(openwow::ui::framexml::UiFrame *, const std::string &,
                     std::unordered_set<std::string> *)>
      apply_template_attributes;
  apply_template_attributes = [&](openwow::ui::framexml::UiFrame *target_frame,
                                  const std::string &template_name,
                                  std::unordered_set<std::string> *recursion_stack) {
    if (target_frame == nullptr || template_name.empty()) return;
    std::unordered_set<std::string> local_stack;
    auto &stack = recursion_stack != nullptr ? *recursion_stack : local_stack;
    if (!stack.insert(template_name).second) return;
    const auto *template_view = get_template_view(template_name);
    if (template_view == nullptr) {
      stack.erase(template_name);
      return;
    }
    const auto *template_root = template_view->root;
    MergeInheritedFrameDefinition(*target_frame, *template_root);
    const auto inherited_names = openwow::ui::framexml::SplitTemplateList(
        template_root->inherits,
        openwow::ui::framexml::TemplateListSyntax::kCreateFrame);
    for (auto inherited = inherited_names.rbegin();
         inherited != inherited_names.rend(); ++inherited) {
      apply_template_attributes(target_frame, *inherited, &stack);
    }
    stack.erase(template_name);
  };

  std::function<void(std::string, std::string, std::string, const std::string &,
                     std::unordered_set<std::string> *)>
      append_template_children;
  append_template_children = [&](std::string structural_instance_root,
                                 std::string instance_name_root,
                                 std::string instance_lua_root,
                                 const std::string &template_name,
                                 std::unordered_set<std::string> *recursion_stack) {
    if (template_name.empty()) return;
    std::unordered_set<std::string> local_stack;
    auto &stack = recursion_stack != nullptr ? *recursion_stack : local_stack;
    if (!stack.insert(template_name).second) return;
    const auto *template_view = get_template_view(template_name);
    const auto template_it = templates.find(template_name);
    if (template_view == nullptr || template_it == templates.end()) {
      stack.erase(template_name);
      return;
    }
    for (const auto &inherited_name : openwow::ui::framexml::SplitTemplateList(
             template_view->root->inherits,
             openwow::ui::framexml::TemplateListSyntax::kCreateFrame)) {
      append_template_children(structural_instance_root, instance_name_root,
                               instance_lua_root, inherited_name, &stack);
    }
    std::unordered_map<std::string, std::string> resolved_names;
    resolved_names.reserve(template_view->frames_by_name.size() * 2);
    for (const auto &template_child : template_it->second) {
      if (template_child.name.empty() || template_child.name == template_name) continue;
      expanded_children.push_back(CloneTemplateFrameForInstance(
          template_child, template_name, structural_instance_root, instance_name_root,
          instance_lua_root, template_view->frames_by_name, &resolved_names));
      expanded_children_is_template_sourced.push_back(true);
      ++inherited_node_count;
    }
    stack.erase(template_name);
  };
  const auto apply_inherits_to_frame =
      [&](openwow::ui::framexml::UiFrame *frame, std::string structural_instance_root,
          std::string instance_name_root, std::string instance_lua_root,
          const std::string &inherits) {
        const auto template_names = openwow::ui::framexml::SplitTemplateList(
            inherits, openwow::ui::framexml::TemplateListSyntax::kCreateFrame);
        std::unordered_set<std::string> attribute_stack;
        for (auto template_name = template_names.rbegin();
             template_name != template_names.rend(); ++template_name) {
          apply_template_attributes(frame, *template_name, &attribute_stack);
        }
        std::unordered_set<std::string> child_stack;
        for (const auto &template_name : template_names) {
          append_template_children(structural_instance_root, instance_name_root,
                                   instance_lua_root, template_name, &child_stack);
        }
      };
  std::string root_lua_scope{root_frame.LuaName()};
  if (root_lua_scope.empty())
    root_lua_scope = root_parent_lua_scope.empty() ? "Top" : root_parent_lua_scope;
  if (!root_frame.inherits.empty()) {
    const std::string root_name_scope = root_frame.LuaName().empty() ? root_lua_scope : root_frame.name;
    apply_inherits_to_frame(&root_frame, root_frame.name, root_name_scope, root_lua_scope,
                            root_frame.inherits);
  }

  for (auto &child : local_children) {
    expanded_children.push_back(std::move(child));
    expanded_children_is_template_sourced.push_back(false);
  }
  std::unordered_map<std::string, std::string> lua_scope_by_runtime_key;
  lua_scope_by_runtime_key.reserve(expanded_children.size() * 2 + 1);
  if (!root_frame.name.empty()) lua_scope_by_runtime_key.emplace(root_frame.name, root_lua_scope);
  for (std::size_t index = 0; index < expanded_children.size(); ++index) {
    auto &child = expanded_children[index];
    std::string parent_lua_scope = root_lua_scope;
    if (!child.parent.empty()) {
      if (const auto parent = lua_scope_by_runtime_key.find(child.parent);
          parent != lua_scope_by_runtime_key.end()) parent_lua_scope = parent->second;
    }
    std::string child_lua_scope{child.LuaName()};
    if (child_lua_scope.empty()) child_lua_scope = parent_lua_scope;
    if (!child.name.empty()) lua_scope_by_runtime_key.insert_or_assign(child.name, child_lua_scope);
    if (!expanded_children[index].inherits.empty()) {
      const std::string instance_name_root = child.LuaName().empty() ? child_lua_scope : child.name;
      const std::string inherits = child.inherits;
      apply_inherits_to_frame(&child, child.name, instance_name_root, child_lua_scope, inherits);
    }
  }
  ExpandedFramePlan plan;
  plan.source_nodes = source_node_count;
  plan.inherited_nodes = inherited_node_count;
  plan.frames.reserve(expanded_children.size() + 1);
  plan.frames.push_back(std::move(root_frame));
  plan.frames.insert(plan.frames.end(), std::make_move_iterator(expanded_children.begin()),
                     std::make_move_iterator(expanded_children.end()));
  plan.children.resize(plan.frames.size());
  plan.parents.assign(plan.frames.size(), 0);
  std::unordered_map<std::string, std::size_t> frame_index_by_name;
  frame_index_by_name.reserve(plan.frames.size() * 2);
  for (std::size_t index = 0; index < plan.frames.size(); ++index) {
    if (!plan.frames[index].name.empty())
      frame_index_by_name.insert_or_assign(plan.frames[index].name, index);
  }
  for (std::size_t index = 1; index < plan.frames.size(); ++index) {
    std::size_t parent_index = 0;
    if (!plan.frames[index].parent.empty()) {
      if (const auto parent = frame_index_by_name.find(plan.frames[index].parent);
          parent != frame_index_by_name.end() && parent->second != index)
        parent_index = parent->second;
    }
    plan.parents[index] = parent_index;
    plan.children[parent_index].push_back(index);
  }

  std::deque<std::size_t> scroll_membership_queue{0u};
  while (!scroll_membership_queue.empty()) {
    const std::size_t parent_index = scroll_membership_queue.front();
    scroll_membership_queue.pop_front();
    const auto parent_membership = plan.frames[parent_index].scroll_child_membership_count;
    for (const std::size_t child_index : plan.children[parent_index]) {

      if (child_index >= 1u &&
          expanded_children_is_template_sourced[child_index - 1u]) {
        auto &child_frame = plan.frames[child_index];
        child_frame.scroll_child_membership_count += parent_membership;
        child_frame.scroll_child_content = child_frame.scroll_child_membership_count != 0u;
      }
      scroll_membership_queue.push_back(child_index);
    }
  }

  return plan;
}

}
