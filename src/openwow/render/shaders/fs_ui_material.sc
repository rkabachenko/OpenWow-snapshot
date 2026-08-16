$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_uiTex, 0);
SAMPLER2D(s_uiMask, 1);
uniform vec4 u_uiMaterial;

void main()
{
    vec2 sourceUv = v_texcoord0;
    if (u_uiMaterial.w > 0.5) {
        sourceUv.y = 1.0 - sourceUv.y;
    }
    vec4 tex = texture2D(s_uiTex, sourceUv);
    if (u_uiMaterial.y > 0.5) {
        float luminance = dot(tex.rgb, vec3(0.299, 0.587, 0.114));
        tex.rgb = vec3_splat(luminance);
    }
    if (u_uiMaterial.z > 0.5) {
        tex.a *= texture2D(s_uiMask, v_texcoord0).a;
    }
    vec4 result = tex * v_color0;
    if (u_uiMaterial.x > 0.0 && result.a < u_uiMaterial.x) {
        discard;
    }
    gl_FragColor = result;
}
