openwow_add_component(openwow_input
  input_manager.cpp
  key_state.cpp
  mouse_look.cpp
  input_control.cpp
  hid_manager.cpp
  adapters/win32/joystick.cpp
)
target_link_libraries(openwow_input PRIVATE
  openwow_foundation
  openwow_lua
  SDL2::SDL2
)
