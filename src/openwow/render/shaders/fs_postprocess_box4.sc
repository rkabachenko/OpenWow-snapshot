$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_ppTexColor, 0);

uniform vec4 u_texelSize;

void main()
{

    vec3 result =
        texture2D(s_ppTexColor, v_texcoord0 + vec2(-1.5, -1.5) * u_texelSize.xy).rgb +
        texture2D(s_ppTexColor, v_texcoord0 + vec2( 0.5, -1.5) * u_texelSize.xy).rgb +
        texture2D(s_ppTexColor, v_texcoord0 + vec2( 0.5,  0.5) * u_texelSize.xy).rgb +
        texture2D(s_ppTexColor, v_texcoord0 + vec2(-1.5,  0.5) * u_texelSize.xy).rgb;

    gl_FragColor = vec4(result * 0.25, 1.0);
}
