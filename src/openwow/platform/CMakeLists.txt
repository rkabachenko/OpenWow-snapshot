add_subdirectory(dynamic_libraries)
add_subdirectory(filesystem)

openwow_add_component(openwow_platform_runtime
  adapters/clipboard/os_clipboard.cpp
  adapters/ime/os_ime.cpp
  adapters/sdl/platform_layer.cpp
  system/os_system_info.cpp
  window/system_mouse_speed.cpp
  window/window_manager.cpp
  diagnostics/crash_handler.cpp
  process/os_platform.cpp
  adapters/win32/win32_compat.cpp
  adapters/win32/storm_registry.cpp
  adapters/win32/win32_error_log.cpp
)
target_link_libraries(openwow_platform_runtime PRIVATE
  openwow_engine_core
  openwow_foundation
  openwow_foundation_diagnostics
  openwow_platform_filesystem
  SDL2::SDL2
)
if(APPLE)
  openwow_link_apple_framework(openwow_platform_runtime AppKit)
  openwow_link_apple_framework(openwow_platform_runtime CoreFoundation)
  openwow_link_apple_framework(openwow_platform_runtime IOKit)

  openwow_link_apple_framework(openwow_platform_runtime ApplicationServices)
endif()
