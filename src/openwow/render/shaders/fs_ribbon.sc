$input v_texcoord0, v_color0, v_viewDist

#include <bgfx_shader.sh>
#include "world_fog.sh"

SAMPLER2D(s_ribbonTex, 0);

uniform vec4 u_ribbonColor;
uniform vec4 u_fogColor;
uniform vec4 u_fogParams;

void main()
{
    vec4 tex = texture2D(s_ribbonTex, v_texcoord0);

    vec4 color = tex * v_color0 * u_ribbonColor;

    if (color.a < 0.01) discard;

    float fogFactor = openwowLinearFogVisibility(u_fogParams, v_viewDist);
    color.rgb = mix(u_fogColor.rgb, color.rgb, fogFactor);

    gl_FragColor = color;
}
