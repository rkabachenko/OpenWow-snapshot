find_package(boost_asio CONFIG REQUIRED)
find_package(boost_system CONFIG REQUIRED)
find_package(boost_json CONFIG REQUIRED)
find_package(Threads REQUIRED)

openwow_add_component(openwow_debug_runtime
  control/debug_control_server.cpp
  control/debug_control_json_codec.cpp
  diagnostics/debug_console.cpp
  diagnostics/error_handler.cpp
  capture/frame_replay_archive.cpp
  inspection/inspector_ui_models.cpp
  diagnostics/profiler.cpp
  capture/render_capture_inspector.cpp
  inspection/render_debug.cpp
  inspection/shader_editor_model.cpp
  inspection/source_definition_search.cpp
)
target_link_libraries(openwow_debug_runtime PRIVATE
  openwow_foundation
  openwow_foundation_diagnostics
  openwow_render_api
  Boost::asio
  Boost::system
  Boost::json
  OpenSSL::Crypto
  Threads::Threads
  ZLIB::ZLIB
)
