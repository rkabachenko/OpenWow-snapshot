openwow_add_component(openwow_game_quests_runtime
  adapters/lua/completed_quest_lua_api.cpp
)
target_link_libraries(openwow_game_quests_runtime PRIVATE
  bgfx::bgfx
  openwow_game
  openwow_ui_lua_runtime
)
