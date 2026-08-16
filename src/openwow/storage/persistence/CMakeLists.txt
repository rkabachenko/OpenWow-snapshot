add_library(openwow_storage_profile_format
  codecs/retail_profile_document.cpp
  codecs/retail_profile_value.cpp
)

target_include_directories(openwow_storage_profile_format
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(openwow_storage_profile_format PRIVATE
  openwow_foundation_text
)
openwow_configure_target(openwow_storage_profile_format)

add_library(openwow_storage_persistence
  adapters/filesystem/profile_import.cpp
  adapters/filesystem/profile_paths.cpp
  adapters/filesystem/profile_reader.cpp
)

target_include_directories(openwow_storage_persistence
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(openwow_storage_persistence PRIVATE
  openwow_platform_filesystem
)
target_link_libraries(openwow_storage_persistence PUBLIC
  openwow_storage_profile_format
)
openwow_configure_target(openwow_storage_persistence)
