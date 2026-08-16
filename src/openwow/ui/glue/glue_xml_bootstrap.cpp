#include "openwow/ui/glue/glue_xml_bootstrap.h"

#include "openwow/core/client_init.h"
#include "openwow/core/md5.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/vfs/virtual_file_system.h"

namespace openwow::ui::glue {

std::string_view GlueXmlSignatureStatusMessage(const GlueXmlSignatureStatus status) {
  switch (status) {
    case GlueXmlSignatureStatus::kMissingSignature:
      return "GlueXML missing signature";
    case GlueXmlSignatureStatus::kCorruptSignature:
      return "GlueXML has corrupt signature";
    case GlueXmlSignatureStatus::kModifiedOrCorrupt:
      return "GlueXML is modified or corrupt";
    case GlueXmlSignatureStatus::kValid:
    case GlueXmlSignatureStatus::kUnchecked:
      return {};
  }
  return {};
}

GlueXmlBootstrapResult LoadGlueTocWithIntegrity(
    const openwow::vfs::VirtualFileSystem& vfs,
    const GlueXmlBootstrapOptions& options) {
  GlueXmlBootstrapResult result;

  const auto toc_bytes = vfs.ReadFileBytes(options.toc_path);
  if (!toc_bytes.has_value()) {
    result.error = "missing Glue TOC: " + options.toc_path;
    return result;
  }

  std::optional<std::array<std::uint8_t, 16>> expected_digest = options.expected_digest;
  if (!expected_digest.has_value() && options.enforce_registered_signature &&
      !options.archive_integrity_path.empty()) {
    expected_digest =
        openwow::core::FindArchiveIntegrityDigestForFile(options.archive_integrity_path.c_str());
  }

  if (expected_digest.has_value()) {
    result.actual_digest =
        openwow::core::MD5_Digest(toc_bytes->data(), toc_bytes->size());
    if (result.actual_digest != *expected_digest) {
      result.signature_status = GlueXmlSignatureStatus::kModifiedOrCorrupt;
      result.error = std::string(GlueXmlSignatureStatusMessage(result.signature_status));
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, result.error);
      return result;
    }

    result.signature_status = GlueXmlSignatureStatus::kValid;
    result.integrity_validated = true;
  }

  result.toc_entries = LoadGlueTocEntries(vfs, options.toc_path);
  if (!result.toc_entries.ok) {
    result.error = result.toc_entries.error;
    return result;
  }

  result.ok = true;
  return result;
}

}
