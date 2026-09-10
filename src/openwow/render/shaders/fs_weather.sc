$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_weatherTex, 0);
uniform vec4 u_weatherParams;

void main()
{
    vec4 tex = texture2D(s_weatherTex, v_texcoord0);
    if (u_weatherParams.x > 0.5) {
        vec2 centered_uv = v_texcoord0 * 2.0 - 1.0;
        float coverage = clamp((1.0 - dot(centered_uv, centered_uv)) * 4.0, 0.0, 1.0);
        tex = vec4(1.0, 1.0, 1.0, coverage);
    }
    gl_FragColor = vec4(tex.rgb * v_color0.rgb, tex.a * v_color0.a);
}
