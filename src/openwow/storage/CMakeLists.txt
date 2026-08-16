add_subdirectory(persistence)

openwow_add_component(openwow_storage_runtime
  persistence/adapters/vfs/retail_profile_vfs_adapter.cpp
  persistence/adapters/storm/retail_profile_runtime.cpp
)
target_link_libraries(openwow_storage_runtime PRIVATE
  openwow_engine_core
  openwow_storage_persistence
  openwow_vfs_runtime
)
