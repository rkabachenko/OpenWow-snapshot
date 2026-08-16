#include "openwow/ui/framexml/framexml_parser_detail.h"

#include "openwow/foundation/text/ascii.h"

#include <algorithm>

namespace openwow::ui::framexml::detail {

using openwow::text::Trim;

std::string NormalizeInherits(const std::string& raw) {
  if (raw.empty()) {
    return {};
  }
  return Trim(raw);
}

std::string SanitizeWidgetIdentifier(std::string value) {
  value = Trim(std::move(value));
  value.erase(std::remove_if(value.begin(),
                             value.end(),
                             [](unsigned char ch) {
                               return ch == 0 || ch < 0x20 || ch == 0x7F;
                             }),
              value.end());
  return value;
}

std::string ResolveParentToken(std::string value, const std::string& parent_name) {
  if (value.empty()) {
    return {};
  }

  constexpr std::string_view kParentToken = "$parent";

  if (value.size() >= kParentToken.size() &&
      openwow::text::EqualsIgnoreCaseAscii(
          std::string_view(value).substr(0, kParentToken.size()),
          kParentToken)) {
    value.replace(0, kParentToken.size(), parent_name);
  }
  return Trim(value);
}

float EffectiveBackdropTileSize(const BackdropSpec& spec) {
  if (!spec.tile) {
    return spec.tile_size;
  }
  if (spec.tile_size > 0.0f) {
    return spec.tile_size;
  }
  if (spec.edge_size > 0.0f) {
    return spec.edge_size;
  }
  return spec.tile_size;
}

void InjectBackdropPieces(const BackdropSpec& spec,
                          UiFrame owner,
                          std::unordered_map<std::string, std::size_t>* index_by_name,
                          std::vector<UiFrame>* out_frames) {
  if (index_by_name == nullptr || out_frames == nullptr || owner.name.empty()) {
    return;
  }

  auto add_frame = [&](UiFrame frame) {
    if (frame.name.empty()) {
      return;
    }
    if (index_by_name->contains(frame.name)) {
      return;
    }
    index_by_name->insert_or_assign(frame.name, out_frames->size());
    out_frames->push_back(std::move(frame));
  };

  const auto inherit_render_props = [&](UiFrame* frame) {
    if (frame == nullptr) {
      return;
    }
    frame->frame_strata = owner.frame_strata;
    frame->frame_level = owner.frame_level;
    frame->publish_to_lua = false;

    frame->visible = true;
  };

  if (!spec.bg_file.empty()) {
    const int effective_tile_size = static_cast<int>(EffectiveBackdropTileSize(spec));

    UiFrame background;
    background.kind = "Texture";
    background.name = owner.name + ".__BackdropBackground";
    background.parent = owner.name;
    background.file = spec.bg_file;
    background.draw_layer = "BACKGROUND";
    background.draw_sublevel = -1;
    background.tile_x = spec.tile;
    background.tile_y = spec.tile;
    background.tile_size_x = effective_tile_size;
    background.tile_size_y = effective_tile_size;
    if (spec.has_bg_color) {
      background.color_r = spec.bg_color_r;
      background.color_g = spec.bg_color_g;
      background.color_b = spec.bg_color_b;
      background.color_a = spec.bg_color_a;
    }
    inherit_render_props(&background);
    background.anchors = {
        UiAnchor{
            .point = "TOPLEFT",
            .relative_to = owner.name,
            .relative_point = "TOPLEFT",
            .x = static_cast<float>(spec.inset_left),
            .y = static_cast<float>(-spec.inset_top),
        },
        UiAnchor{
            .point = "BOTTOMRIGHT",
            .relative_to = owner.name,
            .relative_point = "BOTTOMRIGHT",
            .x = static_cast<float>(-spec.inset_right),
            .y = static_cast<float>(spec.inset_bottom),
        },
    };
    add_frame(std::move(background));
  }

  if (spec.edge_file.empty() || spec.edge_size <= 0.0f) {
    return;
  }

  const int edge_size = static_cast<int>(spec.edge_size);
  auto make_border_piece = [&](std::string_view suffix,
                               TextureSlice slice) -> UiFrame {
    UiFrame frame;
    frame.kind = "Texture";
    frame.name = owner.name + ".__BackdropBorder" + std::string(suffix);
    frame.parent = owner.name;
    frame.file = spec.edge_file;
    frame.draw_layer = "BORDER";
    frame.draw_sublevel = -1;
    frame.slice = slice;
    frame.slice_edge_size_px = edge_size;
    if (spec.has_border_color) {
      frame.color_r = spec.border_color_r;
      frame.color_g = spec.border_color_g;
      frame.color_b = spec.border_color_b;
      frame.color_a = spec.border_color_a;
    }
    if (!spec.alpha_mode.empty()) {
      frame.alpha_mode = spec.alpha_mode;
    }
    inherit_render_props(&frame);
    return frame;
  };

  auto make_corner = [&](std::string_view suffix, const std::string& point, TextureSlice slice) {
    UiFrame frame = make_border_piece(suffix, slice);
    frame.width = edge_size;
    frame.height = edge_size;
    frame.anchors = {
        UiAnchor{
            .point = point,
            .relative_to = owner.name,
            .relative_point = point,
            .x = 0.0f,
            .y = 0.0f,
        },
    };
    add_frame(std::move(frame));
  };
  make_corner("TopLeft", "TOPLEFT", TextureSlice::kBackdropTopLeft);
  make_corner("TopRight", "TOPRIGHT", TextureSlice::kBackdropTopRight);
  make_corner("BottomLeft", "BOTTOMLEFT", TextureSlice::kBackdropBottomLeft);
  make_corner("BottomRight", "BOTTOMRIGHT", TextureSlice::kBackdropBottomRight);

  auto make_horizontal_edge = [&](std::string_view suffix,
                                  const std::string& left_point,
                                  const std::string& right_point,
                                  TextureSlice slice) {
    UiFrame frame = make_border_piece(suffix, slice);
    frame.height = edge_size;
    frame.tile_x = true;
    frame.tile_y = false;
    frame.tile_size_x = edge_size;
    frame.tile_size_y = edge_size;
    frame.anchors = {
        UiAnchor{
            .point = left_point,
            .relative_to = owner.name,
            .relative_point = left_point,
            .x = static_cast<float>(edge_size),
            .y = 0.0f,
        },
        UiAnchor{
            .point = right_point,
            .relative_to = owner.name,
            .relative_point = right_point,
            .x = static_cast<float>(-edge_size),
            .y = 0.0f,
        },
    };
    add_frame(std::move(frame));
  };
  make_horizontal_edge("Top", "TOPLEFT", "TOPRIGHT", TextureSlice::kBackdropTop);
  make_horizontal_edge("Bottom", "BOTTOMLEFT", "BOTTOMRIGHT", TextureSlice::kBackdropBottom);

  auto make_vertical_edge = [&](std::string_view suffix,
                                const std::string& top_point,
                                const std::string& bottom_point,
                                TextureSlice slice) {
    UiFrame frame = make_border_piece(suffix, slice);
    frame.width = edge_size;
    frame.tile_x = false;
    frame.tile_y = true;
    frame.tile_size_x = edge_size;
    frame.tile_size_y = edge_size;
    frame.anchors = {
        UiAnchor{
            .point = top_point,
            .relative_to = owner.name,
            .relative_point = top_point,
            .x = 0.0f,
            .y = static_cast<float>(-edge_size),
        },
        UiAnchor{
            .point = bottom_point,
            .relative_to = owner.name,
            .relative_point = bottom_point,
            .x = 0.0f,
            .y = static_cast<float>(edge_size),
        },
    };
    add_frame(std::move(frame));
  };
  make_vertical_edge("Left", "TOPLEFT", "BOTTOMLEFT", TextureSlice::kBackdropLeft);
  make_vertical_edge("Right", "TOPRIGHT", "BOTTOMRIGHT", TextureSlice::kBackdropRight);
}

}
