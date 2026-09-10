$input v_color0, v_texcoord0, v_viewDist

#include <bgfx_shader.sh>
#include "world_fog.sh"

uniform vec4 u_detailDoodadParams[4];
SAMPLER2D(s_detailDoodadTex, 0);

void main()
{
    vec4 textureColor = texture2D(s_detailDoodadTex, v_texcoord0);
    float alpha = textureColor.a * v_color0.a;
    if (alpha < u_detailDoodadParams[1].z) {
        discard;
    }
    vec3 litColor = textureColor.rgb * v_color0.rgb;
    float fogFactor = openwowLinearFogVisibility(
        u_detailDoodadParams[2], v_viewDist);
    gl_FragColor = vec4(
        mix(u_detailDoodadParams[3].rgb, litColor, fogFactor), alpha);
}
