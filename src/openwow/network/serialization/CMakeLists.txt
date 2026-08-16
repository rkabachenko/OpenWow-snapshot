add_library(openwow_network_serialization
  byte_buffer.cpp
  zlib_compression.cpp
)

target_include_directories(openwow_network_serialization
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(openwow_network_serialization PRIVATE
  ZLIB::ZLIB
)
openwow_configure_target(openwow_network_serialization)
