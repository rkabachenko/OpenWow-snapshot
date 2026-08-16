#pragma once

#include <cctype>
#include <cstddef>

namespace openwow::core {

inline char NormalizeStormPathSeparatorChar(const char value) {
  return value == '\\' ? '/' : value;
}

inline char ChooseStormPathSeparator(const char* path) {
  if (!path) {
    return '/';
  }

  for (const char* cursor = path; *cursor != '\0'; ++cursor) {
    if (*cursor == '/' || *cursor == '\\') {
      return *cursor;
    }
  }

  return '/';
}

inline int CopyStormPath(char* destination, const char* source, int capacity) {
  if (!destination || capacity <= 0) {
    return 0;
  }

  if (!source) {
    destination[0] = '\0';
    return 0;
  }

  const int limit = capacity - 1;
  int count = 0;
  while (count < limit && source[count] != '\0') {
    destination[count] = source[count];
    ++count;
  }
  destination[count] = '\0';
  return count;
}

inline int AppendStormPath(char* destination, const char* source, int capacity) {
  if (!destination || capacity <= 0) {
    return 0;
  }

  char* cursor = destination;
  char* const last_writable = destination + capacity - 1;
  if (cursor <= last_writable) {
    while (*cursor != '\0') {
      ++cursor;
      if (cursor > last_writable) {
        return static_cast<int>(cursor - destination);
      }
    }

    if (source) {
      while (cursor < last_writable) {
        const char value = *source++;
        if (value == '\0') {
          break;
        }

        *cursor++ = value;
      }
      *cursor = '\0';
    }
  }

  return static_cast<int>(cursor - destination);
}

inline const char* FindLastStormPathSeparatorBounded(const char* path,
                                                     std::size_t max_length) {
  if (!path) {
    return nullptr;
  }

  const char* last_separator = nullptr;
  for (std::size_t index = 0; index < max_length; ++index) {
    const char value = path[index];
    if (value == '\0') {
      break;
    }

    if (value == '/' || value == '\\') {
      last_separator = path + index;
    }
  }

  return last_separator;
}

inline const char* FindStormPathLeafName(const char* path) {
  static constexpr char kEmptyPath[] = "";
  if (!path) {
    return kEmptyPath;
  }

  std::size_t length = 0;
  while (path[length] != '\0') {
    ++length;
  }

  const char* const last_separator =
      FindLastStormPathSeparatorBounded(path, length);
  return last_separator ? last_separator + 1 : path;
}

inline int GetStormRootPathLength(const char* path) {
  if (!path || path[0] == '\0') {
    return 0;
  }

  if (NormalizeStormPathSeparatorChar(path[1]) == ':') {
    return NormalizeStormPathSeparatorChar(path[2]) == '/' ? 3 : 2;
  }

  if (NormalizeStormPathSeparatorChar(path[0]) != '/'
      || NormalizeStormPathSeparatorChar(path[1]) != '/') {
    return 0;
  }

  int slash_count = 0;
  int length = 2;
  for (; path[length] != '\0'; ++length) {
    if (NormalizeStormPathSeparatorChar(path[length]) != '/') {
      continue;
    }

    if (++slash_count >= 2) {
      return length + 1;
    }
  }

  return length + 1;
}

inline int GetStormLogFileCreateRootPathLength(const char* path) {
  if (!path || path[0] == '\0') {
    return 0;
  }

  if (path[0] == '/') {
    return 1;
  }

  int length = 0;
  while (path[length] != '\0') {
    ++length;
  }

  if (length < 2) {
    return 0;
  }

  if (path[1] == ':') {
    return path[2] == '\\' ? 3 : 2;
  }

  if (path[0] != '\\' || path[1] != '\\') {
    return 0;
  }

  const char* cursor = path + 2;
  for (int separator_count = 0; separator_count < 2; ++separator_count) {
    while (*cursor != '\0' && *cursor != '\\') {
      ++cursor;
    }

    if (*cursor == '\0') {
      return length;
    }

    ++cursor;
  }

  return static_cast<int>(cursor - path);
}

inline bool CanResolveStormFilesystemPath(const char* path) {
  const char first = path[0];
  if ((first == '\\' || first == '/') && (path[1] == '\\' || path[1] == '/')) {
    return path[2] != '\0';
  }

  return std::isalpha(static_cast<unsigned char>(first)) != 0 && path[1] == ':' &&
      (path[2] == '\\' || path[2] == '/') && path[3] != '\0';
}

inline void EnsureTrailingStormPathSeparator(char* path, int capacity,
                                             char separator = '\0') {
  if (!path) {
    return;
  }

  char trailing_separator = separator;
  if (trailing_separator == '\0') {
    trailing_separator = ChooseStormPathSeparator(path);
  }

  if (capacity < 2) {
    return;
  }

  int length = 0;
  while (path[length] != '\0') {
    ++length;
  }

  const char tail = length > 0 ? path[length - 1] : '\0';
  if (length != 0 && tail == trailing_separator) {
    return;
  }

  if (tail == '/' || tail == '\\') {
    --length;
  }
  if (length > capacity - 2) {
    length = capacity - 2;
  }

  path[length] = trailing_separator;
  path[length + 1] = '\0';
}

namespace detail {

inline void NormalizeStormPathSeparators(const char* source,
                                         char* destination,
                                         int capacity,
                                         char from_separator,
                                         char to_separator) {
  if (!destination || capacity <= 0) {
    return;
  }

  if (!source) {
    destination[0] = '\0';
    return;
  }

  int remaining = capacity;
  char* out = destination;
  while (true) {
    char value = *source++;
    --remaining;
    if (value == '\0') {
      break;
    }

    *out++ = (value == from_separator) ? to_separator : value;
    if (remaining == 0) {
      out[-1] = '\0';
      return;
    }
  }

  *out = '\0';
}

}

inline void NormalizePathToForwardSlashes(const char* source,
                                          char* destination,
                                          int capacity) {
  detail::NormalizeStormPathSeparators(
      source, destination, capacity, '\\', '/');
}

inline void NormalizePathToBackslashes(const char* source,
                                       char* destination,
                                       int capacity) {
  detail::NormalizeStormPathSeparators(
      source, destination, capacity, '/', '\\');
}

inline char* NormalizePathToFirstSeparatorStyle(const char* source,
                                                char* destination,
                                                int capacity) {
  if (!destination || capacity <= 0) {
    return nullptr;
  }

  if (!source || capacity == 1) {
    destination[0] = '\0';
    return destination;
  }

  char* out = destination;
  int remaining = capacity;
  for (;;) {
    const char value = *source++;
    --remaining;
    if (value == '\0') {
      *out = '\0';
      return destination;
    }

    if (value == '/') {
      *out = '/';
      NormalizePathToForwardSlashes(source, out + 1, remaining);
      return destination;
    }
    if (value == '\\') {
      *out = '\\';
      NormalizePathToBackslashes(source, out + 1, remaining);
      return destination;
    }

    *out++ = value;
    if (remaining == 0) {
      out[-1] = '\0';
      return destination;
    }
  }
}

inline void JoinStormPathBounded(char* destination, int capacity,
                                 const char* base, const char* suffix) {
  if (!destination) {
    return;
  }

  CopyStormPath(destination, base, capacity);
  if (destination[0] != '\0') {
    EnsureTrailingStormPathSeparator(destination, capacity);
  }

  const char* normalized_suffix = suffix;
  if (normalized_suffix) {
    while (*normalized_suffix == '/' || *normalized_suffix == '\\') {
      ++normalized_suffix;
    }
  }

  AppendStormPath(destination, normalized_suffix, capacity);
  NormalizePathToFirstSeparatorStyle(destination, destination, capacity);
}

}
