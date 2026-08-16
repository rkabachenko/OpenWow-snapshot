openwow_add_component(openwow_vfs_runtime
  adapters/mpq/mpq_archive.cpp
  adapters/filesystem/native_filesystem.cpp
  retail/archive_registry.cpp
  retail/retail_path_resolver.cpp
  retail/file_stack/file_stack_abi.cpp
  retail/file_stack/file_stack_provider.cpp
  retail/io_unit/checksum_unit.cpp
  retail/io_unit/io_unit_compat.cpp
  retail/io_unit/write_planner.cpp
  retail/runtime_file.cpp
  retail/runtime_file_registry.cpp
  retail/sfile_archive.cpp
  retail/sfile_configuration.cpp
  retail/sfile_runtime.cpp
  retail/sfile_test_support.cpp
  retail/sound_cache/sound_cache.cpp
  retail/streaming/data_preload_controller.cpp
  retail/streaming/streaming_dispatch_lifetime.cpp
  retail/streaming/streaming_file_adapter.cpp
  retail/streaming/streaming_read_plan.cpp
  retail/streaming/streaming_source_transport.cpp
  virtual_file_system.cpp
  sfile_core.cpp
  mpq_hash.cpp
)
target_link_libraries(openwow_vfs_runtime PRIVATE

  openwow_engine_core
  openwow_foundation
  openwow_foundation_diagnostics
  openwow_platform_filesystem
  ZLIB::ZLIB
)
target_compile_definitions(openwow_vfs_runtime PRIVATE
  OPENWOW_HAS_STORMLIB=$<BOOL:${OPENWOW_HAS_STORMLIB}>
)
if(OPENWOW_HAS_STORMLIB)
  target_link_libraries(openwow_vfs_runtime PRIVATE ${OPENWOW_STORMLIB_TARGET})
endif()
if(APPLE)
  openwow_link_apple_framework(openwow_vfs_runtime CoreFoundation)
endif()
