
#pragma once

#include "openwow/render/models/animation/model_render_callback_pipeline.h"

namespace openwow::render {

void ApplyModelFfxLightingRenderCallback(
    void* model_instance,
    ModelRenderCallbackContext& render_ctx,
    const void* user_ctx);

}
