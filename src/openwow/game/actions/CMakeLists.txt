add_library(openwow_game_actions_model
  model/action_assignments.cpp
)
target_include_directories(openwow_game_actions_model PUBLIC
  ${PROJECT_SOURCE_DIR}/include
)
target_compile_features(openwow_game_actions_model PUBLIC cxx_std_20)

add_library(openwow_game_actions_bindings_model INTERFACE)
target_include_directories(openwow_game_actions_bindings_model INTERFACE
  ${PROJECT_SOURCE_DIR}/include
)
target_compile_features(openwow_game_actions_bindings_model INTERFACE cxx_std_20)

add_library(openwow_game_actions_application
  application/action_command_service.cpp
)
target_include_directories(openwow_game_actions_application PUBLIC
  ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(openwow_game_actions_application PUBLIC
  openwow_game_actions_model
)
target_compile_features(openwow_game_actions_application PUBLIC cxx_std_20)

add_library(openwow_game_held_cursor
  held_cursor/held_cursor.cpp
)
target_include_directories(openwow_game_held_cursor PUBLIC
  ${PROJECT_SOURCE_DIR}/include
)
target_compile_features(openwow_game_held_cursor PUBLIC cxx_std_20)

add_library(openwow_game_actions_protocol_adapter
  adapters/protocol/wotlk_action_packets.cpp
)
target_include_directories(openwow_game_actions_protocol_adapter
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(openwow_game_actions_protocol_adapter PUBLIC
  openwow_game_actions_model
  openwow_network_protocol
)
target_compile_features(openwow_game_actions_protocol_adapter PUBLIC cxx_std_20)

function(openwow_configure_actions_object_target target)
  target_include_directories(${target}
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE
      ${PROJECT_SOURCE_DIR}/src
      ${PROJECT_SOURCE_DIR}
      ${PROJECT_SOURCE_DIR}/third_party
  )
  target_compile_features(${target} PUBLIC cxx_std_20)
  target_compile_definitions(${target}
    PRIVATE OPENWOW_HAS_STORMLIB=$<BOOL:${OPENWOW_HAS_STORMLIB}>
  )
  target_link_libraries(${target} PRIVATE
    openwow_foundation
    openwow_foundation_diagnostics
    openwow_foundation_text
    openwow_ui_lua_runtime
    SDL2::SDL2
  )
  if(OPENWOW_HAS_STORMLIB)
    target_link_libraries(${target} PRIVATE ${OPENWOW_STORMLIB_TARGET})
  endif()
endfunction()

function(openwow_configure_actions_core_object_target target)
  target_include_directories(${target}
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE
      ${PROJECT_SOURCE_DIR}/src
      ${PROJECT_SOURCE_DIR}
  )
  target_compile_features(${target} PUBLIC cxx_std_20)
  target_link_libraries(${target} PRIVATE
    openwow_foundation
    openwow_foundation_text
  )
endfunction()

add_library(openwow_game_actions_runtime OBJECT
  application/action_assignment_runtime.cpp
)
openwow_configure_actions_object_target(openwow_game_actions_runtime)
target_link_libraries(openwow_game_actions_runtime PRIVATE
  openwow_game_actions_model
  openwow_game_actions_protocol_adapter
)

add_library(openwow_game_actions_macro_icon_rules STATIC
  macros/rules/retail_macro_icon_catalog.cpp
)
openwow_configure_actions_core_object_target(
  openwow_game_actions_macro_icon_rules
)

add_library(openwow_game_actions_macros OBJECT
  macros/application/macro_condition_runtime.cpp
  macros/application/macro_presentation_runtime.cpp
  macros/application/macro_execution_runtime.cpp
  macros/application/macro_icon_library.cpp
  macros/application/macro_catalog.cpp
  macros/application/macro_catalog_action_bar.cpp
  macros/application/macro_catalog_execution.cpp
  macros/application/macro_catalog_storage.cpp
  macros/model/macro_store.cpp
  macros/rules/macro_body_rules.cpp
  macros/rules/macro_condition_rules.cpp
  macros/rules/macro_icon_resolution_rules.cpp
  macros/rules/secure_command_option_parser.cpp
)
openwow_configure_actions_core_object_target(openwow_game_actions_macros)

add_library(openwow_game_actions_macro_adapters OBJECT
  macros/adapters/lua/macro_lua_api.cpp
  macros/adapters/persistence/macro_account_data_adapter.cpp
  macros/adapters/retail/retail_macro_condition_adapter.cpp
  macros/adapters/retail/retail_macro_icon_resolution_adapter.cpp
  macros/adapters/retail/retail_macro_icon_path_adapter.cpp
  macros/adapters/storage/retail_macro_icon_loader.cpp
  macros/adapters/ui/macro_action_bar_adapter.cpp
)
openwow_configure_actions_object_target(
  openwow_game_actions_macro_adapters
)
target_link_libraries(openwow_game_actions_macro_adapters PRIVATE
  openwow_game_actions_bindings_model
  openwow_game_actions_macro_icon_rules
  openwow_game_actions_macros
)

add_library(openwow_game_actions_bindings OBJECT
  bindings/adapters/lua/binding_script_executor.cpp
  bindings/adapters/lua/binding_lua_values.cpp
  bindings/adapters/platform/sdl_joystick_axis.cpp
  bindings/adapters/platform/sdl_joystick_config.cpp
  bindings/adapters/platform/sdl_binding_key_codec.cpp
  bindings/adapters/platform/sdl_binding_input_runtime.cpp
  bindings/adapters/persistence/binding_account_data_adapter.cpp
  bindings/adapters/persistence/binding_profile_storage.cpp
  bindings/adapters/retail/modified_click_adapter.cpp
  bindings/adapters/lua/binding_command_executor.cpp
  bindings/adapters/xml/binding_xml_adapter.cpp
  bindings/application/binding_profiles.cpp
  bindings/adapters/persistence/retail_binding_text_codec.cpp
)
openwow_configure_actions_object_target(openwow_game_actions_bindings)
target_link_libraries(openwow_game_actions_bindings PRIVATE
  openwow_game_actions_bindings_model
)
