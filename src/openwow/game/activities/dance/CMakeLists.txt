add_library(openwow_game_dance_model
  model/dance_move_catalog.cpp
)
target_include_directories(openwow_game_dance_model
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

add_library(openwow_game_dance_rules
  rules/dance_playback_rules.cpp
)
target_include_directories(openwow_game_dance_rules
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(openwow_game_dance_rules
  PUBLIC openwow_game_dance_model
)

add_library(openwow_game_dance_application
  application/dance_cache_coordinator.cpp
  application/dance_management_application.cpp
  application/dance_playback_application.cpp
  application/dance_studio.cpp
  application/known_dance_catalog.cpp
  application/unit_dance_state.cpp
)
target_include_directories(openwow_game_dance_application
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(openwow_game_dance_application
  PUBLIC openwow_game_dance_model
  PRIVATE openwow_game_dance_rules
)

add_library(openwow_game_dance_data_adapter
  adapters/data/dbc_dance_move_catalog.cpp
)
target_include_directories(openwow_game_dance_data_adapter
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(openwow_game_dance_data_adapter
  PUBLIC openwow_game_dance_model
  PRIVATE openwow_data_formats
)

add_library(openwow_game_dance_protocol_adapter
  adapters/protocol/dance_protocol.cpp
)
target_include_directories(openwow_game_dance_protocol_adapter
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(openwow_game_dance_protocol_adapter
  PUBLIC openwow_game_dance_model
  PRIVATE openwow_network_serialization
)
