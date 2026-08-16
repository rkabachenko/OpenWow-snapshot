$input a_position, a_normal, a_texcoord0, a_texcoord1, a_texcoord2, a_color0
$output v_texcoord0, v_alphaUV, v_color0, v_viewDist, v_worldPos, v_layerSlice

#define OPENWOW_TERRAIN_LAYER_ARRAY 1

#include "vs_terrain_splat_body.sh"
