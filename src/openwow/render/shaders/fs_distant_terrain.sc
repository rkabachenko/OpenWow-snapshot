$input v_viewDepth

#include <bgfx_shader.sh>
#include "world_fog.sh"

#include "distant_terrain_params.sh"

void main()
{
    float fogVisibility = openwowLinearFogVisibility(u_distantTerrainFogParams, v_viewDepth);
    gl_FragColor = vec4(mix(u_distantTerrainFogColor.rgb, vec3_splat(1.0), fogVisibility), 1.0);
}
