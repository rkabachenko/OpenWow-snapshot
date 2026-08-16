#include "openwow/data/image/image_decoder.h"

#include "openwow/data/blp/blp_decoder.h"
#include "openwow/data/blp/blp_info.h"
#include "openwow/data/image/jpeg_decoder.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#include "stb/stb_image.h"

#include <cstring>

namespace openwow::data::image {

namespace {

bool LooksLikeBlp2(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 4) {
    return false;
  }
  return bytes[0] == 'B' && bytes[1] == 'L' && bytes[2] == 'P' && bytes[3] == '2';
}

bool LooksLikeJpeg(const std::vector<std::uint8_t>& bytes) {
  return bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xD8;
}

}

DecodedImage DecodeImage(const std::vector<std::uint8_t>& bytes) {
  DecodedImage out;
  if (bytes.empty()) {
    out.ok = false;
    out.error = "empty image buffer";
    return out;
  }

  if (LooksLikeBlp2(bytes)) {
    if (const auto info =
            openwow::data::BLPInfoReader::ParseHeader(bytes.data(), bytes.size())) {
      out.has_alpha_channel =
          info->alphaType != openwow::data::BLPAlphaType::NoAlpha;
    }
    const auto decoded = openwow::data::blp::DecodeBlp(bytes);
    out.ok = decoded.ok;
    out.error = decoded.error;
    out.width = static_cast<int>(decoded.width);
    out.height = static_cast<int>(decoded.height);
    out.pixels_rgba = decoded.pixels_rgba;
    if (out.ok && (out.width <= 0 || out.height <= 0 || out.pixels_rgba.empty())) {
      out.ok = false;
      out.error = "BLP decode returned empty pixel buffer";
    }
    return out;
  }

  if (LooksLikeJpeg(bytes)) {
    const auto decoded = openwow::data::DecodeJpeg(bytes);
    out.ok = decoded.ok;
    out.error = decoded.ok ? std::string() : std::string(openwow::data::ToString(decoded.error));
    out.width = decoded.width;
    out.height = decoded.height;
    out.has_alpha_channel =
        decoded.channels_in_file == 2 || decoded.channels_in_file == 4;
    out.pixels_rgba = decoded.pixels_rgba;
    return out;
  }

  int w = 0;
  int h = 0;
  int channels_in_file = 0;
  stbi_uc* pixels =
      stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                            static_cast<int>(bytes.size()),
                            &w,
                            &h,
                            &channels_in_file,
                            4);
  if (pixels == nullptr || w <= 0 || h <= 0) {
    out.ok = false;
    const char* reason = stbi_failure_reason();
    out.error = reason ? std::string(reason) : std::string("stb_image decode failed");
    if (pixels != nullptr) {
      stbi_image_free(pixels);
    }
    return out;
  }

  out.ok = true;
  out.width = w;
  out.height = h;
  out.has_alpha_channel = channels_in_file == 2 || channels_in_file == 4;
  out.pixels_rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
  std::memcpy(out.pixels_rgba.data(), pixels, out.pixels_rgba.size());
  stbi_image_free(pixels);
  return out;
}

}
