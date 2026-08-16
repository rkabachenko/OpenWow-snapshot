$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_ppTexColor, 0);
SAMPLER2D(s_ppTexBloom, 1);

uniform vec4 u_deathParams;

uniform vec4 u_sourceUvScale;

void main()
{
    vec3 blurred = texture2D(s_ppTexBloom,
                             v_texcoord0 * u_sourceUvScale.zw).rgb;
    vec3 scene = texture2D(s_ppTexColor,
                           v_texcoord0 * u_sourceUvScale.xy).rgb;
    vec3 base = scene + u_deathParams.a * blurred * blurred;
    float grey = clamp(dot(base, vec3(0.299, 0.587, 0.144)), 0.0, 1.0);
    float shaped = clamp(4.0 * grey * (1.0 - grey), 0.0, 1.0);
    gl_FragColor = vec4(u_deathParams.rgb * shaped + vec3_splat(grey), 1.0);
}
