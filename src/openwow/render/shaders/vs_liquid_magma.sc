$input a_position, a_color0, a_texcoord0
$output v_texcoord0, v_primary, v_viewDist

#include <bgfx_shader.sh>

#include "liquid_params.sh"
#include "world_fog.sh"

void main()
{
    vec4 worldPosition = vec4(a_position, 1.0);
    gl_Position = mul(u_modelViewProj, worldPosition);
    v_texcoord0 = a_texcoord0 * u_liquidUvTransform0.x +
                  u_liquidUvTransform0.zw;
    v_primary = a_color0;
    v_viewDist = openwowWorldFogDepth(
        mul(u_modelView, worldPosition).xyz);
}
