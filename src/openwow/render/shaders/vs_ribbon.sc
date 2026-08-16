$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_color0, v_viewDist

#include <bgfx_shader.sh>
#include "world_fog.sh"

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_texcoord0 = a_texcoord0;
    v_color0    = a_color0;
    v_viewDist = openwowWorldFogDepth(
        mul(u_modelView, vec4(a_position, 1.0)).xyz);
}
