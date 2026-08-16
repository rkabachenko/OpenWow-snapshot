#pragma once

#include <string>
#include <string_view>

namespace openwow::ui::xml {

struct XMLNode;

[[nodiscard]] bool ParseOwnedXmlDocument(std::string_view xml_text,
                                         XMLNode* out_root,
                                         std::string* error = nullptr);

}
