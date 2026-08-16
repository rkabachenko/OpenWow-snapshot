$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

uniform vec4 u_decalParams;

void main()
{
    vec4 clip = mul(u_viewProj, vec4(a_position.xyz, 1.0) );
    clip.z -= u_decalParams.x * clip.w;
    gl_Position = clip;

    v_texcoord0 = vec4(a_texcoord0.xyz, 0.0);
    v_color0    = a_color0;
}
