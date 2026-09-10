$input a_position, a_normal, a_texcoord0, a_color0
$output v_color0, v_texcoord0, v_viewDist

#include <bgfx_shader.sh>
#include "world_fog.sh"

uniform vec4 u_detailDoodadLighting[3];
uniform vec4 u_detailDoodadParams[4];

vec3 safeNormalizeDetailDoodad(vec3 value)
{
    float lengthSquared = dot(value, value);
    return lengthSquared > 0.00000023841858
        ? value * (1.0 / sqrt(lengthSquared))
        : vec3(0.0, 0.0, 1.0);
}

void main()
{
    vec4 viewPosition = mul(u_modelView, vec4(a_position, 1.0));
    gl_Position = mul(u_proj, viewPosition);

    vec3 normal = safeNormalizeDetailDoodad(a_normal);
    vec3 surfaceToLight = safeNormalizeDetailDoodad(
        u_detailDoodadLighting[0].xyz);
    float directional = clamp(dot(normal, surfaceToLight), 0.0, 1.0);
    vec3 lighting = u_detailDoodadLighting[1].rgb
                  + u_detailDoodadLighting[2].rgb * directional;
    float terrainShadow = 0.7 + 0.3 * a_color0.a;

    float distanceToCamera = length(
        a_position - u_detailDoodadParams[0].xyz);
    float distanceFade = clamp(
        1.0 - (distanceToCamera - u_detailDoodadParams[1].x)
              * u_detailDoodadParams[1].y,
        0.0, 1.0);
    v_color0 = vec4(
        clamp(a_color0.rgb * lighting * terrainShadow, 0.0, 1.0),
        distanceFade);
    v_texcoord0 = a_texcoord0;
    v_viewDist = openwowWorldFogDepth(viewPosition.xyz);
}
