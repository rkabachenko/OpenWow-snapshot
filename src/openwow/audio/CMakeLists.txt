openwow_add_component(openwow_audio
  playback/sfx_pool.cpp
  playback/sound_engine.cpp
  playback/sound_engine_capture.cpp
  playback/sound_engine_dsp.cpp
  playback/sound_engine_mixing.cpp
  playback/sound_asset_repository.cpp
  playback/sound_channel_pool.cpp
  playback/sound_chaos_runtime.cpp
  playback/sound_dsp_runtime.cpp
  playback/sound_interface.cpp
  playback/sound_lifecycle.cpp
  playback/sound_playback_runtime.cpp
  playback/sound_settings.cpp
  playback/sound_tables_and_unit_audio.cpp
  playback/spatial_audio_runtime.cpp
  playback/music_runtime.cpp

  resources/sound_data.cpp
  resources/sound_entry_resolver.cpp

  codecs/audio_decoder.cpp
  codecs/adpcm/ima_adpcm_decoder.cpp
  codecs/adpcm/vag_adpcm_decoder.cpp
  codecs/ogg/ogg_logical_streams.cpp
  codecs/ogg/vorbis_header_parity.cpp
  codecs/wav/wav_decoder.cpp

  dsp/dsp_mixing_matrix.cpp
  dsp/freeverb_engine.cpp

  spatial/audio_environment.cpp
  spatial/spatial_audio.cpp
  voice/voice_chat_audio_setup.cpp
  voice/voice_chat_loopback.cpp
  lifecycle/audio_system_shutdown.cpp

  adapters/lua/sound_voicechat_lua_registration.cpp
  adapters/ffmpeg/audio_decoder.cpp
  adapters/minimp3/mp3_decoder.cpp
  adapters/sdl/audio_engine.cpp
  adapters/sdl/audio_engine_assets.cpp
  adapters/sdl/audio_engine_channels.cpp
  adapters/sdl/audio_engine_streaming.cpp
  adapters/stb/ogg_decoder.cpp
  adapters/stb/ogg_decompress.cpp

)
target_link_libraries(openwow_audio PRIVATE
  openwow_engine_core
  openwow_data_runtime
  openwow_foundation
  openwow_foundation_diagnostics
  openwow_lua
  openwow_vfs_runtime
  SDL2::SDL2
  openwow_ffmpeg
)
