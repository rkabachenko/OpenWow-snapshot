$input v_procUv0, v_procUv1, v_procUv2, v_worldPosition, v_tangent, v_bitangent, v_surfaceNormal, v_viewDist

#include <bgfx_shader.sh>

#define OPENWOW_LIQUID_PROCEDURAL 1
#include "liquid_params.sh"
#include "world_fog.sh"

SAMPLERCUBE(s_liquid0, 0);
SAMPLERCUBE(s_liquid1, 1);
SAMPLER2D(s_liquid2, 2);
SAMPLER2D(s_liquid3, 3);
SAMPLER2D(s_liquid4, 4);
SAMPLER2D(s_liquid5, 5);

vec3 DecodeLiquidNormal(vec2 encoded)
{
    vec2 xy = encoded * 2.0 - 1.0;

    return vec3(xy, sqrt(1.0 - dot(xy, xy)));
}

void main()
{

    vec4 firstHeight = texture2D(s_liquid5, v_procUv1.wz);
    vec3 normalA = DecodeLiquidNormal(firstHeight.wz);
    vec4 secondHeight = texture2D(s_liquid5, v_procUv1.xy);
    vec3 normalB = DecodeLiquidNormal(secondHeight.xy);
    vec3 combined = normalize(normalA + normalB);

    vec4 detailSample = texture2D(s_liquid4, v_procUv0.xy);
    vec3 detail = DecodeLiquidNormal(detailSample.xy);
    float detailWeight = clamp(
        length(detail.xy) * u_liquidMaterialParams(0).x + 0.5,
        0.0, 1.0);

    vec3 tangentNormal = mix(combined, detail, detailWeight);

    vec3 surfaceNormal =
        tangentNormal.x * v_tangent +
        tangentNormal.y * v_bitangent +
        tangentNormal.z * v_surfaceNormal;
    vec3 viewOffset = v_worldPosition - u_liquidCamera.xyz;

    vec3 viewRay = viewOffset / length(viewOffset.xy);
    vec3 reflectedRay = viewRay -
                        2.0 * surfaceNormal * dot(surfaceNormal, viewRay);
    float nDotV = clamp(dot(-viewRay, surfaceNormal), 0.0, 1.0);
    float fresnel = (1.0 - nDotV) * (1.0 - nDotV);

    vec4 base = texture2D(s_liquid2, v_procUv2.xy);

    vec3 firstReflection = textureCube(s_liquid0, -surfaceNormal.xzy).rgb;
    vec3 color = base.rgb + firstReflection;
    vec3 secondReflection = textureCube(s_liquid1,
                                        reflectedRay.xzy).rgb;
    float reflectionWeight = fresnel * u_liquidMaterialParams(0).w +
                             u_liquidMaterialParams(0).z;
    color = mix(color, secondReflection, reflectionWeight);

    vec3 halfDirection = normalize(viewRay + u_liquidLightRay.xyz);
    float specular = pow(clamp(dot(halfDirection, surfaceNormal),
                               0.0, 1.0),
                         u_liquidSpecular.w);
    float specularMask = texture2D(s_liquid3, v_procUv2.wz).a;
    float specularScale = fresnel * u_liquidMaterialParams(1).z +
                          u_liquidMaterialParams(1).y;
    color += u_liquidSpecular.rgb * specular * specularMask *
             specularScale * 4.0;

    float visibility = openwowLinearFogVisibility(u_liquidFogParams, v_viewDist);
    color = mix(u_liquidFogColor.rgb, color, visibility);
    float alpha = base.a + fresnel * u_liquidMaterialParams(1).x;
    gl_FragColor = vec4(color, alpha);
}
