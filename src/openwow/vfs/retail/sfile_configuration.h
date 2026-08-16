#pragma once

#include <cstdint>

namespace openwow::vfs {

struct StreamingConfig {
  std::uint8_t flag1{0};
  std::uint8_t flag2{0};
  std::uint8_t flag3{0};
  std::uint8_t flag4{0};
  std::int32_t build{0};
};

StreamingConfig &GetStreamingConfig();
void SetStreamingFlags(std::uint8_t flag1, std::uint8_t flag2, std::uint8_t flag3,
                       std::uint8_t flag4, std::int32_t build);
int InitStreamingFromManifest(const char *manifest);
const char *GetStreamingStatusMessageText();

using GenericErrorDisplayCallback = int (*)(const char *, const char *, const char *, int);
GenericErrorDisplayCallback SetGenericErrorDisplayCallback(GenericErrorDisplayCallback callback);
int GenericErrorDisplay(const char *primary, const char *fallback, const char *source,
                        int exit_code);
char *Macro_LoadIconList_Callee();
int ClientInit_SetLocaleDataPath(const char *path);

}
