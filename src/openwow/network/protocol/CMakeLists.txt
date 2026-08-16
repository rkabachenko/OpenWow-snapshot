add_library(openwow_network_protocol
  wotlk/opcode_names.cpp
  wotlk/world_packet.cpp
)

target_include_directories(openwow_network_protocol
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)
openwow_configure_target(openwow_network_protocol)
