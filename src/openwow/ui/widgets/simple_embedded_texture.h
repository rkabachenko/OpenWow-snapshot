
#pragma once

#include "openwow/ui/widgets/simple_texture.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace openwow::ui::widgets {

struct CSimpleEmbeddedTexture {
  using DetachCallback = std::function<void(CSimpleTexture&)>;

  std::unique_ptr<CSimpleTexture> texture;
  DetachCallback detach;
  bool active{false};

  [[nodiscard]] CSimpleTexture* GetTexture() noexcept { return texture.get(); }
  [[nodiscard]] const CSimpleTexture* GetTexture() const noexcept {
    return texture.get();
  }

  void PrepareForRemoval() {
    if (!texture) return;
    texture->SetShown(false);
    if (detach) detach(*texture);
  }

  void Destroy() {
    texture.reset();
    detach = {};
    active = false;
  }
};

class CSimpleEmbeddedTextureList {
 public:
  CSimpleEmbeddedTextureList() = default;
  ~CSimpleEmbeddedTextureList() { DestroyAll(); }
  CSimpleEmbeddedTextureList(CSimpleEmbeddedTextureList&&) noexcept = default;
  CSimpleEmbeddedTextureList& operator=(CSimpleEmbeddedTextureList&&) noexcept =
      default;

  CSimpleEmbeddedTextureList(const CSimpleEmbeddedTextureList&) = delete;
  CSimpleEmbeddedTextureList& operator=(const CSimpleEmbeddedTextureList&) =
      delete;

  CSimpleEmbeddedTexture& Add(std::unique_ptr<CSimpleTexture> tex,
                              CSimpleEmbeddedTexture::DetachCallback detach =
                                  {}) {
    nodes_.emplace_back(
        CSimpleEmbeddedTexture{std::move(tex), std::move(detach), true});
    return nodes_.back();
  }

  void Destroy(size_t index) {
    if (index < nodes_.size()) {
      nodes_[index].Destroy();
      nodes_.erase(nodes_.begin() + static_cast<ptrdiff_t>(index));
    }
  }

  void DestroyAll() {
    for (auto& node : nodes_) {
      node.Destroy();
    }
    nodes_.clear();
  }

  [[nodiscard]] size_t GetCount() const noexcept { return nodes_.size(); }
  [[nodiscard]] bool IsEmpty() const noexcept { return nodes_.empty(); }

  [[nodiscard]] CSimpleEmbeddedTexture* GetNode(size_t index) {
    return index < nodes_.size() ? &nodes_[index] : nullptr;
  }
  [[nodiscard]] const CSimpleEmbeddedTexture* GetNode(size_t index) const {
    return index < nodes_.size() ? &nodes_[index] : nullptr;
  }

  template <typename Func>
  void ForEach(Func&& fn) {
    for (auto& node : nodes_) {
      if (node.active && node.texture) {
        fn(node);
      }
    }
  }

  template <typename Func>
  void ForEach(Func&& fn) const {
    for (const auto& node : nodes_) {
      if (node.active && node.texture) {
        fn(node);
      }
    }
  }

 private:
  std::vector<CSimpleEmbeddedTexture> nodes_;
};

}
