$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_weatherTex, 0);

void main()
{
    vec4 tex = texture2D(s_weatherTex, v_texcoord0);
    gl_FragColor = vec4(tex.rgb * v_color0.rgb, tex.a * v_color0.a);
}
