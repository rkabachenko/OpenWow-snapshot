
#ifndef OPENWOW_TERRAIN_LAYER_ARRAY
#define OPENWOW_TERRAIN_LAYER_ARRAY 0
#endif

#include <bgfx_shader.sh>
#include "world_fog.sh"

#include "terrain_params.sh"

vec3 safeNormalizeTerrain(vec3 value) {
    float lengthSquared = dot(value, value);
    return lengthSquared > 0.00000023841858
        ? value * (1.0 / sqrt(lengthSquared))
        : vec3_splat(0.0);
}

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));

    vec3 worldPosition = a_position;
    vec3 normal = safeNormalizeTerrain(a_normal);
    float directional = clamp(dot(normal, safeNormalizeTerrain(u_terrainSunDir.xyz)),
                              0.0, 1.0);
    vec3 lighting = u_terrainLightAmbient.rgb
                  + u_terrainLightDiffuse.rgb * directional;
    int pointLightCount = int(u_terrainPointLightCount.x + 0.5);
    for (int index = 0; index < 3; ++index) {
        if (index >= pointLightCount) break;
        vec3 toLight = terrainPointLightPosition(index).xyz - worldPosition;
        float distanceSquared = dot(toLight, toLight);
        float distance = sqrt(max(distanceSquared, 0.0));
        vec3 attenuation = terrainPointLightAttenuation(index).xyz;
        float denominator = attenuation.x + attenuation.y * distance
                          + attenuation.z * distanceSquared;
        float strength = clamp(dot(normal, safeNormalizeTerrain(toLight)), 0.0, 1.0)
                       / max(denominator, 0.0001);
        lighting += terrainPointLightDiffuse(index).rgb * strength;
    }

    float tileScale = u_terrainParams.x;
    v_texcoord0 = a_texcoord0 * tileScale;

    v_alphaUV = a_texcoord1;
    v_color0 = vec4(clamp(lighting, 0.0, 1.0) * a_color0.rgb, a_color0.a);

    v_viewDist = openwowWorldFogDepth(
        mul(u_modelView, vec4(a_position, 1.0)).xyz);
    v_worldPos = worldPosition;

#if OPENWOW_TERRAIN_LAYER_ARRAY

    v_layerSlice = a_texcoord2 * 255.0;
#endif
}
