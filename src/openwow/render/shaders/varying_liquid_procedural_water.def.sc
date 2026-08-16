vec4 v_procUv0      : TEXCOORD0 = vec4(0.0, 0.0, 0.0, 0.0);
vec4 v_procUv1      : TEXCOORD1 = vec4(0.0, 0.0, 0.0, 0.0);
vec4 v_procUv2      : TEXCOORD2 = vec4(0.0, 0.0, 0.0, 0.0);
vec3 v_worldPosition : TEXCOORD3 = vec3(0.0, 0.0, 0.0);
vec3 v_tangent      : TEXCOORD4 = vec3(1.0, 0.0, 0.0);
vec3 v_bitangent    : TEXCOORD5 = vec3(0.0, 0.0, 1.0);
vec3 v_surfaceNormal : TEXCOORD6 = vec3(0.0, 1.0, 0.0);
float v_viewDist    : TEXCOORD7 = 0.0;

vec3 a_position  : POSITION;
vec3 a_normal    : NORMAL;
vec2 a_texcoord0 : TEXCOORD0;
vec2 a_texcoord1 : TEXCOORD1;

vec3 a_position  : POSITION;
vec3 a_normal    : NORMAL;
vec4 a_color0    : COLOR0;
vec2 a_texcoord0 : TEXCOORD0;
vec2 a_texcoord1 : TEXCOORD1;
