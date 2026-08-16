#ifndef OPENWOW_POSTPROCESS_COMPOSITE_SH
#define OPENWOW_POSTPROCESS_COMPOSITE_SH

vec4 openwowCompositePassGlow(vec4 scene, vec3 blurredScene, vec4 params)
{
    vec3 mixedScene = mix(scene.rgb, blurredScene, params.x);
    return vec4(mixedScene + blurredScene * blurredScene * params.y, 1.0);
}

#endif
