openwow_add_component(openwow_game_calendar_runtime
  adapters/lua/calendar_lua_api.cpp
  adapters/protocol/calendar_handler.cpp
  application/calendar_system.cpp
  composition/calendar_lua_registration.cpp
  rules/calendar_dynamic_holiday_hash.cpp
)
target_link_libraries(openwow_game_calendar_runtime PRIVATE

  bgfx::bgfx
  openwow_data_formats
  openwow_foundation
  openwow_game
  openwow_network_protocol
  openwow_ui_lua_runtime
)
