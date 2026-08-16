#include "openwow/ui/game/framescript/xml/frame_xml_bootstrap.h"

#include "openwow/core/md5.h"
#include "openwow/core/client_init.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/vfs/virtual_file_system.h"

namespace openwow::ui::game {

std::string_view FrameXmlSignatureStatusMessage(const FrameXmlSignatureStatus status) {
  switch (status) {
    case FrameXmlSignatureStatus::kMissingSignature:
      return "FrameXML missing signature";
    case FrameXmlSignatureStatus::kCorruptSignature:
      return "FrameXML has corrupt signature";
    case FrameXmlSignatureStatus::kModifiedOrCorrupt:
      return "FrameXML is modified or corrupt";
    case FrameXmlSignatureStatus::kValid:
    case FrameXmlSignatureStatus::kUnchecked:
      return {};
  }
  return {};
}

FrameXmlBootstrapResult LoadFrameXmlWithIntegrity(
    lua_State* state,
    const openwow::vfs::VirtualFileSystem* vfs,
    FrameXmlLoader& loader,
    openwow::game::BindingProfiles* key_bindings,
    const FrameXmlBootstrapOptions& options,
    TocLoadProgress* progress,
    UiLoadStatusSink* status_sink) {
  FrameXmlBootstrapResult result;
  if (state == nullptr) {
    result.error = "Lua state not set";
    return result;
  }
  if (vfs == nullptr) {
    result.error = "VFS not set";
    return result;
  }

  result.interface_version = loader.ReadTocInterfaceVersion(options.toc_path);
  if (options.expected_interface_version != 0 && result.interface_version != 0 &&
      result.interface_version != options.expected_interface_version) {
    result.error = "FrameXML TOC interface mismatch: expected " +
                   std::to_string(options.expected_interface_version) + ", got " +
                   std::to_string(result.interface_version);
    return result;
  }

  std::optional<FrameXmlIntegrityExpectation> expected = options.expected_digest;
  if (!expected.has_value() && options.enforce_registered_signature &&
      !options.archive_integrity_path.empty()) {
    if (const auto registered = openwow::core::FindArchiveIntegrityDigestForFile(
            options.archive_integrity_path.c_str());
        registered.has_value()) {
      expected = FrameXmlIntegrityExpectation{*registered};
    }
  }

  result.bindings_present = options.load_bindings && !options.bindings_path.empty() &&
                            loader.HasCachedFile(options.bindings_path);
  if (expected.has_value()) {
    const std::string_view trailing = result.bindings_present
                                          ? std::string_view(options.bindings_path)
                                          : std::string_view{};
    if (!loader.ComputeTocDigest(options.toc_path, &result.actual_digest,
                                 &result.error, trailing)) {
      return result;
    }
    result.integrity_validated = result.actual_digest == expected->digest;
    if (!result.integrity_validated) {
      result.error = std::string(FrameXmlSignatureStatusMessage(
          FrameXmlSignatureStatus::kModifiedOrCorrupt));
      if (status_sink != nullptr) status_sink->AppendStatus(1, result.error);
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                result.error);
      return result;
    }
  }

  openwow::core::MD5Context digest{};
  openwow::core::MD5_Init(&digest);

  result.frame_xml = loader.LoadToc(
      state, options.toc_path, progress, expected.has_value() ? nullptr : &digest);
  if (!result.frame_xml.ok) {
    result.error = result.frame_xml.error;
    return result;
  }

  if (result.bindings_present && key_bindings != nullptr) {
    result.bindings_loaded =
        loader.LoadBindingXml(*key_bindings, state, options.bindings_path,
                              status_sink,
                              expected.has_value() ? nullptr : &digest);
    if (!result.bindings_loaded) {
      result.error = "FrameXML bindings failed to load: " + options.bindings_path;
      return result;
    }
  }

  if (!expected.has_value()) {
    openwow::core::MD5_Final(&digest, result.actual_digest.data());
  }

  result.ok = true;
  return result;
}

}
