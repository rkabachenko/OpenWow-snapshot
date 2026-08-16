$input a_position, a_normal, a_texcoord0, a_texcoord1, a_color0, a_indices, a_weight
$output v_texcoord0, v_texcoord1, v_color0, v_viewDist

#include <bgfx_shader.sh>
#include "world_fog.sh"

#define OPENWOW_M2_VS_LIGHTING 1
#define OPENWOW_M2_VS_TEXGEN_ENV 1

#include "m2_vertex_perdraw.sh"
