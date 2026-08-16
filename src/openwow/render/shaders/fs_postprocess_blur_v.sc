$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_ppTexColor, 0);

uniform vec4 u_texelSize;

void main()
{

    vec4 result =
        texture2D(s_ppTexColor, v_texcoord0 + vec2(0.0, -0.5 * u_texelSize.y)) * 0.375;
    result +=
        texture2D(s_ppTexColor, v_texcoord0 + vec2(0.0, -2.5 * u_texelSize.y)) * 0.125;
    result +=
        texture2D(s_ppTexColor, v_texcoord0 + vec2(0.0,  0.5 * u_texelSize.y)) * 0.375;
    result +=
        texture2D(s_ppTexColor, v_texcoord0 + vec2(0.0,  2.5 * u_texelSize.y)) * 0.125;
    gl_FragColor = result;
}
