add_subdirectory(foundation)
add_subdirectory(data/formats)
add_subdirectory(network/serialization)
add_subdirectory(network/protocol)
add_subdirectory(ui/display/settings)
add_subdirectory(ui/runtime/lua)
add_subdirectory(ui/widgets/media)

function(openwow_add_component target)
  add_library(${target} STATIC ${ARGN})
  target_include_directories(${target} PUBLIC
    ${PROJECT_SOURCE_DIR}/src
  )
  target_include_directories(${target} PRIVATE
    ${PROJECT_SOURCE_DIR}
  )

  target_include_directories(${target} SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party
  )
  openwow_configure_target(${target})
endfunction()

add_subdirectory(auth)
add_subdirectory(audio)
add_subdirectory(core)
add_subdirectory(runtime)
add_subdirectory(data)
add_subdirectory(debug)
add_subdirectory(era)
add_subdirectory(game)
add_subdirectory(game/activities/dance)
add_subdirectory(game/achievements)
add_subdirectory(game/actions)
add_subdirectory(game/calendar)
add_subdirectory(game/quests)
add_subdirectory(input)
add_subdirectory(media)
add_subdirectory(net)
add_subdirectory(platform)
add_subdirectory(render)
add_subdirectory(storage)
add_subdirectory(ui)
add_subdirectory(vfs)
add_subdirectory(world)

add_subdirectory(render/backend/bgfx)
