
#include "openwow/render/scene/projected_decal_conjugation.h"

#include <bgfx/bgfx.h>

namespace openwow::render {

float ResolveDecalDepthBias() {
  const auto* const caps = bgfx::getCaps();
  const bool homogeneous_depth = caps != nullptr && caps->homogeneousDepth;
  return homogeneous_depth ? kDecalDepthBiasNdc * 2.0f : kDecalDepthBiasNdc;
}

}
