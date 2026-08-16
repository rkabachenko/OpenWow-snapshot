#ifndef OPENWOW_WORLD_FOG_SH
#define OPENWOW_WORLD_FOG_SH

float openwowWorldFogDepth(vec3 viewPosition)
{
    return max(viewPosition.z, 0.0);
}

float openwowLinearFogVisibility(vec4 fogParams, float fogDepth)
{
    return clamp((fogParams.y - fogDepth) /
                 max(fogParams.y - fogParams.x, 0.0001), 0.0, 1.0);
}

#endif
