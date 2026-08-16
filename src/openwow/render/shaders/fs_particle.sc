$input v_color0, v_texcoord0, v_viewDist

#include <bgfx_shader.sh>
#include "world_fog.sh"

SAMPLER2D(s_texColor, 0);

uniform vec4 u_particleFlags;
uniform vec4 u_fogParams;
uniform vec4 u_fogColor;

void main()
{
    vec4 result;
    if (u_particleFlags.x > 0.5)
    {
        vec4 texel = texture2D(s_texColor, v_texcoord0);
        result = texel * v_color0;
    }
    else
    {
        vec2 uv = v_texcoord0 * 2.0 - 1.0;
        float dist2 = dot(uv, uv);
        float alpha = smoothstep(1.0, 0.2, dist2) * v_color0.a;
        result = vec4(v_color0.rgb, alpha);
    }

    result.a *= u_particleFlags.w;

    if (result.a < u_particleFlags.z) discard;

    if (u_particleFlags.y < 0.5)
    {
        float fogFactor = openwowLinearFogVisibility(u_fogParams, v_viewDist);
        result.rgb = mix(u_fogColor.rgb, result.rgb, fogFactor);
    }

    gl_FragColor = result;
}
