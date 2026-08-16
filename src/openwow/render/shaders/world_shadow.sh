#ifndef OPENWOW_WORLD_SHADOW_SH
#define OPENWOW_WORLD_SHADOW_SH

float openwowSampleWorldShadow(vec3 worldPosition, float depthBias)
{
    vec4 shadowPosition = mul(u_shadowMtx, vec4(worldPosition, 1.0));
    vec3 projected = shadowPosition.xyz / shadowPosition.w;
    if (any(greaterThan(projected.xy, vec2_splat(1.0)))
     || any(lessThan(projected.xy, vec2_splat(0.0)))) {
        return 1.0;
    }
    return shadow2D(s_shadowMap, vec3(projected.xy, projected.z - depthBias));
}

#endif
