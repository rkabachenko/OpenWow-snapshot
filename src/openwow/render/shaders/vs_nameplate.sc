$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

uniform vec4 u_billboardParams;

void main()
{

    vec2 ndc;
    ndc.x = (a_position.x / u_billboardParams.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (a_position.y / u_billboardParams.y) * 2.0;

    gl_Position = vec4(ndc, a_position.z, 1.0);
    v_texcoord0 = a_texcoord0;
    v_color0    = a_color0;
}
