$input a_position, a_normal, a_texcoord0, a_texcoord1
$output v_procUv0, v_procUv1, v_procUv2, v_worldPosition, v_tangent, v_bitangent, v_surfaceNormal, v_viewDist

#include <bgfx_shader.sh>

#define OPENWOW_LIQUID_PROCEDURAL 1
#include "liquid_params.sh"
#include "world_fog.sh"

vec2 TransformProceduralLiquidUv(vec2 uv, vec4 transform)
{
    return vec2(dot(uv, transform.xz), dot(uv, transform.yw));
}

float EvaluateLiquidWave(vec2 position, vec2 sampleOffset, vec4 record,
                         float inverseSpatialPeriod,
                         float inverseFalloffRadius,
                         float phase, float amplitude)
{
    float radialWeight = clamp(
        1.0 - length(position - record.xy + sampleOffset) *
              inverseFalloffRadius,
        0.0, 1.0);
    float angle = dot(position * inverseSpatialPeriod + sampleOffset,
                      record.zw) - phase;

    float wrapped = fract(angle * 0.15915491 - 0.25);

    float offset = wrapped < 0.25
                       ? -wrapped
                       : (wrapped >= 0.75 ? 1.0 - wrapped
                                          : 0.5 - wrapped);
    float squared = offset * offset;
    float cosine = 24.980801 * squared - 60.145809;
    cosine = cosine * squared + 85.453789;
    cosine = cosine * squared - 64.939346;
    cosine = cosine * squared + 19.73921;
    cosine = -(cosine * squared - 1.0);
    float sine = wrapped >= 0.25 && wrapped < 0.75 ? -cosine : cosine;
    return sine * radialWeight * amplitude;
}

float EvaluateLiquidHeight(vec2 position, vec2 sampleOffset)
{
    return EvaluateLiquidWave(
               position, sampleOffset, liquidWaveRecord(0),
               u_liquidWaveInverseSpatialPeriod.x,
               u_liquidWaveInverseFalloff.x,
               u_liquidWavePhases.x, u_liquidWaveAmplitudes.x) +
           EvaluateLiquidWave(
               position, sampleOffset, liquidWaveRecord(1),
               u_liquidWaveInverseSpatialPeriod.y,
               u_liquidWaveInverseFalloff.y,
               u_liquidWavePhases.y, u_liquidWaveAmplitudes.y) +
           EvaluateLiquidWave(
               position, sampleOffset, liquidWaveRecord(2),
               u_liquidWaveInverseSpatialPeriod.z,
               u_liquidWaveInverseFalloff.z,
               u_liquidWavePhases.z, u_liquidWaveAmplitudes.z);
}

void main()
{
    vec4 worldPosition = vec4(a_position, 1.0);

    vec2 horizontalPosition = a_position.xy;
    float baseHeight = EvaluateLiquidHeight(
        horizontalPosition, vec2(0.0, 0.0));
    float tangentHeight = EvaluateLiquidHeight(
        horizontalPosition, vec2(0.5, 0.0));
    float bitangentHeight = EvaluateLiquidHeight(
        horizontalPosition, vec2(0.0, 0.5));
    vec3 tangent = normalize(vec3(1.0, 0.0, tangentHeight - baseHeight));
    vec3 bitangent = normalize(vec3(0.0, 1.0, bitangentHeight - baseHeight));
    vec3 surfaceNormal = normalize(cross(tangent, bitangent));

    vec2 procUv0 = TransformProceduralLiquidUv(
        a_texcoord0, liquidProceduralUvTransform(0));
    vec2 procUv1 = TransformProceduralLiquidUv(
        a_texcoord0, liquidProceduralUvTransform(1));
    vec2 procUv2 = TransformProceduralLiquidUv(
        a_texcoord0, liquidProceduralUvTransform(2));
    vec2 procUv3 = TransformProceduralLiquidUv(
        a_texcoord0, liquidProceduralUvTransform(3));
    vec2 procUv4 = TransformProceduralLiquidUv(
        a_texcoord1, liquidProceduralUvTransform(4));
    vec2 procUv5 = TransformProceduralLiquidUv(
        a_texcoord0, liquidProceduralUvTransform(5));

    v_procUv0 = vec4(procUv0, procUv1.yx);
    v_procUv1 = vec4(procUv2, procUv3.yx);
    v_procUv2 = vec4(procUv4, procUv5.yx);
    v_worldPosition = a_position;
    v_tangent = tangent;
    v_bitangent = bitangent;
    v_surfaceNormal = surfaceNormal;
    v_viewDist = openwowWorldFogDepth(
        mul(u_modelView, worldPosition).xyz);
    gl_Position = mul(u_modelViewProj, worldPosition);
}
