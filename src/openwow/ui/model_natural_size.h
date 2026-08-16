#pragma once

#include "openwow/ui/framexml/ui_frame.h"

#include <array>
#include <optional>
#include <string>

namespace openwow::ui {

struct ModelBoundingBox {
  std::array<float, 3> min{};
  std::array<float, 3> max{};
};

class ModelNaturalSizeSource {
 public:
  virtual ~ModelNaturalSizeSource() = default;

  [[nodiscard]] virtual std::optional<ModelBoundingBox> ResolveModelBoundingBox(
      const std::string& model_path) = 0;
};

struct ModelNaturalSize {

  std::optional<float> width;
  std::optional<float> height;

  bool pending{false};
};

[[nodiscard]] inline ModelNaturalSize ResolveModelNaturalSize(
    ModelNaturalSizeSource* const source, const std::string& model_path) {
  ModelNaturalSize natural;
  if (model_path.empty() || source == nullptr) {
    return natural;
  }
  const auto box = source->ResolveModelBoundingBox(model_path);
  if (!box.has_value()) {
    natural.pending = true;
    return natural;
  }
  const float width = box->max[0] - box->min[0];
  const float height = box->max[1] - box->min[1];
  if (width != 0.0F) {
    natural.width = width;
  }
  if (height != 0.0F) {
    natural.height = height;
  }
  return natural;
}

inline bool SyncModelNaturalSize(framexml::UiFrame& frame,
                                 ModelNaturalSizeSource* const source,
                                 const std::string& model_path) {

  if (!model_path.empty() && frame.model_natural_size_path == model_path) {
    return false;
  }
  const ModelNaturalSize natural = ResolveModelNaturalSize(source, model_path);
  if (natural.pending) {
    frame.model_natural_width.reset();
    frame.model_natural_height.reset();
    frame.model_natural_size_path.clear();
    return true;
  }
  frame.model_natural_width = natural.width;
  frame.model_natural_height = natural.height;
  frame.model_natural_size_path = model_path;
  return false;
}

}
