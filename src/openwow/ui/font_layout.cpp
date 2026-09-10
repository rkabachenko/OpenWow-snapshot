#include "openwow/ui/font_layout.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/render/resources/fonts/font_string_flags.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace openwow::ui {
namespace {

std::shared_ptr<openwow::render::text::FontFace> AcquireMeasurementFace(
    const openwow::vfs::VirtualFileSystem* const vfs, const std::string& path,
    const int pixel_size, const openwow::render::text::FontStyle style) {
  struct CacheKey {
    std::string path;
    int pixel_size;
    openwow::render::text::FontOutline outline;
    bool monochrome;

    bool operator==(const CacheKey& other) const noexcept {
      return pixel_size == other.pixel_size && outline == other.outline &&
             monochrome == other.monochrome && path == other.path;
    }
  };
  struct CacheKeyHash {
    std::size_t operator()(const CacheKey& key) const noexcept {
      std::size_t hash = std::hash<std::string>{}(key.path);
      hash = hash * 31u + static_cast<std::size_t>(key.pixel_size);
      hash = hash * 31u + static_cast<std::size_t>(key.outline);
      hash = hash * 31u + static_cast<std::size_t>(key.monochrome);
      return hash;
    }
  };
  struct CacheBucket {
    std::uint64_t vfs_revision{};
    std::unordered_map<
        CacheKey, std::shared_ptr<openwow::render::text::FontFace>, CacheKeyHash>
        faces;
  };

  static std::unordered_map<const openwow::vfs::VirtualFileSystem*, CacheBucket>
      cache;
  static std::mutex cache_mutex;

  const CacheKey key{.path = path,
                     .pixel_size = pixel_size,
                     .outline = style.outline,
                     .monochrome = style.monochrome};
  const std::uint64_t vfs_revision = vfs != nullptr ? vfs->lookup_revision() : 0;
  std::shared_ptr<openwow::render::text::FontFace> source_face;
  {
    const std::lock_guard lock(cache_mutex);
    auto& bucket = cache[vfs];
    if (bucket.vfs_revision != vfs_revision) {
      bucket.faces.clear();
      bucket.vfs_revision = vfs_revision;
    }
    if (const auto found = bucket.faces.find(key);
        found != bucket.faces.end()) {
      return found->second;
    }
    if (vfs != nullptr) {
      for (const auto& [cached_key, cached_face] : bucket.faces) {
        if (cached_key.path == path && cached_face) {
          source_face = cached_face;
          break;
        }
      }
    }
  }

  std::shared_ptr<openwow::render::text::FontFace> face;
  if (source_face) {
    face = source_face->WithSizeAndStyle(pixel_size, style);
    if (!face) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
          "FontLayout: font initialization failed path=" + path +
              " source=retained-font-bytes pixel_height=" +
              std::to_string(pixel_size) + " reason=font-decode-failed");
    }
  } else if (vfs != nullptr) {
    auto bytes = vfs->ReadFileBytes(path);
    const bool has_bytes = bytes && !bytes->empty();
    if (bytes && !bytes->empty()) {
      face = openwow::render::text::FontFace::LoadMemory(path, std::move(*bytes),
                                                         pixel_size, style);
    }
    if (!face) {
      const auto source = vfs->Resolve(path);
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kError,
          "FontLayout: active VFS font initialization failed path=" + path +
              " source=" +
              (source.has_value() ? source->string() : "missing") +
              " pixel_height=" + std::to_string(pixel_size) + " reason=" +
              (has_bytes ? "font-decode-failed"
                                         : "read-failed-or-empty"));
    }
  } else {
    face = openwow::render::text::FontFace::LoadFile(path, pixel_size, style);
  }

  if (face || vfs != nullptr) {
    const std::lock_guard lock(cache_mutex);
    auto& bucket = cache[vfs];
    if (bucket.vfs_revision == vfs_revision) {
      bucket.faces.emplace(key, face);
    }
  }
  return face;
}

}

std::optional<openwow::render::text::TextLayout> LayoutFontText(
    const openwow::vfs::VirtualFileSystem* vfs,
    const std::string_view font_path, const int pixel_height,
    const std::string_view text,
    const openwow::render::text::TextLayoutRequest& request,
    const float requested_scale,
    const openwow::render::text::FontStyle style) {
  if (font_path.empty() || pixel_height <= 0 || text.empty()) {
    return std::nullopt;
  }
  const float scale = requested_scale > 0.0f ? requested_scale : 1.0f;
  std::string path(font_path);
  std::replace(path.begin(), path.end(), '\\', '/');

  if (vfs != nullptr && !path.empty() && path.front() != '/') {
    path.insert(path.begin(), '/');
  }
  const auto face = AcquireMeasurementFace(
      vfs, path,
      std::max(
      2, openwow::render::ClampRetailFontPixelHeight(
             static_cast<int>(std::lround(pixel_height * scale)))), style);
  if (!face) return std::nullopt;

  auto scaled = request;
  scaled.maximum_width *= scale;
  scaled.maximum_height *= scale;
  scaled.line_spacing *= scale;
  scaled.line_height *= scale;
  scaled.inline_image_scale *= scale;
  scaled.continuation_indent *= scale;
  auto layout = openwow::render::text::LayoutText(*face, text, scaled);
  const float inverse = 1.0f / scale;
  layout.width *= inverse;
  layout.height *= inverse;
  for (auto& line : layout.lines) {
    line.width *= inverse;
    line.height *= inverse;
  }
  for (auto& element : layout.elements) {
    element.x *= inverse;
    element.y *= inverse;
    element.width *= inverse;
    element.height *= inverse;
  }
  return layout;
}

}
