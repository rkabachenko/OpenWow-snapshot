#pragma once

#include <cstdint>

namespace openwow::ui::xml {

int streqci(const char *a, const char *b);

char *normalizePublicId(char *publicId);

char *normalizeLines(char *str);

uint32_t hashString(const uint8_t *str);

}
