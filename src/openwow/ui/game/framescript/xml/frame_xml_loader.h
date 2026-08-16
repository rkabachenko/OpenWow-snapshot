#pragma once

#include "openwow/ui/game/ui_load_status.h"
#include "openwow/ui/framexml_font_registry.h"
#include "openwow/ui/framexml/framexml_parser.h"
#include "openwow/vfs/virtual_file_system.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct lua_State;

namespace openwow::core {
struct MD5Context;
}
namespace openwow::game {
class BindingProfiles;
}

namespace openwow::ui::game {

struct FrameXmlLoadResult {

  bool ok{false};
  std::string error;
  int xml_files_loaded{0};
  int lua_files_loaded{0};
  int file_failures{0};
  std::vector<std::string> loaded_files;
  std::vector<UiLoadStatusEntry> diagnostics;

  std::size_t prepared_xml_documents{0};
  std::size_t prepared_xml_cache_hits{0};
  std::size_t source_cache_hits{0};
  std::uint64_t preparation_time_us{0};
};

struct TocLoadProgress {
  int completed{0};
  int total{0};
  std::function<void(float)> callback;
};

struct TocFileList {
  bool ok{false};
  std::string error;
  std::vector<std::string> xml_paths;
  std::vector<std::string> lua_paths;
};

class FrameXmlLoader {
 public:
  FrameXmlLoader();
  ~FrameXmlLoader();

  FrameXmlLoader(const FrameXmlLoader&) = delete;
  FrameXmlLoader& operator=(const FrameXmlLoader&) = delete;

  void SetVfs(const openwow::vfs::VirtualFileSystem* vfs);

  void BeginLoadLifetime();

  using XmlProcessCallback = std::function<void(
      const std::string&, const openwow::ui::framexml::ParseResult&, std::size_t)>;
  void SetXmlProcessCallback(XmlProcessCallback callback);

  using XmlFontProcessCallback = std::function<void(
      const std::string&, const openwow::ui::FontDefinition&)>;
  void SetXmlFontProcessCallback(XmlFontProcessCallback callback);

  TocFileList ParseToc(const std::string& toc_path,
                       TocLoadProgress* progress = nullptr) const;

  [[nodiscard]] std::uint32_t ReadTocInterfaceVersion(
      const std::string& toc_path) const;
  [[nodiscard]] int ReadTocVisibleEntryCount(const std::string& toc_path) const;

  [[nodiscard]] bool ComputeTocDigest(
      const std::string& toc_path,
      std::array<std::uint8_t, 16>* digest,
      std::string* error = nullptr,
      std::string_view trailing_file_path = {});

  [[nodiscard]] bool LoadBindingXml(
      openwow::game::BindingProfiles& profiles,
      lua_State* state,
      std::string_view path,
      UiLoadStatusSink* status_sink = nullptr,
      openwow::core::MD5Context* digest = nullptr);
  [[nodiscard]] bool HasCachedFile(std::string_view path) const;

  bool LoadXml(lua_State* L, const std::string& xml_path,
               UiLoadStatusSink* status_sink = nullptr,
               openwow::core::MD5Context* digest = nullptr);

  bool RunLua(lua_State* L, const std::string& lua_path,
              UiLoadStatusSink* status_sink = nullptr,
              openwow::core::MD5Context* digest = nullptr);

  FrameXmlLoadResult LoadToc(lua_State* L, const std::string& toc_path,
                             TocLoadProgress* progress = nullptr,
                             openwow::core::MD5Context* digest = nullptr,
                             std::string_view addon_name = {});

  FrameXmlLoadResult LoadDefaultFrameXml(lua_State* L,
                                         TocLoadProgress* progress = nullptr,
                                         openwow::core::MD5Context* digest = nullptr);

 private:

  struct PreparedXmlDocument;

  struct AddonScriptContext {
    std::string_view name;
    int namespace_stack_index{0};

    [[nodiscard]] bool has_chunk_arguments() const {
      return !name.empty() && namespace_stack_index > 0;
    }
  };

  std::string NormalizeFrameXmlPath(const std::string& filename,
                                    const std::string& base_dir) const;

  std::vector<std::string> CollectScriptRefs(const std::string& xml_content,
                                             const std::string& base_dir) const;
  std::vector<std::string> CollectIncludeRefs(const std::string& xml_content,
                                              const std::string& base_dir) const;

  bool ProcessFileInterleaved(lua_State* L, const std::string& path,
                              UiLoadStatusSink* status_sink,
                              FrameXmlLoadResult* result,
                              std::unordered_set<std::string>* xml_stack,
                              openwow::core::MD5Context* digest,
                              const AddonScriptContext& addon_context);
  bool ProcessXmlInterleaved(lua_State* L, const std::string& xml_path,
                             UiLoadStatusSink* status_sink,
                             FrameXmlLoadResult* result,
                             std::unordered_set<std::string>* xml_stack,
                             openwow::core::MD5Context* digest,
                             const AddonScriptContext& addon_context);
  bool RunLuaInContext(lua_State* L, const std::string& lua_path,
                       UiLoadStatusSink* status_sink,
                       openwow::core::MD5Context* digest,
                       const AddonScriptContext& addon_context);
  bool ExecuteInlineLua(lua_State* L, const std::string& lua_source,
                        const std::string& source_name,
                        UiLoadStatusSink* status_sink);

  [[nodiscard]] std::shared_ptr<const std::string> ReadTextCached(
      const std::string& path) const;
  [[nodiscard]] std::shared_ptr<const PreparedXmlDocument>
  GetPreparedXmlDocument(const std::string& path);
  [[nodiscard]] static std::shared_ptr<const PreparedXmlDocument>
  BuildPreparedXmlDocument(std::shared_ptr<const std::string> source);
  void PrimePreparedXmlDocuments(const std::vector<std::string>& root_paths);
  bool AccumulateFileDigest(const std::string& path,
                            openwow::core::MD5Context* digest,
                            std::unordered_set<std::string>* xml_stack,
                            std::string* error);

  const openwow::vfs::VirtualFileSystem* vfs_{nullptr};
  XmlProcessCallback xml_process_callback_;
  XmlFontProcessCallback xml_font_process_callback_;

  mutable std::unordered_map<std::string, std::shared_ptr<const std::string>>
      source_cache_;
  std::unordered_map<std::string, std::shared_ptr<const PreparedXmlDocument>>
      prepared_xml_cache_;
  mutable std::size_t source_cache_hit_count_{0};
  std::size_t prepared_xml_build_count_{0};
  std::size_t prepared_xml_cache_hit_count_{0};
};

}
