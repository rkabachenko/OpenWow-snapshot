#pragma once

#include <cstddef>
#include <cstdint>

namespace openwow::ui::game::runtime {

struct UiPerformanceCounters {
  std::uint64_t frame_tree_finalize_dispatches{0};
  std::uint64_t frame_tree_finalize_stack_peak{0};
  std::uint64_t frame_tree_plans{0};
  std::uint64_t frame_tree_source_nodes{0};
  std::uint64_t frame_tree_inherited_nodes{0};
  std::uint64_t frame_tree_plan_nodes{0};
  std::uint64_t frame_tree_plan_node_peak{0};
  std::uint64_t default_frame_xml_parsed_declarations{0};
  std::uint64_t default_frame_xml_concrete_declarations{0};
  std::uint64_t default_frame_xml_tree_plans{0};
  std::uint64_t default_frame_xml_tree_source_nodes{0};
  std::uint64_t default_frame_xml_tree_inherited_nodes{0};
  std::uint64_t default_frame_xml_tree_plan_nodes{0};
  std::uint64_t default_frame_xml_tree_plan_node_peak{0};
  std::uint64_t default_frame_xml_template_roots{0};
  std::uint64_t default_frame_xml_template_nodes{0};
  std::uint64_t default_frame_xml_materialized_objects{0};
  std::uint64_t default_frame_xml_materialized_frames{0};
  std::uint64_t default_frame_xml_materialized_textures{0};
  std::uint64_t default_frame_xml_materialized_font_strings{0};
  std::uint64_t frame_bindings_created{0};
  std::uint64_t texture_bindings_created{0};
  std::uint64_t font_string_bindings_created{0};
  std::uint64_t traversal_snapshot_rebuilds{0};
  std::uint64_t hit_test_cache_rebuilds{0};
  std::uint64_t frame_xml_progress_pulses{0};
  std::uint64_t texture_validation_requests{0};
  std::uint64_t texture_validation_cache_hits{0};
  std::uint64_t texture_validation_source_reads{0};
  std::uint64_t default_ui_load_duration_ns{0};
  std::size_t traversal_entries{0};
  std::size_t hit_test_index_entries{0};
  std::size_t last_render_candidates{0};
  std::size_t last_render_lua_visibility_queries{0};
  std::uint64_t last_render_generation{0};
  std::size_t last_render_world_map_descendant_submissions{0};
  std::size_t last_render_world_map_background_submissions{0};
  std::size_t last_render_world_map_detail_tile_submissions{0};
  std::size_t last_render_character_panel_descendant_submissions{0};
  std::size_t last_render_character_panel_background_submissions{0};
  bool last_render_character_model_submitted{false};
  std::size_t last_render_player_frame_background_submissions{0};
  bool last_render_player_portrait_submitted{false};
  bool last_render_player_health_submitted{false};
  bool last_render_player_power_submitted{false};
  bool last_render_action_icon_submitted{false};
  bool last_render_chat_content_submitted{false};
  std::size_t last_hit_test_candidates{0};
  std::size_t last_hit_test_lua_queries{0};
};

}
