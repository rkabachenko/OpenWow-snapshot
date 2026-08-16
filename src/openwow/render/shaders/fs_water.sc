$input v_texcoord0, v_texcoord1, v_primary, v_secondary, v_viewDist

#include <bgfx_shader.sh>

#include "liquid_params.sh"
#include "world_fog.sh"

SAMPLER2D(s_liquid0, 0);
SAMPLER2D(s_liquid1, 1);

void main()
{

    vec4 texture1 = texture2D(s_liquid1, v_texcoord1);
    vec4 texture0 = texture2D(s_liquid0, v_texcoord0);
    vec3 color = v_primary.rgb * texture0.rgb + texture1.rgb;
    color += texture1.a * (v_secondary.rgb + vec3_splat(0.25));
    float visibility = openwowLinearFogVisibility(u_liquidFogParams, v_viewDist);
    color = mix(u_liquidFogColor.rgb, color, visibility);
    gl_FragColor = vec4(color, v_primary.a * texture0.a);
}
