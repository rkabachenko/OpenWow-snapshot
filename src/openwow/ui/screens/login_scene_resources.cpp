#include "openwow/ui/screens/login_scene.h"

#include "openwow/data/image/image_decoder.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace openwow::ui::screens {

using openwow::text::Trim;

namespace {

void PremultiplyAlphaRgba(std::vector<std::uint8_t>* pixels_rgba) {
  if (pixels_rgba == nullptr || pixels_rgba->empty()) {
    return;
  }
  auto& px = *pixels_rgba;
  const std::size_t count = px.size() / 4u;
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t off = i * 4u;
    const std::uint32_t a = px[off + 3];
    if (a == 255u) {
      continue;
    }
    if (a == 0u) {
      px[off + 0] = 0;
      px[off + 1] = 0;
      px[off + 2] = 0;
      continue;
    }
    const std::uint32_t r = px[off + 0];
    const std::uint32_t g = px[off + 1];
    const std::uint32_t b = px[off + 2];
    px[off + 0] = static_cast<std::uint8_t>((r * a + 127u) / 255u);
    px[off + 1] = static_cast<std::uint8_t>((g * a + 127u) / 255u);
    px[off + 2] = static_cast<std::uint8_t>((b * a + 127u) / 255u);
  }
}

struct FreeTypeLibrary {
  FT_Library lib{nullptr};
  bool ok{false};

  FreeTypeLibrary() {
    if (FT_Init_FreeType(&lib) == 0) {
      ok = true;
    }
  }

  ~FreeTypeLibrary() {
    if (lib) {
      FT_Done_FreeType(lib);
      lib = nullptr;
    }
  }
};

FreeTypeLibrary &Ft() {
  static FreeTypeLibrary library;
  return library;
}

}

LoginScene::FontEntry::~FontEntry() {
  if (face != nullptr) {
    FT_Done_Face(static_cast<FT_Face>(face));
    face = nullptr;
  }
}

LoginScene::FontEntry::FontEntry(FontEntry &&other) noexcept {
  bytes = std::move(other.bytes);
  face = other.face;
  other.face = nullptr;
  path = std::move(other.path);
  height_px = other.height_px;
  ascent_px = other.ascent_px;
  line_height_px = other.line_height_px;
  other.height_px = 0;
  other.ascent_px = 0;
  other.line_height_px = 0;
}

LoginScene::FontEntry &LoginScene::FontEntry::operator=(FontEntry &&other) noexcept {
  if (this == &other) {
    return *this;
  }
  if (face != nullptr) {
    FT_Done_Face(static_cast<FT_Face>(face));
    face = nullptr;
  }
  bytes = std::move(other.bytes);
  face = other.face;
  other.face = nullptr;
  path = std::move(other.path);
  height_px = other.height_px;
  ascent_px = other.ascent_px;
  line_height_px = other.line_height_px;
  other.height_px = 0;
  other.ascent_px = 0;
  other.line_height_px = 0;
  return *this;
}

LoginScene::~LoginScene() {
  for (auto *texture : loaded_textures_) {
    if (texture) {
      SDL_DestroyTexture(texture);
    }
  }
  for (auto &[key, entry] : text_cache_) {
    (void)key;
    if (entry.texture) {
      SDL_DestroyTexture(entry.texture);
      entry.texture = nullptr;
    }
  }
  text_cache_.clear();
  font_cache_.clear();
}

void LoginScene::BindWidgetRuntime(const openwow::ui::glue::GlueWidgetRuntime *runtime) {
  widget_runtime_ = runtime;
}

void LoginScene::Initialize(const openwow::vfs::VirtualFileSystem &vfs, int width, int height) {
  vfs_ = &vfs;
  for (auto *texture : loaded_textures_) {
    if (texture) {
      SDL_DestroyTexture(texture);
    }
  }
  loaded_textures_.clear();
  loaded_texture_paths_.clear();
  for (auto &[key, entry] : text_cache_) {
    (void)key;
    if (entry.texture) {
      SDL_DestroyTexture(entry.texture);
    }
  }
  text_cache_.clear();
  font_cache_.clear();
  font_registry_ = openwow::ui::glue::GlueFontRegistry::LoadFromVfs(vfs);
  logged_runtime_empty_draw_ = false;
  logged_draw_dump_ = false;
  (void)width;
  (void)height;
}

void LoginScene::SetState(const LoginSceneState &state) {
  state_ = state;
}

SDL_Texture *LoginScene::LoadTexture(SDL_Renderer *renderer, const std::string &path) {
  auto it = std::find(loaded_texture_paths_.begin(), loaded_texture_paths_.end(), path);
  if (it != loaded_texture_paths_.end()) {
    const auto idx = static_cast<std::size_t>(std::distance(loaded_texture_paths_.begin(), it));
    return loaded_textures_[idx];
  }

  if (vfs_ == nullptr) {
    return nullptr;
  }

  const auto bytes = vfs_->ReadFileBytes(path);
  if (!bytes.has_value() || bytes->empty()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kTrace, "LoginScene texture read failed: " + path);
    return nullptr;
  }

  SDL_Surface *surface = nullptr;
  auto decoded = openwow::data::image::DecodeImage(*bytes);
  if (decoded.ok && !decoded.pixels_rgba.empty()) {

    PremultiplyAlphaRgba(&decoded.pixels_rgba);
    surface = SDL_CreateRGBSurfaceWithFormat(0, decoded.width, decoded.height, 32,
                                             SDL_PIXELFORMAT_ABGR8888);
    if (surface) {
      const std::size_t src_pitch = static_cast<std::size_t>(std::max(0, decoded.width)) * 4u;
      const std::size_t dst_pitch = static_cast<std::size_t>(std::max(0, surface->pitch));
      const std::size_t rows = static_cast<std::size_t>(std::max(0, decoded.height));
      const std::size_t copy_pitch = std::min(src_pitch, dst_pitch);
      auto *dst = static_cast<std::uint8_t *>(surface->pixels);
      const auto *src = decoded.pixels_rgba.data();
      for (std::size_t row = 0; row < rows; ++row) {
        std::memcpy(dst + row * dst_pitch, src + row * src_pitch, copy_pitch);
      }
    }
  } else {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "Image decode failed: " + path + " err=" + decoded.error);
  }
  if (!surface) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "Texture load failed (surface null): " + path);
    return nullptr;
  }

  SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_FreeSurface(surface);
  if (!texture) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       std::string("SDL_CreateTextureFromSurface failed: ") + SDL_GetError() +
                           " path=" + path);
    return nullptr;
  }

  loaded_texture_paths_.push_back(path);
  loaded_textures_.push_back(texture);
  return texture;
}

LoginScene::FontEntry *LoginScene::LoadFont(const std::string &font_path, int height_px) {
  if (vfs_ == nullptr || height_px <= 0) {
    return nullptr;
  }
  if (!Ft().ok) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "FreeType init failed; FontString rendering disabled");
    return nullptr;
  }

  std::string path = font_path;
  std::replace(path.begin(), path.end(), '\\', '/');
  path = Trim(path);
  if (!path.empty() && path.front() != '/') {
    path.insert(path.begin(), '/');
  }
  if (path.empty()) {
    return nullptr;
  }

  const std::string key = path + "#" + std::to_string(height_px);
  const auto it = font_cache_.find(key);
  if (it != font_cache_.end()) {
    return &it->second;
  }

  const auto bytes_opt = vfs_->ReadFileBytes(path);
  if (!bytes_opt.has_value() || bytes_opt->empty()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "Font load failed (VFS read): " + path);
    return nullptr;
  }

  FontEntry entry;
  entry.bytes = std::make_shared<std::vector<std::uint8_t>>(*bytes_opt);
  entry.path = path;
  entry.height_px = height_px;

  FT_Face face = nullptr;
  const FT_Error face_err =
      FT_New_Memory_Face(Ft().lib, reinterpret_cast<const FT_Byte *>(entry.bytes->data()),
                         static_cast<FT_Long>(entry.bytes->size()), 0, &face);
  if (face_err != 0 || face == nullptr) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "FT_New_Memory_Face failed for font: " + path);
    return nullptr;
  }
  entry.face = face;

  if (FT_Set_Pixel_Sizes(static_cast<FT_Face>(entry.face), 0, static_cast<FT_UInt>(height_px)) !=
      0) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "FT_Set_Pixel_Sizes failed for font: " + path);
    return nullptr;
  }

  const FT_Face ft_face = static_cast<FT_Face>(entry.face);
  if (ft_face->size != nullptr) {
    entry.ascent_px = static_cast<int>(ft_face->size->metrics.ascender >> 6);
    entry.line_height_px = static_cast<int>(ft_face->size->metrics.height >> 6);
  }
  if (entry.line_height_px <= 0) {
    entry.line_height_px = height_px;
  }
  if (entry.ascent_px <= 0) {
    entry.ascent_px = height_px;
  }

  font_cache_.insert_or_assign(key, std::move(entry));
  return &font_cache_[key];
}

}
