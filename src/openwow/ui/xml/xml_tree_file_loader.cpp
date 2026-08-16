#include "openwow/ui/xml/xml_tree_file_loader.h"

#include "openwow/core/md5.h"
#include "openwow/ui/xml/xml_tree.h"
#include "openwow/vfs/virtual_file_system.h"

namespace openwow::ui::xml {

XmlTreeFileLoadResult LoadXmlTreeFile(
    const openwow::vfs::VirtualFileSystem& vfs,
    std::string_view xml_path,
    openwow::core::MD5Context* digest) {
  XmlTreeFileLoadResult result;
  const std::string path(xml_path);

  const auto xml_text = vfs.ReadTextFile(path);
  if (!xml_text.has_value()) {
    result.error_message = "Couldn't open " + path;
    return result;
  }

  if (digest != nullptr) {
    openwow::core::MD5_Update(digest, xml_text->data(), xml_text->size());
  }

  result.tree = XMLTree_Parse(xml_text->data(), xml_text->size());
  if (result.tree == nullptr) {
    result.error_message = "Couldn't parse XML in " + path;
  }

  return result;
}

}
