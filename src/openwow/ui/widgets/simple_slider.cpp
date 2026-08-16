
#include "openwow/ui/widgets/simple_slider.h"

#include "openwow/ui/widgets/simple_texture.h"

namespace openwow::ui::widgets {

CSimpleSlider::~CSimpleSlider() {
  delete thumbTexture_;
  thumbTexture_ = nullptr;
}

void CSimpleSlider::SetThumbTexture(CSimpleTexture* texture,
                                    DrawLayer layer) {
  if (texture == thumbTexture_) {
    return;
  }

  delete thumbTexture_;

  if (texture != nullptr) {
    texture->SetParent(this);
    texture->SetDrawLayer(layer);
    texture->Show();
    texture->ClearAllPoints();
  }

  flags_ |= kDirty;
  thumbTexture_ = texture;
}

bool CSimpleSlider::SetThumbTextureFromFile(const char* filePath) {

  if (thumbTexture_ != nullptr) {
    thumbTexture_->SetTexture(filePath ? filePath : "");
    return true;
  }

  auto* tex = new CSimpleTexture();
  tex->SetDrawLayer(DrawLayer::Artwork);

  if (!tex->SetTexture(filePath ? filePath : "")) {
    delete tex;
    return false;
  }

  SetThumbTexture(tex, DrawLayer::Overlay);
  return true;
}

}
