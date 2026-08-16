#pragma once

#include "openwow/ui/glue/glue_toc_loader.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::ui::glue {

inline constexpr std::string_view kRetailGlueXmlTocPath = "/Interface/GlueXML/GlueXML.toc";
inline constexpr std::string_view kRetailGlueXmlArchiveIntegrityPath =
    "Interface\\GlueXML\\GlueXML.toc";
inline constexpr std::string_view kRetailGlueXmlLogPath = "Logs\\GlueXML.log";

enum class GlueXmlSignatureStatus {
  kMissingSignature = 0,
  kCorruptSignature = 1,
  kModifiedOrCorrupt = 2,
  kValid = 3,
  kUnchecked = 4,
};

[[nodiscard]] std::string_view GlueXmlSignatureStatusMessage(GlueXmlSignatureStatus status);

struct GlueXmlBootstrapOptions {
  std::string toc_path{std::string(kRetailGlueXmlTocPath)};
  std::string archive_integrity_path{std::string(kRetailGlueXmlArchiveIntegrityPath)};
  std::optional<std::array<std::uint8_t, 16>> expected_digest;
  bool enforce_registered_signature{true};
};

struct GlueXmlBootstrapResult {
  bool ok{false};
  std::string error;
  TocEntryList toc_entries;
  GlueXmlSignatureStatus signature_status{GlueXmlSignatureStatus::kUnchecked};
  bool integrity_validated{false};
  std::array<std::uint8_t, 16> actual_digest{};
};

[[nodiscard]] GlueXmlBootstrapResult LoadGlueTocWithIntegrity(
    const openwow::vfs::VirtualFileSystem& vfs,
    const GlueXmlBootstrapOptions& options = {});

}
