#include "openwow/data/blp/blp_decoder.h"
#include "openwow/data/blp/blp_loader.h"

#include <filesystem>
#include <fstream>

namespace openwow::data::blp {

RgbaImage DecodeBlp(const std::vector<std::uint8_t>& bytes) {
  RgbaImage out{.ok = false};

  auto decoded = openwow::data::BLPLoader::Load(bytes.data(), bytes.size());
  if (!decoded.has_value()) {
    out.error = "BLPLoader::Load failed";
    return out;
  }

  auto rgba = openwow::data::BLPLoader::DecompressToRGBA8(*decoded);
  if (rgba.mips.empty()) {
    out.error = "no mip levels decoded";
    return out;
  }

  const auto& mip0 = rgba.mips[0];
  out.ok = true;
  out.width = mip0.width;
  out.height = mip0.height;
  out.pixels_rgba = mip0.data;
  return out;
}

RgbaImage DecodeBlpFile(const std::string& file_path) {
  RgbaImage out{.ok = false};
  std::ifstream in(std::filesystem::path(file_path), std::ios::in | std::ios::binary);
  if (!in.is_open()) {
    out.error = "failed to open BLP file";
    return out;
  }

  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
  return DecodeBlp(bytes);
}

}
