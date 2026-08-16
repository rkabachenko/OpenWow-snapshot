$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_uiTex, 0);

void main()
{
    vec4 color = texture2D(s_uiTex, v_texcoord0) * v_color0;
    if (color.a <= 0.003921569) discard;
    gl_FragColor = color;
}
