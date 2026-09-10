$input a_position
$output v_viewDepth

#include <bgfx_shader.sh>
#include "world_fog.sh"

void main()
{
    vec4 viewPosition = mul(u_modelView, vec4(a_position, 1.0));
    gl_Position = mul(u_proj, viewPosition);
    v_viewDepth = openwowWorldFogDepth(viewPosition.xyz);
}
