$input v_texcoord0, v_color0, v_viewDist, v_worldPos

#include <bgfx_shader.sh>
#include "world_fog.sh"

#include "terrain_params.sh"

uniform mat4 u_shadowMtx;
uniform vec4 u_shadowParams;

SAMPLER2DSHADOW(s_shadowMap, 5);

#include "world_shadow.sh"

void main()
{
    float shadowVisibility = 1.0;
    if (u_shadowParams.z > 0.0) {
        shadowVisibility = openwowSampleWorldShadow(v_worldPos, u_shadowParams.x);
        shadowVisibility = mix(1.0, shadowVisibility, u_shadowParams.z);
    }
    vec3 shadowModulate = mix(u_terrainShadowMod.rgb, vec3_splat(1.0), shadowVisibility);
    vec3 litColor = u_terrainColor.rgb * v_color0.rgb * (2.0 * shadowModulate);

    float fogFactor = openwowLinearFogVisibility(u_terrainFogParams, v_viewDist);
    gl_FragColor = vec4(mix(u_terrainFogColor.rgb, litColor, fogFactor), u_terrainColor.a);
}
