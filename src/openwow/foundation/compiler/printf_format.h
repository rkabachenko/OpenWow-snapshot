#pragma once

#if defined(__GNUC__) || defined(__clang__)
#define OPENWOW_PRINTF_FORMAT(fmt_index, first_arg) \
  __attribute__((format(printf, fmt_index, first_arg)))
#else

#define OPENWOW_PRINTF_FORMAT(fmt_index, first_arg)
#endif
