add_library(openwow_game_achievements_model INTERFACE)
target_include_directories(openwow_game_achievements_model
  INTERFACE ${PROJECT_SOURCE_DIR}/include
)

add_library(openwow_game_achievements_rules
  rules/achievement_category_resolver.cpp
)
target_include_directories(openwow_game_achievements_rules
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(openwow_game_achievements_rules
  PUBLIC openwow_game_achievements_model
  PRIVATE openwow_data_formats
)

add_library(openwow_game_achievements_application
  application/achievement_state.cpp
)
target_include_directories(openwow_game_achievements_application
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(openwow_game_achievements_application
  PUBLIC openwow_game_achievements_model
)

add_library(openwow_game_achievements_dbc_adapter
  adapters/data/dbc_achievement_metadata_catalog.cpp
)
target_include_directories(openwow_game_achievements_dbc_adapter
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(openwow_game_achievements_dbc_adapter
  PUBLIC openwow_game_achievements_application
  PRIVATE openwow_data_formats
)

add_library(openwow_game_achievements_protocol_adapter
  adapters/protocol/achievement_protocol.cpp
)
target_include_directories(openwow_game_achievements_protocol_adapter
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(openwow_game_achievements_protocol_adapter
  PUBLIC
    openwow_game_achievements_model
    openwow_network_protocol
)

openwow_add_component(openwow_game_achievements_runtime
  adapters/lua/achievement_activity_lua_api.cpp
  adapters/lua/achievement_category_lua_api.cpp
  adapters/lua/achievement_category_count_lua_api.cpp
  adapters/lua/achievement_completion_count_lua_api.cpp
  adapters/lua/achievement_comparison_session_lua_api.cpp
  adapters/lua/achievement_criteria_lua_api.cpp
  adapters/lua/achievement_link_lua_api.cpp
  adapters/lua/achievement_detail_lua_api.cpp
  adapters/lua/achievement_navigation_lua_api.cpp
  adapters/lua/achievement_points_lua_api.cpp
  adapters/lua/achievement_progress_formatter.cpp
  adapters/lua/achievement_reward_lua_api.cpp
  adapters/lua/achievement_statistic_lua_api.cpp
  adapters/lua/achievement_status_lua_api.cpp
  adapters/lua/achievement_tracking_lua_api.cpp
  application/tracked_achievement_state.cpp
)
target_link_libraries(openwow_game_achievements_runtime PRIVATE
  bgfx::bgfx
  openwow_game_achievements_application
  openwow_game_achievements_dbc_adapter
  openwow_game_achievements_protocol_adapter
  openwow_game_achievements_rules
  openwow_ui_lua_runtime
)
