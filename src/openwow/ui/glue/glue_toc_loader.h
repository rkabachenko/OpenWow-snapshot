#pragma once

#include "openwow/vfs/virtual_file_system.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::ui::glue {

enum class TocEntryKind : std::uint8_t {
  kXml = 0,
  kLua = 1,
};

struct TocEntry {
  TocEntryKind kind{TocEntryKind::kXml};

  std::string path;
};

struct TocEntryList {
  std::vector<TocEntry> entries;
  bool ok{false};
  std::string error;
};

TocEntryList LoadGlueTocEntries(const openwow::vfs::VirtualFileSystem& vfs,
                                const std::string& toc_virtual_path = "/Interface/GlueXML/GlueXML.toc");

}
