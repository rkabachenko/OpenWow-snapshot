$input v_texcoord0, v_primary, v_viewDist

#include <bgfx_shader.sh>

#include "liquid_params.sh"
#include "world_fog.sh"

SAMPLER2D(s_liquid0, 0);

void main()
{

    vec3 color = v_primary.rgb * texture2D(s_liquid0, v_texcoord0).rgb;
    float visibility = openwowLinearFogVisibility(u_liquidFogParams, v_viewDist);
    color = mix(u_liquidFogColor.rgb, color, visibility);
    gl_FragColor = vec4(color, 1.0);
}
