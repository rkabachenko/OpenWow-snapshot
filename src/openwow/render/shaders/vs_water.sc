$input a_position, a_normal, a_color0, a_texcoord0, a_texcoord1
$output v_texcoord0, v_texcoord1, v_primary, v_secondary, v_viewDist

#include <bgfx_shader.sh>

#include "liquid_params.sh"
#include "liquid_lighting.sh"
#include "world_fog.sh"

vec2 TransformLiquidTilingUv(vec2 uv, vec4 transform)
{
    float c = cos(transform.y);
    float s = sin(transform.y);
    vec2 scaled = uv * transform.x;
    return vec2(c * scaled.x - s * scaled.y,
                s * scaled.x + c * scaled.y) + transform.zw;
}

vec2 TransformLiquidDepthLaneUv(vec2 uv, vec4 transform)
{
    return uv * transform.xy + transform.zw;
}

void main()
{
    vec4 worldPosition = vec4(a_position, 1.0);

    vec3 normal = a_normal;
    vec3 lightDirection = u_liquidLightDir.xyz;
    float diffuseFactor = clamp(dot(normal, lightDirection), 0.0, 1.0);
    vec3 lighting = u_liquidAmbient.rgb +
                    u_liquidDiffuse.rgb * diffuseFactor +
                    AccumulateLiquidPointLighting(a_position, normal);

    vec3 viewDirection = normalize(u_liquidCamera.xyz - a_position);
    vec3 halfDirection = normalize(lightDirection + viewDirection);
    float specularFactor = pow(max(dot(normal, halfDirection), 0.0),
                               u_liquidSpecular.w);

    gl_Position = mul(u_modelViewProj, worldPosition);
    v_primary = vec4(lighting, 1.0) * a_color0;
    v_secondary = vec4(u_liquidSpecular.rgb * specularFactor, 1.0);

    v_texcoord0 = TransformLiquidDepthLaneUv(
        a_texcoord1, u_liquidUvTransform0);
    v_texcoord1 = TransformLiquidTilingUv(a_texcoord0, u_liquidUvTransform1);
    v_viewDist = openwowWorldFogDepth(
        mul(u_modelView, worldPosition).xyz);
}
