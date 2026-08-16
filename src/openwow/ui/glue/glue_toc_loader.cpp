#include "openwow/ui/glue/glue_toc_loader.h"

#include "openwow/ui/framexml/framexml_name_utils.h"
#include "openwow/ui/toc_parser.h"
#include "openwow/foundation/text/ascii.h"

#include <filesystem>

namespace openwow::ui::glue {

using openwow::text::ToLowerAscii;
namespace {

std::string JoinGluePath(const std::string& filename) {
  return openwow::ui::framexml::ResolveVirtualPath(filename, "/Interface/GlueXML");
}

}

TocEntryList LoadGlueTocEntries(const openwow::vfs::VirtualFileSystem& vfs,
                                const std::string& toc_virtual_path) {
  TocEntryList out;

  const auto toc = vfs.ReadTextFile(toc_virtual_path);
  if (!toc.has_value()) {
    out.ok = false;
    out.error = "missing Glue TOC: " + toc_virtual_path;
    return out;
  }

  const auto lines = openwow::ui::TOCParser::ExtractVisibleFileEntries(*toc, true);
  out.entries.reserve(lines.size());

  for (const auto& line : lines) {
    const auto path = JoinGluePath(line);
    if (path.empty()) continue;
    if (!vfs.Exists(path)) {
      out.ok = false;
      out.error = "Glue TOC entry not found: " + path;
      return out;
    }

    const auto ext = ToLowerAscii(std::filesystem::path(path).extension().string());
    if (ext == ".xml") {
      out.entries.push_back(TocEntry{.kind = TocEntryKind::kXml, .path = path});
    } else if (ext == ".lua") {
      out.entries.push_back(TocEntry{.kind = TocEntryKind::kLua, .path = path});
    } else {
      out.ok = false;
      out.error = "Unsupported Glue TOC entry type: " + path;
      return out;
    }
  }

  out.ok = !out.entries.empty();
  if (!out.ok) {
    out.error = "Glue TOC did not yield any entries: " + toc_virtual_path;
  }
  return out;
}

}
