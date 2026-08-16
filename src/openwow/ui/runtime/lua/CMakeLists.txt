add_library(openwow_ui_lua_runtime
  frame_script_runtime.cpp
  lua_binding.cpp
  lua_call.cpp
  lua_composition.cpp
  lua_reference.cpp
  lua_vm.cpp
)

target_include_directories(openwow_ui_lua_runtime
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${PROJECT_SOURCE_DIR}/src
)

target_compile_features(openwow_ui_lua_runtime PUBLIC cxx_std_20)
target_link_libraries(openwow_ui_lua_runtime PUBLIC openwow_lua)
