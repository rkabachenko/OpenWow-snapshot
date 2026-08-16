$input v_texcoord0, v_texcoord1, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texMinimap, 0);
SAMPLER2D(s_texMinimapMask, 1);

void main()
{
    vec4 tex = texture2D(s_texMinimap, v_texcoord0);
    vec4 mask_texel = texture2D(s_texMinimapMask, v_texcoord1);
    float mask = max(mask_texel.a, mask_texel.r);

    if (mask < 0.01)
    {
        discard;
    }

    gl_FragColor = vec4(tex.rgb * v_color0.rgb, tex.a * mask * v_color0.a);
}
