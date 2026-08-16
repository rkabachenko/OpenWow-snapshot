$input v_texcoord0

#include <bgfx_shader.sh>
#include "postprocess_composite.sh"

SAMPLER2D(s_ppTexColor,  0);
SAMPLER2D(s_ppTexBloom,  1);

uniform vec4 u_compositeParams;

uniform vec4 u_colorGrade;

uniform vec4 u_sourceUvScale;

void main()
{
    vec4 scene = texture2D(s_ppTexColor,
                           v_texcoord0 * u_sourceUvScale.xy);
    vec3 bloom = texture2D(s_ppTexBloom,
                           v_texcoord0 * u_sourceUvScale.zw).rgb;

    vec4 composite = openwowCompositePassGlow(scene, bloom, u_compositeParams);
    vec3 result = composite.rgb;

    float gradeStrength = u_compositeParams.w;
    if (gradeStrength > 0.001)
    {
        result = mix(result, result * u_colorGrade.rgb, gradeStrength);
    }

    gl_FragColor = vec4(clamp(result, 0.0, 1.0),
                        clamp(composite.a, 0.0, 1.0));
}
