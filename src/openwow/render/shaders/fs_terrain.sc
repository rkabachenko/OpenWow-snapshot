$input v_texcoord0, v_color0, v_viewDist, v_worldPos

#include <bgfx_shader.sh>
#include "world_fog.sh"

#include "terrain_params.sh"

#include "world_shadow.sh"

void main()
{
    float shadowVisibility = openwowSampleWorldShadow(v_worldPos);
    vec3 shadowModulate = mix(u_terrainShadowMod.rgb, vec3_splat(1.0), shadowVisibility);
    vec3 litColor = u_terrainColor.rgb * v_color0.rgb * (2.0 * shadowModulate);

    float fogFactor = openwowLinearFogVisibility(u_terrainFogParams, v_viewDist);
    gl_FragColor = vec4(mix(u_terrainFogColor.rgb, litColor, fogFactor), u_terrainColor.a);
}
