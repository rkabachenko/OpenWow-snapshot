
#pragma once

#include <cstdint>
#include <string>

namespace openwow::ui::widgets {

class CBackdrop {
 public:
  CBackdrop() = default;
  ~CBackdrop() = default;

  void SetBgFile(const std::string& f)    { bg_file_ = f; }
  const std::string& GetBgFile() const    { return bg_file_; }

  void SetEdgeFile(const std::string& f)  { edge_file_ = f; }
  const std::string& GetEdgeFile() const  { return edge_file_; }

  void SetTile(bool t) { tile_ = t; }
  bool GetTile() const { return tile_; }

  void SetTileSize(float s) { tile_size_ = s; }
  float GetTileSize() const { return tile_size_; }

  void SetEdgeSize(float s) { edge_size_ = s; }
  float GetEdgeSize() const { return edge_size_; }

  void SetInsets(float left, float right, float top, float bottom) {
    inset_left_ = left; inset_right_ = right;
    inset_top_ = top; inset_bottom_ = bottom;
  }
  void GetInsets(float& l, float& r, float& t, float& b) const {
    l = inset_left_; r = inset_right_; t = inset_top_; b = inset_bottom_;
  }

  void SetBgColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    bg_color_r_ = r; bg_color_g_ = g; bg_color_b_ = b; bg_color_a_ = a;
  }

  void SetBorderColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    border_color_r_ = r; border_color_g_ = g; border_color_b_ = b; border_color_a_ = a;
  }

  void SetAlphaMode(int mode) { alpha_mode_ = mode; }
  int GetAlphaMode() const { return alpha_mode_; }

  void SetEdgeMask(uint8_t mask) { edge_mask_ = mask; }
  uint8_t GetEdgeMask() const { return edge_mask_; }

  void CreateTextureElements(void* parent_frame);

  bool IsCreated() const { return created_; }

 private:
  std::string bg_file_;
  std::string edge_file_;
  bool tile_{false};
  float tile_size_{0.0f};
  float edge_size_{0.0f};
  float inset_left_{0.0f}, inset_right_{0.0f};
  float inset_top_{0.0f}, inset_bottom_{0.0f};
  uint8_t bg_color_r_{0}, bg_color_g_{0}, bg_color_b_{0}, bg_color_a_{255};
  uint8_t border_color_r_{255}, border_color_g_{255}, border_color_b_{255}, border_color_a_{255};
  int alpha_mode_{2};

  uint8_t edge_mask_{0xFF};
  bool created_{false};
};

}
