#pragma once

#include "openwow/vfs/virtual_file_system.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace openwow::ui::glue {

struct GlueFontStyle {
  std::string name;
  std::string inherits;
  std::string font_file;
  int height_px{0};
  float color_r{1.0F};
  float color_g{1.0F};
  float color_b{1.0F};
  float color_a{1.0F};
  float shadow_x_px{0.0F};
  float shadow_y_px{0.0F};
  float shadow_r{0.0F};
  float shadow_g{0.0F};
  float shadow_b{0.0F};
  float shadow_a{1.0F};
  float spacing_px{0.0F};
  std::string justify_h;
  std::string justify_v;
  std::string outline;
  bool monochrome{false};
  bool non_space_wrap{false};
  bool indented_word_wrap{false};
  bool has_font_file{false};
  bool has_height{false};
  bool has_color{false};
  bool has_shadow{false};
  bool has_spacing{false};
  bool has_justify_h{false};
  bool has_justify_v{false};
  bool has_outline{false};
  bool has_monochrome{false};
  bool has_non_space_wrap{false};
  bool has_indented_word_wrap{false};
};

class GlueFontRegistry {
 public:
  static std::optional<GlueFontRegistry> LoadFromVfs(const openwow::vfs::VirtualFileSystem& vfs);

  std::optional<GlueFontStyle> Resolve(const std::string& style_name) const;
  bool HasStyle(const std::string& style_name) const;

 private:
  std::optional<GlueFontStyle> ResolveImpl(const std::string& style_name,
                                          std::unordered_map<std::string, GlueFontStyle>* memo,
                                          std::unordered_map<std::string, bool>* stack) const;

  std::unordered_map<std::string, GlueFontStyle> styles_;
};

}
