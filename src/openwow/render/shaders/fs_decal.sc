$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_decalTex, 0);

void main()
{
    vec4 texel = texture2D(s_decalTex, v_texcoord0.xy);

    float s = v_texcoord0.z;
    float fade = clamp(min(s * 6.0, (1.0 - s) * 6.0), 0.0, 1.0);

    vec4 color = texel * v_color0;
    color.a *= fade;
    gl_FragColor = color;
}
