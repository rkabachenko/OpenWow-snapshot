#ifndef OPENWOW_LIQUID_LIGHTING_SH
#define OPENWOW_LIQUID_LIGHTING_SH

#include "liquid_params.sh"

#ifndef OPENWOW_LIQUID_POINT_LIGHT_COUNT
#define OPENWOW_LIQUID_POINT_LIGHT_COUNT 0
#endif

#if OPENWOW_LIQUID_POINT_LIGHT_COUNT > 0
vec3 LiquidPointLightContribution(int index, vec3 worldPosition, vec3 normal)
{
    vec3 toLight = liquidPointLightPosition(index).xyz - worldPosition;
    float distanceToLight = length(toLight);
    vec3 lightDirection = toLight / distanceToLight;
    float diffuseFactor = clamp(dot(normal, lightDirection), 0.0, 1.0);
    vec3 attenuation = liquidPointLightAttenuation(index).xyz;
    float denominator = attenuation.x +
                        distanceToLight * attenuation.y +
                        distanceToLight * distanceToLight * attenuation.z;
    return liquidPointLightDiffuse(index).rgb * diffuseFactor / denominator;
}
#endif

vec3 AccumulateLiquidPointLighting(vec3 worldPosition, vec3 normal)
{
    vec3 lighting = vec3(0.0, 0.0, 0.0);
#if OPENWOW_LIQUID_POINT_LIGHT_COUNT > 0
    lighting += LiquidPointLightContribution(0, worldPosition, normal);
#endif
#if OPENWOW_LIQUID_POINT_LIGHT_COUNT > 1
    lighting += LiquidPointLightContribution(1, worldPosition, normal);
#endif
#if OPENWOW_LIQUID_POINT_LIGHT_COUNT > 2
    lighting += LiquidPointLightContribution(2, worldPosition, normal);
#endif
    return lighting;
}

#endif
