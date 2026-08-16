#pragma once

#include <string>
#include <string_view>

namespace openwow::core {
struct MD5Context;
}

namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::ui::xml {

struct CXMLTree;

struct XmlTreeFileLoadResult {
  CXMLTree* tree = nullptr;
  std::string error_message;

  [[nodiscard]] bool ok() const {
    return tree != nullptr;
  }
};

[[nodiscard]] XmlTreeFileLoadResult LoadXmlTreeFile(
    const openwow::vfs::VirtualFileSystem& vfs,
    std::string_view xml_path,
    openwow::core::MD5Context* digest = nullptr);

}
