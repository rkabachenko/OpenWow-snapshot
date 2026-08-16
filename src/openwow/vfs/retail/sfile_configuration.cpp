#include "openwow/vfs/retail/sfile_configuration.h"

#include "openwow/core/storm_error.h"
#include "openwow/core/storm_path.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/data/streaming_init.h"

#include <array>

namespace openwow::vfs {
namespace {

StreamingConfig g_streaming_config;
std::array<char, 260> g_archive_data_path_buffer{};
GenericErrorDisplayCallback g_generic_error_display_callback = nullptr;

}

StreamingConfig &GetStreamingConfig() {
  return g_streaming_config;
}

void SetStreamingFlags(std::uint8_t flag1, std::uint8_t flag2, std::uint8_t flag3,
                       std::uint8_t flag4, std::int32_t build) {
  if (flag1) g_streaming_config.flag1 = 1;
  if (flag2) g_streaming_config.flag2 = 1;
  if (flag3) g_streaming_config.flag3 = 1;
  if (flag4) g_streaming_config.flag4 = 1;
  if (build) g_streaming_config.build = build;
}

int InitStreamingFromManifest(const char *manifest) {
  openwow::data::ResetCurrentStreamingStatusChain();
  return openwow::data::InitializeStreaming(manifest, nullptr, nullptr, nullptr, false) ? 1 : 0;
}

const char *GetStreamingStatusMessageText() {
  return openwow::data::GetStreamingStatusMessageCString();
}

GenericErrorDisplayCallback SetGenericErrorDisplayCallback(GenericErrorDisplayCallback callback) {
  g_generic_error_display_callback = callback;
  return callback;
}

int GenericErrorDisplay(const char *primary, const char *fallback, const char *source,
                        int exit_code) {
  const char *message = fallback && *fallback ? fallback : primary;
  return openwow::core::SErrDisplayError(0, source, exit_code, message, 0, 1u, 0x11111111);
}

char *Macro_LoadIconList_Callee() {
  const auto &startup_state = openwow::data::GetStartupFileSystemState();
  openwow::core::CopyStormPath(g_archive_data_path_buffer.data(),
                               startup_state.archive_data_path.c_str(),
                               static_cast<int>(g_archive_data_path_buffer.size()));
  return g_archive_data_path_buffer.data();
}

int ClientInit_SetLocaleDataPath(const char *path) {
  openwow::data::SetStartupLocaleDataPath(path ? path : "");
  return 1;
}

}
