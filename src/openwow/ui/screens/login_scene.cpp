#include "openwow/ui/screens/login_scene.h"

#include "openwow/data/formats/blp/texture_path.h"
#include "openwow/ui/framexml/backdrop_render_utils.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>

namespace openwow::ui::screens {

using openwow::data::blp::NormalizeTexturePath;
using openwow::text::Trim;

namespace {

SDL_BlendMode PremultipliedAlphaBlendMode() {
  static const SDL_BlendMode mode = SDL_ComposeCustomBlendMode(
      SDL_BLENDFACTOR_ONE,
      SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
      SDL_BLENDOPERATION_ADD,
      SDL_BLENDFACTOR_ONE,
      SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
      SDL_BLENDOPERATION_ADD);
  return mode;
}

SDL_BlendMode PremultipliedAddBlendMode() {
  static const SDL_BlendMode mode = SDL_ComposeCustomBlendMode(
      SDL_BLENDFACTOR_ONE,
      SDL_BLENDFACTOR_ONE,
      SDL_BLENDOPERATION_ADD,
      SDL_BLENDFACTOR_ONE,
      SDL_BLENDFACTOR_ONE,
      SDL_BLENDOPERATION_ADD);
  return mode;
}

int ResolveHorizontalJustifyIndex(const std::string& justify) {
  uint32_t flags = 0x2u;
  if (openwow::ui::StringToHorizontalJustify(justify.empty() ? nullptr : justify.c_str(), &flags) ==
      0) {
    flags = 0x2u;
  }
  return openwow::ui::HorizontalJustifyFlagsToIndex(flags);
}

int ResolveVerticalJustifyIndex(const std::string& justify) {
  uint32_t flags = 0x10u;
  if (openwow::ui::StringToVerticalJustify(justify.empty() ? nullptr : justify.c_str(), &flags) ==
      0) {
    flags = 0x10u;
  }
  return openwow::ui::VerticalJustifyFlagsToIndex(flags);
}

}

void LoginScene::Render(SDL_Renderer *renderer, int width, int height) {
  (void)width;
  (void)height;
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);

  int drawn = 0;
  if (widget_runtime_ != nullptr) {
    const char *dump_env = std::getenv("OPENWOW_UI_DUMP_DRAW");
    auto should_dump_draw = [&]() -> bool {
      if (dump_env == nullptr)
        return false;
      std::string s(dump_env);
      while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        s.erase(s.begin());
      }
      while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
        s.pop_back();
      }
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      return s == "1" || s == "true" || s == "yes" || s == "on";
    }();

    if (!logged_draw_dump_) {
      logged_draw_dump_ = true;
      if (dump_env != nullptr) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                           std::string("OPENWOW_UI_DUMP_DRAW=") + dump_env);
      }
    }

    const auto& visible_widgets =
        widget_runtime_->VisibleWidgetsInRenderOrder();
    if (should_dump_draw) {
      std::size_t count = 0;
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "Glue draw dump (first 80 textures):");
      for (const auto &widget : visible_widgets) {
        if (widget.kind != "Texture" || widget.texture_file.empty()) {
          continue;
        }
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kInfo,
            "  tex name=" + widget.name + " file=" + widget.texture_file + " rect=(" +
                std::to_string(widget.x) + "," + std::to_string(widget.y) + " " +
                std::to_string(widget.width) + "x" + std::to_string(widget.height) + ")" +
                " tex=(" + std::to_string(widget.tex_left) + "," + std::to_string(widget.tex_top) +
                " " + std::to_string(widget.tex_right) + "," + std::to_string(widget.tex_bottom) +
                ")" + " alpha=" + std::to_string(widget.alpha) + " tile=(" +
                std::string(widget.tile_x ? "x" : "-") + std::string(widget.tile_y ? "y" : "-") +
                " " + std::to_string(widget.tile_size_x) + "x" +
                std::to_string(widget.tile_size_y) + ")" +
                " slice=" + std::to_string(static_cast<int>(widget.slice)) +
                " edge=" + std::to_string(widget.slice_edge_size_px));
        if (++count >= 80) {
          break;
        }
      }
    }

    std::size_t dumped_flip_draws = 0;
    for (const auto &widget : visible_widgets) {
      const float effective_alpha = widget_runtime_->EffectiveAlpha(widget.name);
      if (effective_alpha <= 0.0F) {
        continue;
      }
      if ((widget.kind == "Texture" || widget.kind == "MovieFrame") &&
          !widget.texture_file.empty()) {
        const auto path = NormalizeTexturePath(widget.texture_file);
        if (path.empty()) {
          continue;
        }
        SDL_Texture *texture = LoadTexture(renderer, path);
        if (!texture) {
          continue;
        }
        int iw = 0;
        int ih = 0;
        SDL_QueryTexture(texture, nullptr, nullptr, &iw, &ih);
        if (iw <= 0 || ih <= 0) {
          continue;
        }

        auto clamp01 = [](float v) -> float {
          if (v < 0.0F)
            return 0.0F;
          if (v > 1.0F)
            return 1.0F;
          return v;
        };
        auto tex_coords = widget.tex_coords;
        tex_coords.upper_left.u = clamp01(tex_coords.upper_left.u);
        tex_coords.upper_left.v = clamp01(tex_coords.upper_left.v);
        tex_coords.lower_left.u = clamp01(tex_coords.lower_left.u);
        tex_coords.lower_left.v = clamp01(tex_coords.lower_left.v);
        tex_coords.upper_right.u = clamp01(tex_coords.upper_right.u);
        tex_coords.upper_right.v = clamp01(tex_coords.upper_right.v);
        tex_coords.lower_right.u = clamp01(tex_coords.lower_right.u);
        tex_coords.lower_right.v = clamp01(tex_coords.lower_right.v);
        const bool flip_x = widget.tex_right < widget.tex_left;
        const bool flip_y = widget.tex_bottom < widget.tex_top;
        const float base_left = std::min({tex_coords.upper_left.u, tex_coords.lower_left.u,
                                          tex_coords.upper_right.u, tex_coords.lower_right.u});
        const float base_right = std::max({tex_coords.upper_left.u, tex_coords.lower_left.u,
                                           tex_coords.upper_right.u, tex_coords.lower_right.u});
        const float base_top = std::min({tex_coords.upper_left.v, tex_coords.lower_left.v,
                                         tex_coords.upper_right.v, tex_coords.lower_right.v});
        const float base_bottom = std::max({tex_coords.upper_left.v, tex_coords.lower_left.v,
                                            tex_coords.upper_right.v, tex_coords.lower_right.v});
        if (base_right <= base_left || base_bottom <= base_top) {
          continue;
        }

        const int base_x0 = static_cast<int>(base_left * static_cast<float>(iw));
        const int base_x1 = static_cast<int>(base_right * static_cast<float>(iw));
        const int base_y0 = static_cast<int>(base_top * static_cast<float>(ih));
        const int base_y1 = static_cast<int>(base_bottom * static_cast<float>(ih));
        const SDL_Rect base_src{
            base_x0,
            base_y0,
            std::max(0, base_x1 - base_x0),
            std::max(0, base_y1 - base_y0),
        };
        SDL_Rect src = base_src;

        auto mirror_src_within_base = [](const SDL_Rect &base, const SDL_Rect &rect, bool mirror_x,
                                         bool mirror_y) -> SDL_Rect {
          SDL_Rect out = rect;
          if (mirror_x) {
            const int local_x = rect.x - base.x;
            out.x = base.x + (base.w - local_x - rect.w);
          }
          if (mirror_y) {
            const int local_y = rect.y - base.y;
            out.y = base.y + (base.h - local_y - rect.h);
          }
          return out;
        };

        const SDL_RendererFlip flip_flags = static_cast<SDL_RendererFlip>(
            (flip_x ? SDL_FLIP_HORIZONTAL : 0) | (flip_y ? SDL_FLIP_VERTICAL : 0));

        struct StripSlice {
          SDL_Rect src{0, 0, 0, 0};
          bool rotate_90{false};
        };

        auto slice_strip_src = [&](openwow::ui::framexml::TextureSlice slice,
                                   int edge_px) -> std::optional<StripSlice> {
          if (slice == openwow::ui::framexml::TextureSlice::kNone || edge_px <= 0) {
            return std::nullopt;
          }

          const bool horiz_strip = (base_src.h == edge_px) && (base_src.w == edge_px * 8);
          const bool vert_strip = (base_src.w == edge_px) && (base_src.h == edge_px * 8);
          if (!horiz_strip && !vert_strip) {
            return std::nullopt;
          }

          int index = -1;
          const bool rotate_90 = vert_strip;
          switch (slice) {
          case openwow::ui::framexml::TextureSlice::kBackdropLeft:
            index = 0;
            break;
          case openwow::ui::framexml::TextureSlice::kBackdropRight:
            index = 1;
            break;
          case openwow::ui::framexml::TextureSlice::kBackdropTop:
            index = 2;
            break;
          case openwow::ui::framexml::TextureSlice::kBackdropBottom:
            index = 3;
            break;
          case openwow::ui::framexml::TextureSlice::kBackdropTopLeft:
            index = 4;
            break;
          case openwow::ui::framexml::TextureSlice::kBackdropTopRight:
            index = 5;
            break;
          case openwow::ui::framexml::TextureSlice::kBackdropBottomLeft:
            index = 6;
            break;
          case openwow::ui::framexml::TextureSlice::kBackdropBottomRight:
            index = 7;
            break;
          case openwow::ui::framexml::TextureSlice::kNone:
            break;
          }
          if (index < 0) {
            return std::nullopt;
          }

          const int tile_px = edge_px;
          if (tile_px <= 0 || tile_px > base_src.w || tile_px > base_src.h) {
            return std::nullopt;
          }

          const int pad = std::clamp(tile_px / 16, 0, std::max(0, tile_px / 2 - 1));
          const int inset = std::max(1, tile_px - (2 * pad));

          if (horiz_strip) {
            return StripSlice{
                .src = SDL_Rect{base_src.x + index * tile_px + pad, base_src.y + pad, inset, inset},
                .rotate_90 = rotate_90,
            };
          }
          return StripSlice{
              .src = SDL_Rect{base_src.x + pad, base_src.y + index * tile_px + pad, inset, inset},
              .rotate_90 = rotate_90,
          };
        };

        bool rotate_strip_tile_90 = false;
        if (widget.slice != openwow::ui::framexml::TextureSlice::kNone &&
            widget.slice_edge_size_px > 0) {
          const int e = widget.slice_edge_size_px;
          if (const auto strip = slice_strip_src(widget.slice, e); strip.has_value()) {
            src = strip->src;
            rotate_strip_tile_90 = strip->rotate_90;
          } else {
            const auto slice_rect =
                openwow::ui::framexml::ComputeBackdropSlicePixelRect(
                    widget.slice, e, base_src.w, base_src.h);
            if (slice_rect.has_value()) {
              src = SDL_Rect{
                  base_src.x + slice_rect->x,
                  base_src.y + slice_rect->y,
                  slice_rect->width,
                  slice_rect->height,
              };
            }
          }
        }
        if (src.w <= 0 || src.h <= 0) {
          continue;
        }

        int w = widget.width;
        int h = widget.height;
        if (w <= 0)
          w = src.w;
        if (h <= 0)
          h = src.h;
        if (w <= 0 || h <= 0) {
          continue;
        }

        const float mod_a = std::clamp(effective_alpha * widget.color_a, 0.0F, 1.0F);

        SDL_SetTextureAlphaMod(texture, static_cast<Uint8>(255.0F * mod_a));
        SDL_SetTextureColorMod(texture,
                               static_cast<Uint8>(255.0F * std::clamp(widget.color_r * mod_a, 0.0F, 1.0F)),
                               static_cast<Uint8>(255.0F * std::clamp(widget.color_g * mod_a, 0.0F, 1.0F)),
                               static_cast<Uint8>(255.0F * std::clamp(widget.color_b * mod_a, 0.0F, 1.0F)));
        if (widget.alpha_mode == "ADD") {
          SDL_SetTextureBlendMode(texture, PremultipliedAddBlendMode());
        } else {
          SDL_SetTextureBlendMode(texture, PremultipliedAlphaBlendMode());
        }
        SDL_Rect rect{widget.x, widget.y, w, h};

        if (should_dump_draw && (flip_x || flip_y) && dumped_flip_draws < 64) {
          auto rect_str = [](const SDL_Rect &r) -> std::string {
            return "(" + std::to_string(r.x) + "," + std::to_string(r.y) + " " +
                   std::to_string(r.w) + "x" + std::to_string(r.h) + ")";
          };
          openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                             "  draw flip name=" + widget.name + " flip=(" +
                                 std::string(flip_x ? "x" : "-") + std::string(flip_y ? "y" : "-") +
                                 ")" + " base=" + rect_str(base_src) + " src=" + rect_str(src) +
                                 " dst=" + rect_str(rect));
          ++dumped_flip_draws;
        }

        if (widget.tile_x || widget.tile_y) {
          const int step_x =
              (widget.tile_x && widget.tile_size_x > 0) ? widget.tile_size_x : rect.w;
          const int step_y =
              (widget.tile_y && widget.tile_size_y > 0) ? widget.tile_size_y : rect.h;
          const int base_step_x = std::max(1, step_x);
          const int base_step_y = std::max(1, step_y);
          const int tile_src_w = src.w;
          const int tile_src_h = src.h;

          for (int yy = 0; yy < rect.h; yy += base_step_y) {
            const int remaining_h = rect.h - yy;
            const int dst_h = std::min(step_y, remaining_h);
            const int src_h =
                widget.tile_y
                    ? std::max(1, static_cast<int>(std::round(
                                      (static_cast<double>(tile_src_h) * dst_h) / base_step_y)))
                    : tile_src_h;
            for (int xx = 0; xx < rect.w; xx += base_step_x) {
              const int remaining_w = rect.w - xx;
              const int dst_w = std::min(step_x, remaining_w);
              const int src_w =
                  widget.tile_x
                      ? std::max(1, static_cast<int>(std::round(
                                        (static_cast<double>(tile_src_w) * dst_w) / base_step_x)))
                      : tile_src_w;
              const SDL_Rect tile_src = mirror_src_within_base(
                  base_src, SDL_Rect{src.x, src.y, src_w, src_h}, flip_x, flip_y);
              SDL_Rect tile_dst{rect.x + xx, rect.y + yy, dst_w, dst_h};
              if (rotate_strip_tile_90 || flip_flags != SDL_FLIP_NONE) {
                SDL_RenderCopyEx(renderer, texture, &tile_src, &tile_dst,
                                 rotate_strip_tile_90 ? 90.0 : 0.0, nullptr, flip_flags);
              } else {
                SDL_RenderCopy(renderer, texture, &tile_src, &tile_dst);
              }
              ++drawn;
            }
          }
        } else {
          if (widget.slice == openwow::ui::framexml::TextureSlice::kNone && !rotate_strip_tile_90) {
            const SDL_Color vertex_color{
                static_cast<Uint8>(255.0F * std::clamp(widget.color_r * mod_a, 0.0F, 1.0F)),
                static_cast<Uint8>(255.0F * std::clamp(widget.color_g * mod_a, 0.0F, 1.0F)),
                static_cast<Uint8>(255.0F * std::clamp(widget.color_b * mod_a, 0.0F, 1.0F)),
                static_cast<Uint8>(255.0F * mod_a),
            };
            const SDL_Vertex vertices[4] = {
                SDL_Vertex{SDL_FPoint{static_cast<float>(rect.x), static_cast<float>(rect.y)},
                           vertex_color,
                           SDL_FPoint{tex_coords.upper_left.u, tex_coords.upper_left.v}},
                SDL_Vertex{SDL_FPoint{static_cast<float>(rect.x),
                                      static_cast<float>(rect.y + rect.h)},
                           vertex_color,
                           SDL_FPoint{tex_coords.lower_left.u, tex_coords.lower_left.v}},
                SDL_Vertex{SDL_FPoint{static_cast<float>(rect.x + rect.w),
                                      static_cast<float>(rect.y)},
                           vertex_color,
                           SDL_FPoint{tex_coords.upper_right.u, tex_coords.upper_right.v}},
                SDL_Vertex{SDL_FPoint{static_cast<float>(rect.x + rect.w),
                                      static_cast<float>(rect.y + rect.h)},
                           vertex_color,
                           SDL_FPoint{tex_coords.lower_right.u, tex_coords.lower_right.v}},
            };
            constexpr int kQuadIndices[6] = {0, 1, 2, 2, 1, 3};
            if (SDL_RenderGeometry(renderer, texture, vertices, 4, kQuadIndices, 6) == 0) {
              ++drawn;
            } else {
              openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                 std::string("SDL_RenderGeometry failed for ") + widget.name +
                                     ": " + SDL_GetError());
            }
          } else {
            const SDL_Rect draw_src = mirror_src_within_base(base_src, src, flip_x, flip_y);
            if (rotate_strip_tile_90 || flip_flags != SDL_FLIP_NONE) {
              SDL_RenderCopyEx(renderer, texture, &draw_src, &rect,
                               rotate_strip_tile_90 ? 90.0 : 0.0, nullptr, flip_flags);
            } else {
              SDL_RenderCopy(renderer, texture, &draw_src, &rect);
            }
            ++drawn;
          }
        }
        continue;
      }
      if (widget.kind == "FontString" && !widget.text.empty()) {

        std::string style_name = widget.font_style;
        if (style_name.empty()) {
          style_name = widget.inherits;
        }
        auto split_csv = [&](const std::string &raw) -> std::vector<std::string> {
          std::vector<std::string> out;
          std::string cur;
          for (char ch : raw) {
            if (ch == ',') {
              out.push_back(cur);
              cur.clear();
              continue;
            }
            cur.push_back(ch);
          }
          out.push_back(cur);
          for (auto &s : out) {
            s = Trim(s);
          }
          out.erase(std::remove_if(out.begin(), out.end(),
                                   [](const std::string &s) { return s.empty(); }),
                    out.end());
          return out;
        };

        openwow::ui::glue::GlueFontStyle resolved;
        bool have_style = false;
        if (font_registry_.has_value()) {
          for (const auto &token : split_csv(style_name)) {
            const auto r = font_registry_->Resolve(token);
            if (r.has_value() && !r->font_file.empty() && r->height_px > 0) {
              resolved = *r;
              have_style = true;
              break;
            }
          }
        }
        if (!have_style) {
          resolved.font_file = "/Fonts/FRIZQT__.TTF";
          resolved.height_px = 14;
          resolved.color_r = 1.0F;
          resolved.color_g = 1.0F;
          resolved.color_b = 1.0F;
          resolved.color_a = 1.0F;
        }

        const float eff_scale = widget_runtime_->GetEffectiveScale(widget.name);
        const int scaled_height_px = std::max(1, static_cast<int>(resolved.height_px * eff_scale));
        FontEntry *font = LoadFont(resolved.font_file, scaled_height_px);
        if (!font) {
          continue;
        }

        auto clamp01 = [](float v) -> float { return std::clamp(v, 0.0F, 1.0F); };
        const float a = clamp01(effective_alpha * widget.color_a * resolved.color_a);
        SDL_Color color{
            static_cast<Uint8>(255.0F * clamp01(widget.color_r * resolved.color_r)),
            static_cast<Uint8>(255.0F * clamp01(widget.color_g * resolved.color_g)),
            static_cast<Uint8>(255.0F * clamp01(widget.color_b * resolved.color_b)),
            static_cast<Uint8>(255.0F * a),
        };

        const int wrap_px = widget.width > 0 ? widget.width : 0;
        int tw = 0;
        int th = 0;
        SDL_Texture *text_tex =
            RenderText(renderer,
                       widget.name,
                       font,
                       widget.text,
                       color,
                       wrap_px,
                       widget.word_wrap,
                       widget.non_space_wrap,
                       &tw,
                       &th);
        if (!text_tex || tw <= 0 || th <= 0) {
          continue;
        }

        std::string justify_h = widget.justify_h.empty() ? resolved.justify_h : widget.justify_h;
        std::string justify_v = widget.justify_v.empty() ? resolved.justify_v : widget.justify_v;
        const int justify_h_index = ResolveHorizontalJustifyIndex(justify_h);
        const int justify_v_index = ResolveVerticalJustifyIndex(justify_v);

        int dx = widget.x;
        int dy = widget.y;
        if (widget.width > 0) {
          if (justify_h_index == 1) {
            dx = widget.x + (widget.width - tw) / 2;
          } else if (justify_h_index == 2) {
            dx = widget.x + widget.width - tw;
          }
        }
        if (widget.height > 0) {
          if (justify_v_index == 1) {
            dy = widget.y + (widget.height - th) / 2;
          } else if (justify_v_index == 2) {
            dy = widget.y + widget.height - th;
          }
        }
        SDL_Rect dst{dx, dy, tw, th};
        SDL_RenderCopy(renderer, text_tex, nullptr, &dst);
        ++drawn;
        continue;
      }
    }
  }

  if (drawn == 0 && !logged_runtime_empty_draw_) {
    logged_runtime_empty_draw_ = true;
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "LoginScene: runtime widget render produced 0 draw calls");
  }
}

}
