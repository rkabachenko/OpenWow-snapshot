#include "openwow/ui/glue/interleaved_toc_processor.h"

#include "openwow/ui/xml/frame_xml_document_order.h"
#include "openwow/ui/framexml_font_registry.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/client_path_identity.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace openwow::ui::glue {

using openwow::text::ToLowerAscii;

namespace {

bool CanReadTextFile(const openwow::vfs::VirtualFileSystem& vfs,
                     const std::string& path) {
  return !path.empty() && vfs.ReadTextFile(path).has_value();
}

}

InterleavedTocProcessor::InterleavedTocProcessor(
    const openwow::vfs::VirtualFileSystem& vfs,
    GlueLuaRuntime& lua_runtime,
    GlueWidgetRuntime& widget_runtime,
    GlueBindingRegistry& binding_registry)
    : vfs_(vfs),
      lua_runtime_(lua_runtime),
      widget_runtime_(widget_runtime),
      binding_registry_(binding_registry) {}

void InterleavedTocProcessor::SetOnWidgetCreated(OnWidgetCreatedCallback callback) {
  on_widget_created_ = std::move(callback);
}

InterleavedProcessResult InterleavedTocProcessor::ProcessToc(
    const std::vector<TocEntry>& entries) {

  xml_files_processed_ = 0;
  lua_files_executed_ = 0;
  widgets_created_ = 0;
  onload_fired_ = 0;
  errors_.clear();
  processing_stack_.clear();

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "InterleavedTocProcessor: processing " + std::to_string(entries.size())
                         + " TOC entries");

  for (const auto& entry : entries) {
    ProcessFile(entry.path);
  }

  InterleavedProcessResult result;
  result.ok = errors_.empty();
  result.xml_files_processed = xml_files_processed_;
  result.lua_files_executed = lua_files_executed_;
  result.widgets_created = widgets_created_;
  result.onload_fired = onload_fired_;

  if (!errors_.empty()) {

    result.error = "Interleaved processing completed with " + std::to_string(errors_.size())
                   + " error(s): " + errors_.front();
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "InterleavedTocProcessor: done — xml=" + std::to_string(xml_files_processed_)
                         + " lua=" + std::to_string(lua_files_executed_)
                         + " widgets=" + std::to_string(widgets_created_)
                         + " onloads=" + std::to_string(onload_fired_)
                         + " errors=" + std::to_string(errors_.size()));

  return result;
}

void InterleavedTocProcessor::ProcessFile(const std::string& path) {
  if (path.empty()) {
    errors_.push_back("InterleavedTocProcessor: empty TOC path");
    return;
  }

  const auto ext = ToLowerAscii(std::filesystem::path(path).extension().string());
  if (ext == ".lua") {
    ProcessLuaFile(path);
  } else if (ext == ".xml") {
    ProcessXmlFile(path);
  } else {
    const std::string message = "InterleavedTocProcessor: unknown file type: " + path;
    errors_.push_back(message);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
  }
}

void InterleavedTocProcessor::ProcessLuaFile(const std::string& path) {
  const auto result = lua_runtime_.ExecuteFile(path);
  if (result.ok) {
    ++lua_files_executed_;
  } else {
    errors_.push_back(result.error);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "InterleavedTocProcessor: Lua execution failed: " + path
                           + " — " + result.error);
  }
}

void InterleavedTocProcessor::ProcessXmlFile(const std::string& path) {
  using Clock = std::chrono::steady_clock;
  const auto file_started = Clock::now();
  std::chrono::nanoseconds prepare_time{};
  std::chrono::nanoseconds register_time{};
  std::chrono::nanoseconds publish_time{};
  std::chrono::nanoseconds onload_time{};
  std::chrono::nanoseconds visibility_time{};

  const auto path_identity = openwow::vfs::MakeClientPathIdentity(path);
  if (!processing_stack_.insert(path_identity.lookup_path).second) {
    const std::string message = "InterleavedTocProcessor: cycle detected: " + path;
    errors_.push_back(message);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
    return;
  }

  const auto xml_text = vfs_.ReadTextFile(path);
  if (!xml_text.has_value()) {
    processing_stack_.erase(path_identity.lookup_path);
    std::string message;
    if (vfs_.Exists(path)) {
      message = "InterleavedTocProcessor: XML exists but unreadable: " + path;
    } else {
      message = "InterleavedTocProcessor: XML file not found: " + path;
    }
    errors_.push_back(message);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
    return;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "InterleavedTocProcessor: processing XML " + path + " bytes="
                         + std::to_string(xml_text->size()));

  const auto document_order = openwow::ui::xml::ExtractFrameXmlTopLevelElements(*xml_text);
  if (!document_order.ok) {
    errors_.push_back("FrameXML parse failed for " + path + ": " + document_order.error);
    processing_stack_.erase(path_identity.lookup_path);
    return;
  }

  const auto parsed_fonts = openwow::ui::ParseFontAndIntrinsicXml(*xml_text);
  if (!parsed_fonts.ok) {
    errors_.push_back("FrameXML font parse failed for " + path + ": " +
                      parsed_fonts.error);
    processing_stack_.erase(path_identity.lookup_path);
    return;
  }

  const auto process_dependency = [&](const openwow::ui::xml::FrameXmlTopLevelElement& elem) {
    switch (elem.kind) {
      case openwow::ui::xml::FrameXmlTopLevelElementKind::kInclude: {
        const auto resolved = ResolveRelativePath(path, elem.value);
        if (CanReadTextFile(vfs_, resolved)) {
          ProcessFile(resolved);
        } else {
          const std::string message = "InterleavedTocProcessor: Include not found: "
                                      + elem.value + " (resolved: " + resolved + ")";
          errors_.push_back(message);
          openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
        }
        break;
      }
      case openwow::ui::xml::FrameXmlTopLevelElementKind::kScriptFile: {
        const auto resolved = ResolveRelativePath(path, elem.value);
        if (CanReadTextFile(vfs_, resolved)) {
          ProcessLuaFile(resolved);
        } else {
          const std::string message = "InterleavedTocProcessor: Script file not found: "
                                      + elem.value + " (resolved: " + resolved + ")";
          errors_.push_back(message);
          openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
        }
        break;
      }
      case openwow::ui::xml::FrameXmlTopLevelElementKind::kScriptInline: {
        const auto result = lua_runtime_.ExecuteString(elem.value, path + ":<Script>");
        if (!result.ok) {
          errors_.push_back(result.error);
        }
        break;
      }
      case openwow::ui::xml::FrameXmlTopLevelElementKind::kFont:
      case openwow::ui::xml::FrameXmlTopLevelElementKind::kFrame:
        break;
    }
  };

  std::size_t first_unprocessed_element = 0;
  while (first_unprocessed_element < document_order.elements.size()) {
    const auto& elem = document_order.elements[first_unprocessed_element];
    if (elem.kind == openwow::ui::xml::FrameXmlTopLevelElementKind::kFont ||
        elem.kind == openwow::ui::xml::FrameXmlTopLevelElementKind::kFrame) {
      break;
    }
    process_dependency(elem);
    ++first_unprocessed_element;
  }

  const auto prepare_started = Clock::now();
  auto prepared = widget_runtime_.PrepareXml(vfs_, *xml_text);
  prepare_time = Clock::now() - prepare_started;
  if (!prepared.ok && !prepared.error.empty()) {
    errors_.push_back("FrameXML preparation failed for " + path + ": " + prepared.error);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "InterleavedTocProcessor: PrepareXml failed for " + path
                           + ": " + prepared.error);
  }

  binding_registry_.LoadBindingsFromXmlText(*xml_text);

  std::size_t group_index = 0;
  std::size_t font_index = 0;

  for (std::size_t element_index = first_unprocessed_element;
       element_index < document_order.elements.size(); ++element_index) {
    const auto& elem = document_order.elements[element_index];
    switch (elem.kind) {
      case openwow::ui::xml::FrameXmlTopLevelElementKind::kInclude:
      case openwow::ui::xml::FrameXmlTopLevelElementKind::kScriptFile:
      case openwow::ui::xml::FrameXmlTopLevelElementKind::kScriptInline:
        process_dependency(elem);
        break;
      case openwow::ui::xml::FrameXmlTopLevelElementKind::kFont: {

        if (!elem.value.empty()) {
          if (font_index < parsed_fonts.fonts.size()) {
            const auto font_result =
                lua_runtime_.RegisterXmlFont(parsed_fonts.fonts[font_index]);
            if (!font_result.ok) {
              openwow::diagnostics::Log(
                  openwow::diagnostics::LogLevel::kWarn,
                  "InterleavedTocProcessor: " + font_result.error +
                      " (source=" + path + ")");
            }
          }
          ++font_index;
        }

        break;
      }
      case openwow::ui::xml::FrameXmlTopLevelElementKind::kFrame: {

        while (group_index < prepared.groups.size()
               && prepared.groups[group_index].is_virtual) {
          ++group_index;
        }

        if (group_index < prepared.groups.size()) {
          auto& group = prepared.groups[group_index];
          const auto register_started = Clock::now();
          const auto registered_group =
              widget_runtime_.RegisterFrameGroup(std::move(group.frames), &vfs_);

          const auto& created_names = registered_group.registered;
          const auto& newly_created_names = registered_group.newly_created;
          const auto group_register_time = Clock::now() - register_started;
          register_time += group_register_time;
          if (group_register_time >= std::chrono::milliseconds(20)) {
            openwow::diagnostics::Log(
                openwow::diagnostics::LogLevel::kInfo,
                "Glue group register timing: root=" + group.top_level_name +
                    " widgets=" + std::to_string(created_names.size()) +
                    " elapsed_ms=" + std::to_string(
                        std::chrono::duration_cast<std::chrono::milliseconds>(group_register_time)
                            .count()));
          }
          widgets_created_ += static_cast<std::uint32_t>(newly_created_names.size());

          const auto publish_started = Clock::now();
          lua_runtime_.PublishNewWidgets(created_names);

          for (const auto& name : created_names) {
            if (lua_runtime_.HasWidgetScript(name, "OnMouseWheel")) {
              widget_runtime_.SetMouseWheelEnabled(name, true);
            }
          }
          publish_time += Clock::now() - publish_started;

          if (on_widget_created_) {
            const auto onload_started = Clock::now();
            for (const auto& name : newly_created_names) {
              const auto widget_onload_started = Clock::now();
              on_widget_created_(name, name + ".OnLoad");
              const auto widget_onload_time = Clock::now() - widget_onload_started;
              if (widget_onload_time >= std::chrono::milliseconds(20)) {
                openwow::diagnostics::Log(
                    openwow::diagnostics::LogLevel::kInfo,
                    "Glue OnLoad timing: widget=" + name + " elapsed_ms=" +
                        std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           widget_onload_time)
                                           .count()));
              }
              ++onload_fired_;
            }
            onload_time += Clock::now() - onload_started;
          }

          const auto visibility_started = Clock::now();
          lua_runtime_.InitializeVisibilityForNewWidgets(newly_created_names);
          visibility_time += Clock::now() - visibility_started;
          ++group_index;
        }
        break;
      }
    }
  }

  ++xml_files_processed_;
  processing_stack_.erase(path_identity.lookup_path);
  const auto elapsed = Clock::now() - file_started;
  if (elapsed >= std::chrono::milliseconds(50)) {
    const auto milliseconds = [](const auto duration) {
      return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    };
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kInfo,
        "Glue XML timing: path=" + path + " total_ms=" + std::to_string(milliseconds(elapsed)) +
            " prepare_ms=" + std::to_string(milliseconds(prepare_time)) +
            " register_ms=" + std::to_string(milliseconds(register_time)) +
            " publish_ms=" + std::to_string(milliseconds(publish_time)) +
            " onload_ms=" + std::to_string(milliseconds(onload_time)) +
            " visibility_ms=" + std::to_string(milliseconds(visibility_time)));
  }
}

std::string InterleavedTocProcessor::ResolveRelativePath(
    const std::string& base_xml_path,
    const std::string& relative_ref) const {
  const auto base_identity =
      openwow::vfs::MakeClientPathIdentity(base_xml_path);
  const std::string display_directory = base_identity.empty()
      ? std::string("/Interface/GlueXML")
      : std::filesystem::path(base_identity.display_path)
            .parent_path()
            .generic_string();
  return openwow::vfs::ResolveClientPathIdentity(relative_ref,
                                                  display_directory)
      .display_path;
}

}
