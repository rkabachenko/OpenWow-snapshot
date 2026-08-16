openwow_add_component(openwow_media_runtime
  playback/movie_player.cpp
  subtitles/subtitle_parser.cpp
  adapters/ffmpeg/movie_decoder.cpp
)
target_link_libraries(openwow_media_runtime PRIVATE
  openwow_foundation
  openwow_foundation_diagnostics
  openwow_vfs_runtime
  openwow_ffmpeg
)
