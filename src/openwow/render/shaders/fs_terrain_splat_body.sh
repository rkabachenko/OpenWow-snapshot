
#ifndef OPENWOW_TERRAIN_LAYER_ARRAY
#define OPENWOW_TERRAIN_LAYER_ARRAY 0
#endif

#include <bgfx_shader.sh>
#include "world_fog.sh"

#include "terrain_params.sh"

#if OPENWOW_TERRAIN_LAYER_ARRAY

SAMPLER2DARRAY(s_terrainLayers, 0);
#else

SAMPLER2D(s_terrainTex0, 0);
SAMPLER2D(s_terrainTex1, 1);
SAMPLER2D(s_terrainTex2, 2);
SAMPLER2D(s_terrainTex3, 3);
#endif

SAMPLER2D(s_terrainAlpha, 4);

SAMPLER2DSHADOW(s_shadowMap, 5);

uniform mat4 u_shadowMtx;
uniform vec4 u_shadowParams;

#include "world_shadow.sh"

void main()
{

    vec4 alphaBytes = texture2D(s_terrainAlpha, v_alphaUV);

#if OPENWOW_TERRAIN_LAYER_ARRAY

    vec4 layerSlice = floor(v_layerSlice + vec4_splat(0.5));
    vec4 c0 = texture2DArray(s_terrainLayers, vec3(v_texcoord0, layerSlice.x));
    vec4 c1 = texture2DArray(s_terrainLayers, vec3(v_texcoord0, layerSlice.y));
    vec4 c2 = texture2DArray(s_terrainLayers, vec3(v_texcoord0, layerSlice.z));
    vec4 c3 = texture2DArray(s_terrainLayers, vec3(v_texcoord0, layerSlice.w));
#else
    vec4 c0 = texture2D(s_terrainTex0, v_texcoord0);
    vec4 c1 = texture2D(s_terrainTex1, v_texcoord0);
    vec4 c2 = texture2D(s_terrainTex2, v_texcoord0);
    vec4 c3 = texture2D(s_terrainTex3, v_texcoord0);
#endif

    vec4 blended = mix(c0, c1, alphaBytes.r);
    blended = mix(blended, c2, alphaBytes.g);
    blended = mix(blended, c3, alphaBytes.b);

    float shadowVisibility = mix(1.0, alphaBytes.a, u_terrainShadowMod.a);
    if (u_shadowParams.z > 0.0) {
        float projectedVisibility = openwowSampleWorldShadow(v_worldPos, u_shadowParams.x);
        projectedVisibility = mix(1.0, projectedVisibility, u_shadowParams.z);
        shadowVisibility = min(shadowVisibility, projectedVisibility);
    }
    vec3 shadowModulate = mix(u_terrainShadowMod.rgb, vec3_splat(1.0), shadowVisibility);
    vec3 litColor = blended.rgb * v_color0.rgb * u_terrainColor.rgb
                  * (2.0 * shadowModulate);

    float fogFactor = openwowLinearFogVisibility(u_terrainFogParams, v_viewDist);
    gl_FragColor = vec4(mix(u_terrainFogColor.rgb, litColor, fogFactor), 1.0);
}
