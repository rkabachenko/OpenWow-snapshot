#pragma once

#include "openwow/ui/game/framescript/xml/frame_xml_loader.h"
#include "openwow/ui/retail_client_build.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct lua_State;

namespace openwow::game {
class BindingProfiles;
}

namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::ui::game {

inline constexpr std::string_view kRetailFrameXmlTocPath = "/Interface/FrameXML/FrameXML.toc";
inline constexpr std::string_view kRetailFrameXmlBindingsPath = "/Interface/FrameXML/Bindings.xml";
inline constexpr std::string_view kRetailFrameXmlArchiveIntegrityPath =
    "Interface\\FrameXML\\FrameXML.toc";
inline constexpr std::string_view kRetailFrameXmlLogPath = "Logs\\FrameXML.log";
inline constexpr std::uint32_t kRetailFrameXmlInterfaceVersion =
    openwow::ui::kRetailInterfaceVersion;

enum class FrameXmlSignatureStatus {
  kMissingSignature = 0,
  kCorruptSignature = 1,
  kModifiedOrCorrupt = 2,
  kValid = 3,
  kUnchecked = 4,
};

[[nodiscard]] std::string_view FrameXmlSignatureStatusMessage(FrameXmlSignatureStatus status);

struct FrameXmlIntegrityExpectation {
  std::array<std::uint8_t, 16> digest{};
};

struct FrameXmlBootstrapOptions {
  std::string toc_path{std::string(kRetailFrameXmlTocPath)};
  std::string bindings_path{std::string(kRetailFrameXmlBindingsPath)};
  std::string archive_integrity_path{
      std::string(kRetailFrameXmlArchiveIntegrityPath)};
  std::uint32_t expected_interface_version{kRetailFrameXmlInterfaceVersion};
  bool load_bindings{true};
  bool enforce_registered_signature{true};
  std::optional<FrameXmlIntegrityExpectation> expected_digest;
};

struct FrameXmlBootstrapResult {
  bool ok{false};
  std::string error;
  std::uint32_t interface_version{0};
  FrameXmlLoadResult frame_xml;
  bool bindings_present{false};
  bool bindings_loaded{false};
  bool integrity_validated{false};
  std::array<std::uint8_t, 16> actual_digest{};
};

[[nodiscard]] FrameXmlBootstrapResult LoadFrameXmlWithIntegrity(
    lua_State* state,
    const openwow::vfs::VirtualFileSystem* vfs,
    FrameXmlLoader& loader,
    openwow::game::BindingProfiles* key_bindings,
    const FrameXmlBootstrapOptions& options,
    TocLoadProgress* progress = nullptr,
    UiLoadStatusSink* status_sink = nullptr);

}
