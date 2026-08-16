
#include "openwow/ui/game/framescript/xml/frame_xml_loader.h"
#include "openwow/core/md5.h"
#include "openwow/game/actions/bindings/adapters/xml/binding_xml_adapter.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/ui/game/lua_addon_memory_tracker.h"
#include "openwow/ui/framexml/framexml_name_utils.h"
#include "openwow/ui/lua_base_overrides.h"
#include "openwow/ui/lua_taint_api.h"
#include "openwow/ui/toc_parser.h"
#include "openwow/ui/xml/frame_xml_document_order.h"
#include "openwow/ui/xml/frame_xml_parser.h"
#include "openwow/ui/xml/owned_xml_document_parser.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/client_path_identity.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <unordered_set>

namespace openwow::ui::game {

using openwow::text::ToLowerAscii;

namespace {

void DedupPreserveOrder(std::vector<std::string>* paths) {
  if (!paths) return;
  std::unordered_set<std::string> seen;
  seen.reserve(paths->size() * 2);
  std::vector<std::string> out;
  out.reserve(paths->size());
  for (auto& p : *paths) {
    const std::string key =
        openwow::vfs::MakeClientPathIdentity(p).lookup_path;
    if (!seen.insert(key).second) continue;
    out.push_back(std::move(p));
  }
  *paths = std::move(out);
}

void AppendStatus(UiLoadStatusSink* sink,
                  std::intptr_t code,
                  const std::string& message) {
  if (sink == nullptr || message.empty()) {
    return;
  }
  sink->AppendStatus(code, message);
}

class LuaStackGuard {
 public:
  explicit LuaStackGuard(lua_State* state)
      : state_(state), top_(state != nullptr ? lua_gettop(state) : 0) {}

  ~LuaStackGuard() {
    if (state_ != nullptr) {
      lua_settop(state_, top_);
    }
  }

  LuaStackGuard(const LuaStackGuard&) = delete;
  LuaStackGuard& operator=(const LuaStackGuard&) = delete;

 private:
  lua_State* state_{nullptr};
  int top_{0};
};

class ScopedTrustedFrameXmlExecution final {
 public:
  ScopedTrustedFrameXmlExecution(lua_State* state, const bool trusted)
      : state_(trusted ? state : nullptr) {
    if (state_ == nullptr) {
      return;
    }
    saved_ = openwow::ui::lua_get_execution_taint_state(state_);
    openwow::ui::lua_set_execution_taint_state(state_, {});
  }

  ~ScopedTrustedFrameXmlExecution() {
    if (state_ != nullptr) {
      openwow::ui::lua_set_execution_taint_state(state_, saved_);
    }
  }

  ScopedTrustedFrameXmlExecution(const ScopedTrustedFrameXmlExecution&) = delete;
  ScopedTrustedFrameXmlExecution& operator=(const ScopedTrustedFrameXmlExecution&) = delete;

 private:
  lua_State* state_{nullptr};
  openwow::ui::LuaExecutionTaintState saved_{};
};

}

struct FrameXmlLoader::PreparedXmlDocument {
  std::shared_ptr<const std::string> source;
  openwow::ui::xml::FrameXmlDocumentOrderResult document_order;
  openwow::ui::framexml::ParseResult frames;
  openwow::ui::FontXmlParseResult fonts;
};

FrameXmlLoader::FrameXmlLoader() = default;
FrameXmlLoader::~FrameXmlLoader() = default;

void FrameXmlLoader::SetVfs(const openwow::vfs::VirtualFileSystem* vfs) {
  if (vfs_ != vfs) {
    BeginLoadLifetime();
  }
  vfs_ = vfs;
}

void FrameXmlLoader::BeginLoadLifetime() {
  source_cache_.clear();
  prepared_xml_cache_.clear();
  source_cache_hit_count_ = 0;
  prepared_xml_build_count_ = 0;
  prepared_xml_cache_hit_count_ = 0;
}

std::shared_ptr<const std::string> FrameXmlLoader::ReadTextCached(
    const std::string& path) const {
  if (vfs_ == nullptr || path.empty()) {
    return {};
  }

  const std::string key =
      openwow::vfs::MakeClientPathIdentity(path).lookup_path;
  if (const auto cached = source_cache_.find(key);
      cached != source_cache_.end()) {
    ++source_cache_hit_count_;
    return cached->second;
  }

  auto loaded = vfs_->ReadTextFile(path);
  std::shared_ptr<const std::string> source;
  if (loaded.has_value()) {
    source = std::make_shared<const std::string>(std::move(*loaded));
  }
  source_cache_.emplace(key, source);
  return source;
}

std::shared_ptr<const FrameXmlLoader::PreparedXmlDocument>
FrameXmlLoader::BuildPreparedXmlDocument(
    std::shared_ptr<const std::string> source) {
  auto prepared = std::make_shared<PreparedXmlDocument>();
  prepared->source = std::move(source);
  if (prepared->source == nullptr) {
    prepared->document_order.error = "XML source is null";
    return prepared;
  }

  openwow::ui::xml::XMLNode root;
  std::string error;
  if (!openwow::ui::xml::ParseOwnedXmlDocument(
          *prepared->source, &root, &error)) {
    prepared->document_order.error =
        error.empty() ? "Failed to parse XML document" : std::move(error);
    return prepared;
  }

  prepared->document_order =
      openwow::ui::xml::ExtractFrameXmlTopLevelElements(root);
  prepared->frames = openwow::ui::framexml::ParseFrameXml(root);
  prepared->fonts = openwow::ui::ParseFontAndIntrinsicXml(root);
  return prepared;
}

std::shared_ptr<const FrameXmlLoader::PreparedXmlDocument>
FrameXmlLoader::GetPreparedXmlDocument(const std::string& path) {
  const std::string key =
      openwow::vfs::MakeClientPathIdentity(path).lookup_path;
  if (const auto cached = prepared_xml_cache_.find(key);
      cached != prepared_xml_cache_.end()) {
    ++prepared_xml_cache_hit_count_;
    return cached->second;
  }

  auto source = ReadTextCached(path);
  if (source == nullptr) {
    return {};
  }
  auto prepared = BuildPreparedXmlDocument(std::move(source));
  prepared_xml_cache_.emplace(key, prepared);
  ++prepared_xml_build_count_;
  return prepared;
}

void FrameXmlLoader::PrimePreparedXmlDocuments(
    const std::vector<std::string>& root_paths) {
  if (vfs_ == nullptr || root_paths.empty()) {
    return;
  }

  std::vector<std::string> queue;
  queue.reserve(root_paths.size());
  std::unordered_set<std::string> queued;
  queued.reserve(root_paths.size() * 2u);
  for (const auto& path : root_paths) {
    if (queued.insert(
            openwow::vfs::MakeClientPathIdentity(path).lookup_path)
            .second) {
      queue.push_back(path);
    }
  }

  std::size_t cursor = 0;
  while (cursor < queue.size()) {
    const std::size_t frontier_end = queue.size();
    struct PendingDocument {
      std::string path;
      std::string key;
      std::shared_ptr<const std::string> source;
      std::shared_ptr<const PreparedXmlDocument> prepared;
    };
    std::vector<PendingDocument> pending;
    pending.reserve(frontier_end - cursor);

    for (; cursor < frontier_end; ++cursor) {
      const auto& path = queue[cursor];
      const std::string key =
          openwow::vfs::MakeClientPathIdentity(path).lookup_path;
      if (const auto cached = prepared_xml_cache_.find(key);
          cached != prepared_xml_cache_.end()) {
        ++prepared_xml_cache_hit_count_;
        pending.push_back({path, key, {}, cached->second});
        continue;
      }
      auto source = ReadTextCached(path);
      if (source == nullptr) {
        continue;
      }
      pending.push_back({path, key, std::move(source), {}});
    }

    std::vector<std::size_t> build_indices;
    build_indices.reserve(pending.size());
    for (std::size_t index = 0; index < pending.size(); ++index) {
      if (pending[index].prepared == nullptr) {
        build_indices.push_back(index);
      }
    }

    if (!build_indices.empty()) {
      std::atomic<std::size_t> next_index{0u};
      const auto hardware_threads =
          std::max(1u, std::thread::hardware_concurrency());
      const std::size_t worker_count = std::min<std::size_t>(
          {build_indices.size(), static_cast<std::size_t>(hardware_threads),
           8u});
      std::vector<std::thread> workers;
      workers.reserve(worker_count);
      for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&]() {
          for (;;) {
            const std::size_t build_slot =
                next_index.fetch_add(1u, std::memory_order_relaxed);
            if (build_slot >= build_indices.size()) {
              return;
            }
            auto& item = pending[build_indices[build_slot]];
            item.prepared = BuildPreparedXmlDocument(std::move(item.source));
          }
        });
      }
      for (auto& worker : workers) {
        worker.join();
      }
    }

    for (auto& item : pending) {
      if (item.prepared == nullptr) {
        continue;
      }
      if (!prepared_xml_cache_.contains(item.key)) {
        prepared_xml_cache_.emplace(item.key, item.prepared);
        ++prepared_xml_build_count_;
      }
      if (!item.prepared->document_order.ok) {
        continue;
      }

      const std::string base_dir =
          std::filesystem::path(item.path).parent_path().string();
      for (const auto& element : item.prepared->document_order.elements) {
        if (element.kind !=
            openwow::ui::xml::FrameXmlTopLevelElementKind::kInclude) {
          continue;
        }
        const auto include_path = NormalizeFrameXmlPath(element.value, base_dir);
        if (include_path.empty() ||
            ToLowerAscii(std::filesystem::path(include_path).extension().string()) ==
                ".lua") {
          continue;
        }
        const std::string include_key =
            openwow::vfs::MakeClientPathIdentity(include_path).lookup_path;
        if (queued.insert(include_key).second) {
          queue.push_back(include_path);
        }
      }
    }
  }
}

bool FrameXmlLoader::AccumulateFileDigest(
    const std::string& path, openwow::core::MD5Context* digest,
    std::unordered_set<std::string>* xml_stack, std::string* error) {
  const auto source = ReadTextCached(path);
  if (source == nullptr) {
    if (error != nullptr) *error = "FrameXML file not found: " + path;
    return false;
  }
  openwow::core::MD5_Update(digest, source->data(), source->size());
  if (ToLowerAscii(std::filesystem::path(path).extension().string()) == ".lua") {
    return true;
  }

  const std::string key =
      openwow::vfs::MakeClientPathIdentity(path).lookup_path;
  if (!xml_stack->insert(key).second) {
    if (error != nullptr) *error = "FrameXML include cycle: " + path;
    return false;
  }
  const auto erase_stack_entry = [&]() { xml_stack->erase(key); };
  const auto prepared = GetPreparedXmlDocument(path);
  if (prepared == nullptr || !prepared->document_order.ok) {
    if (error != nullptr) {
      *error = "FrameXML parse failed before integrity validation: " + path;
    }
    erase_stack_entry();
    return false;
  }

  const std::string base_dir = std::filesystem::path(path).parent_path().string();
  for (const auto& element : prepared->document_order.elements) {
    if (element.kind != openwow::ui::xml::FrameXmlTopLevelElementKind::kInclude &&
        element.kind != openwow::ui::xml::FrameXmlTopLevelElementKind::kScriptFile) {
      continue;
    }
    const std::string child = NormalizeFrameXmlPath(element.value, base_dir);
    if (!child.empty() &&
        !AccumulateFileDigest(child, digest, xml_stack, error)) {
      erase_stack_entry();
      return false;
    }
  }
  erase_stack_entry();
  return true;
}

bool FrameXmlLoader::ComputeTocDigest(
    const std::string& toc_path, std::array<std::uint8_t, 16>* digest,
    std::string* error, const std::string_view trailing_file_path) {
  if (vfs_ == nullptr || digest == nullptr) {
    if (error != nullptr) *error = vfs_ == nullptr ? "VFS not set" : "Digest output not set";
    return false;
  }
  const auto toc = ReadTextCached(toc_path);
  if (toc == nullptr) {
    if (error != nullptr) *error = "TOC file not found: " + toc_path;
    return false;
  }

  openwow::core::MD5Context context{};
  openwow::core::MD5_Init(&context);
  openwow::core::MD5_Update(&context, toc->data(), toc->size());
  const std::string base_dir = std::filesystem::path(toc_path).parent_path().string();
  std::unordered_set<std::string> xml_stack;
  for (const auto& line : openwow::ui::TOCParser::ExtractVisibleFileEntries(*toc, true)) {
    const std::string path = NormalizeFrameXmlPath(line, base_dir);
    if (!path.empty() && !AccumulateFileDigest(path, &context, &xml_stack, error)) {
      return false;
    }
  }
  if (!trailing_file_path.empty()) {
    const auto trailing = ReadTextCached(std::string(trailing_file_path));
    if (trailing == nullptr) {
      if (error != nullptr) {
        *error = "FrameXML integrity file not found: " +
                 std::string(trailing_file_path);
      }
      return false;
    }
    openwow::core::MD5_Update(&context, trailing->data(), trailing->size());
  }
  openwow::core::MD5_Final(&context, digest->data());
  return true;
}

bool FrameXmlLoader::HasCachedFile(const std::string_view path) const {
  return ReadTextCached(std::string(path)) != nullptr;
}

bool FrameXmlLoader::LoadBindingXml(
    openwow::game::BindingProfiles& profiles,
    lua_State* state,
    const std::string_view path,
    UiLoadStatusSink* status_sink,
    openwow::core::MD5Context* digest) {
  const auto source = ReadTextCached(std::string(path));
  if (source == nullptr) {
    AppendStatus(status_sink, 2, "Couldn't open " + std::string(path));
    return false;
  }
  if (digest != nullptr) {
    openwow::core::MD5_Update(digest, source->data(), source->size());
  }
  return openwow::game::actions::bindings::adapters::xml::BindingXmlAdapter::
      LoadText(profiles, state, path, *source, status_sink);
}

void FrameXmlLoader::SetXmlProcessCallback(XmlProcessCallback callback) {
  xml_process_callback_ = std::move(callback);
}

void FrameXmlLoader::SetXmlFontProcessCallback(XmlFontProcessCallback callback) {
  xml_font_process_callback_ = std::move(callback);
}

TocFileList FrameXmlLoader::ParseToc(const std::string& toc_path,
                                     TocLoadProgress* progress) const {
  TocFileList result;
  if (!vfs_) {
    result.error = "VFS not set";
    return result;
  }

  const auto toc_text = ReadTextCached(toc_path);
  if (toc_text == nullptr) {
    result.error = "TOC file not found: " + toc_path;
    return result;
  }

  const std::filesystem::path toc_fs(toc_path);
  const std::string base_dir = toc_fs.parent_path().string();

  const auto lines = openwow::ui::TOCParser::ExtractVisibleFileEntries(
      *toc_text, true);
  if (progress && progress->callback && progress->total <= progress->completed) {
    progress->total = progress->completed + static_cast<int>(lines.size());
  }

  std::unordered_set<std::string> seen_xml;
  seen_xml.reserve(lines.size() * 4);

  for (const auto& line : lines) {
    const auto path = NormalizeFrameXmlPath(line, base_dir);
    if (!path.empty()) {
      const auto ext = ToLowerAscii(
          std::filesystem::path(path).extension().string());
      if (ext != ".lua") {

        std::vector<std::string> expanded;
        std::function<void(const std::string&)> expand =
            [&](const std::string& xml_path) {
              const std::string key =
                  openwow::vfs::MakeClientPathIdentity(xml_path).lookup_path;
              if (!seen_xml.insert(key).second) return;
              if (!vfs_->Exists(xml_path)) return;

              const auto xml_text = ReadTextCached(xml_path);
              if (xml_text != nullptr) {
                const std::filesystem::path xml_fs(xml_path);
                const std::string xml_dir = xml_fs.parent_path().string();
                for (const auto& inc : CollectIncludeRefs(*xml_text, xml_dir)) {
                  expand(inc);
                }
              }
              expanded.push_back(xml_path);
            };
        expand(path);
        for (auto& p : expanded) {
          result.xml_paths.push_back(std::move(p));
        }
      } else if (ext == ".lua") {
        if (vfs_->Exists(path)) {
          result.lua_paths.push_back(path);
        }
      }
    }

    if (progress && progress->callback && progress->completed < progress->total) {
      ++progress->completed;
      progress->callback(static_cast<float>(progress->completed)
                         / static_cast<float>(progress->total));
    }
  }

  for (const auto& xml_path : result.xml_paths) {
    const auto xml_text = ReadTextCached(xml_path);
    if (xml_text == nullptr) continue;
    const std::filesystem::path xml_fs(xml_path);
    const std::string xml_dir = xml_fs.parent_path().string();
    for (const auto& script : CollectScriptRefs(*xml_text, xml_dir)) {
      result.lua_paths.push_back(script);
    }
  }

  DedupPreserveOrder(&result.lua_paths);

  result.ok = !result.xml_paths.empty() || !result.lua_paths.empty();
  if (!result.ok) {
    result.error = "TOC did not yield any files: " + toc_path;
  }
  return result;
}

std::uint32_t FrameXmlLoader::ReadTocInterfaceVersion(
    const std::string& toc_path) const {
  if (!vfs_) {
    return 0;
  }

  const auto toc_text = ReadTextCached(toc_path);
  if (toc_text == nullptr) {
    return 0;
  }

  const auto toc = openwow::ui::TOCParser::Parse(*toc_text, toc_path);
  if (!toc.has_value()) {
    return 0;
  }

  return toc->GetInterfaceVersion();
}

int FrameXmlLoader::ReadTocVisibleEntryCount(const std::string& toc_path) const {
  if (!vfs_) {
    return 0;
  }

  const auto toc_text = ReadTextCached(toc_path);
  if (toc_text == nullptr) {
    return 0;
  }

  return static_cast<int>(
      openwow::ui::TOCParser::CountVisibleFileEntries(*toc_text, false));
}

bool FrameXmlLoader::LoadXml(lua_State* L,
                             const std::string& xml_path,
                             UiLoadStatusSink* status_sink,
                             openwow::core::MD5Context* digest) {
  FrameXmlLoadResult ignored;
  std::unordered_set<std::string> xml_stack;
  return ProcessXmlInterleaved(
      L, xml_path, status_sink, &ignored, &xml_stack, digest, {});
}

bool FrameXmlLoader::RunLua(lua_State* L,
                            const std::string& lua_path,
                            UiLoadStatusSink* status_sink,
                            openwow::core::MD5Context* digest) {
  return RunLuaInContext(L, lua_path, status_sink, digest, {});
}

bool FrameXmlLoader::RunLuaInContext(
    lua_State* L,
    const std::string& lua_path,
    UiLoadStatusSink* status_sink,
    openwow::core::MD5Context* digest,
    const AddonScriptContext& addon_context) {
  if (!vfs_ || !L) return false;

  const LuaStackGuard stack_guard(L);

  const ScopedTrustedFrameXmlExecution trusted_execution(
      L, addon_context.name.empty());

  const auto lua_text = ReadTextCached(lua_path);
  if (lua_text == nullptr) {
    const std::string message = "FrameXmlLoader: Lua file not found: " + lua_path;
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
    AppendStatus(status_sink, 0, message);
    return false;
  }

  if (digest != nullptr) {
    openwow::core::MD5_Update(digest, lua_text->data(), lua_text->size());
  }

  const std::string& src = *lua_text;

  const std::string chunk_name =
      openwow::vfs::BuildClientLuaChunkName(lua_path);
  InstallLuaAddonMemoryTracker(L);
  std::string inferred_owner;
  std::string_view owner_name = addon_context.name;
  if (owner_name.empty()) {
    inferred_owner = ExtractAddonNameFromLuaSource(chunk_name);
    owner_name = inferred_owner;
  }
  const ScopedLuaAddonMemoryOwner owner_scope(L, owner_name);
  if (openwow::ui::LoadClientLuaChunk(L, std::string_view(src), chunk_name.c_str()) != 0) {
    const char* err = lua_tostring(L, -1);
    const std::string message = "FrameXmlLoader: Lua compile error in " + lua_path
                                + ": " + (err ? err : "(null)");
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
    AppendStatus(status_sink, 0, message);
    return false;
  }

  int argument_count = 0;
  if (addon_context.has_chunk_arguments()) {

    lua_pushlstring(L, addon_context.name.data(), addon_context.name.size());
    lua_pushvalue(L, addon_context.namespace_stack_index);
    argument_count = 2;
  }

  if (lua_pcall(L, argument_count, 0, 0) != 0) {
    const char* err = lua_tostring(L, -1);
    const std::string message = "FrameXmlLoader: Lua runtime error in " + lua_path
                                + ": " + (err ? err : "(null)");
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
    AppendStatus(status_sink, 0, message);
    return false;
  }

  return true;
}

bool FrameXmlLoader::ExecuteInlineLua(lua_State* L,
                                      const std::string& lua_source,
                                      const std::string& source_name,
                                      UiLoadStatusSink* status_sink) {
  if (!L) return false;
  const ScopedTrustedFrameXmlExecution trusted_execution(
      L, ExtractAddonNameFromLuaSource(source_name).empty());
  const std::string chunk_name = "@" + source_name;
  InstallLuaAddonMemoryTracker(L);
  const ScopedLuaAddonMemoryOwner owner_scope(
      L, ExtractAddonNameFromLuaSource(chunk_name));
  if (openwow::ui::LoadClientLuaChunk(
          L, std::string_view(lua_source), chunk_name.c_str()) != 0) {
    const char* err = lua_tostring(L, -1);
    const std::string message = "FrameXmlLoader: inline Lua compile error in "
                                + source_name + ": " + (err ? err : "(null)");
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
    AppendStatus(status_sink, 0, message);
    lua_pop(L, 1);
    return false;
  }

  if (lua_pcall(L, 0, 0, 0) != 0) {
    const char* err = lua_tostring(L, -1);
    const std::string message = "FrameXmlLoader: inline Lua runtime error in "
                                + source_name + ": " + (err ? err : "(null)");
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
    AppendStatus(status_sink, 0, message);
    lua_pop(L, 1);
    return false;
  }

  return true;
}

bool FrameXmlLoader::ProcessFileInterleaved(
    lua_State* L,
    const std::string& path,
    UiLoadStatusSink* status_sink,
    FrameXmlLoadResult* result,
    std::unordered_set<std::string>* xml_stack,
    openwow::core::MD5Context* digest,
    const AddonScriptContext& addon_context) {
  if (!vfs_ || !L || path.empty() || result == nullptr || xml_stack == nullptr) {
    return false;
  }

  const auto ext = ToLowerAscii(std::filesystem::path(path).extension().string());
  if (ext == ".lua") {
    if (RunLuaInContext(L, path, status_sink, digest, addon_context)) {
      ++result->lua_files_loaded;
      result->loaded_files.push_back(path);
      return true;
    }
    return false;
  }

  return ProcessXmlInterleaved(
      L, path, status_sink, result, xml_stack, digest, addon_context);
}

bool FrameXmlLoader::ProcessXmlInterleaved(
    lua_State* L,
    const std::string& xml_path,
    UiLoadStatusSink* status_sink,
    FrameXmlLoadResult* result,
    std::unordered_set<std::string>* xml_stack,
    openwow::core::MD5Context* digest,
    const AddonScriptContext& addon_context) {
  if (!vfs_ || !L || result == nullptr || xml_stack == nullptr) {
    return false;
  }

  const std::string key =
      openwow::vfs::MakeClientPathIdentity(xml_path).lookup_path;
  if (!xml_stack->insert(key).second) {
    const std::string message = "FrameXmlLoader: XML include cycle: " + xml_path;
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
    AppendStatus(status_sink, 0, message);
    return false;
  }

  const auto erase_stack_entry = [&]() { xml_stack->erase(key); };
  const auto prepared = GetPreparedXmlDocument(xml_path);
  if (prepared == nullptr) {
    const std::string message = "FrameXmlLoader: XML file not found: " + xml_path;
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
    AppendStatus(status_sink, 0, message);
    erase_stack_entry();
    return false;
  }

  if (digest != nullptr) {
    openwow::core::MD5_Update(digest, prepared->source->data(),
                             prepared->source->size());
  }

  const auto& document_order = prepared->document_order;
  if (!document_order.ok) {
    const std::string message = "FrameXmlLoader: XML parse error in " + xml_path
                                + ": " + document_order.error;
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
    AppendStatus(status_sink, 0, message);
    erase_stack_entry();
    return false;
  }

  const auto& parsed = prepared->frames;
  if (!parsed.ok) {
    const std::string message = "FrameXmlLoader: FrameXML parse error in " + xml_path
                                + ": " + parsed.error;
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
    AppendStatus(status_sink, 0, message);
    erase_stack_entry();
    return false;
  }

  const auto& font_parse = prepared->fonts;
  if (!font_parse.ok) {
    const std::string message = "FrameXmlLoader: FontXML parse error in " + xml_path
                                + ": " + font_parse.error;
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
    AppendStatus(status_sink, 0, message);
    erase_stack_entry();
    return false;
  }

  const std::filesystem::path xml_fs(xml_path);
  const std::string xml_dir = xml_fs.parent_path().string();
  std::size_t group_index = 0;
  std::size_t font_index = 0;

  for (const auto& element : document_order.elements) {
    switch (element.kind) {
      case openwow::ui::xml::FrameXmlTopLevelElementKind::kInclude: {
        const auto resolved = NormalizeFrameXmlPath(element.value, xml_dir);
        if (!resolved.empty()) {

          if (!ProcessFileInterleaved(
                  L, resolved, status_sink, result, xml_stack, digest,
                  addon_context)) {
            ++result->file_failures;
          }
        }
        break;
      }
      case openwow::ui::xml::FrameXmlTopLevelElementKind::kScriptFile: {
        const auto resolved = NormalizeFrameXmlPath(element.value, xml_dir);
        if (!resolved.empty()) {
          if (RunLuaInContext(L, resolved, status_sink, digest,
                              addon_context)) {
            ++result->lua_files_loaded;
            result->loaded_files.push_back(resolved);
          } else {
            ++result->file_failures;
          }
        }

        break;
      }
      case openwow::ui::xml::FrameXmlTopLevelElementKind::kScriptInline: {

        (void)ExecuteInlineLua(
            L, element.value, xml_path + ":<Script>", status_sink);
        break;
      }
      case openwow::ui::xml::FrameXmlTopLevelElementKind::kFont:

        if (!element.value.empty()) {
          if (font_index < font_parse.fonts.size() && xml_font_process_callback_) {
            xml_font_process_callback_(xml_path, font_parse.fonts[font_index]);
          }
          ++font_index;
        }
        break;
      case openwow::ui::xml::FrameXmlTopLevelElementKind::kFrame: {
        if (group_index < parsed.top_level_groups.size() && xml_process_callback_) {
          xml_process_callback_(xml_path, parsed, group_index);
        }
        ++group_index;
        break;
      }
    }
  }

  ++result->xml_files_loaded;
  result->loaded_files.push_back(xml_path);
  erase_stack_entry();

  return true;
}

FrameXmlLoadResult FrameXmlLoader::LoadToc(lua_State* L,
                                           const std::string& toc_path,
                                           TocLoadProgress* progress,
                                           openwow::core::MD5Context* digest,
                                           const std::string_view addon_name) {
  FrameXmlLoadResult result;
  UiLoadStatusBuffer diagnostics;

  if (!vfs_ || !L) {
    result.error = !vfs_ ? "VFS not set" : "Lua state not set";
    return result;
  }

  const LuaStackGuard stack_guard(L);

  const auto source_hits_before = source_cache_hit_count_;
  const auto prepared_builds_before = prepared_xml_build_count_;
  const auto prepared_hits_before = prepared_xml_cache_hit_count_;

  const auto toc_text = ReadTextCached(toc_path);
  if (toc_text == nullptr) {
    result.error = "TOC file not found: " + toc_path;
    return result;
  }

  if (digest != nullptr) {
    openwow::core::MD5_Update(digest, toc_text->data(), toc_text->size());
  }

  const std::filesystem::path toc_fs(toc_path);
  const std::string base_dir = toc_fs.parent_path().string();
  const auto lines = openwow::ui::TOCParser::ExtractVisibleFileEntries(
      *toc_text, true);
  if (progress && progress->callback && progress->total <= progress->completed) {
    progress->total = progress->completed + static_cast<int>(lines.size());
  }

  std::vector<std::string> xml_roots;
  xml_roots.reserve(lines.size());
  for (const auto& line : lines) {
    const auto path = NormalizeFrameXmlPath(line, base_dir);
    if (path.empty() ||
        ToLowerAscii(std::filesystem::path(path).extension().string()) ==
            ".lua") {
      continue;
    }
    xml_roots.push_back(path);
  }
  const auto preparation_started = std::chrono::steady_clock::now();
  PrimePreparedXmlDocuments(xml_roots);
  const auto preparation_finished = std::chrono::steady_clock::now();

  AddonScriptContext addon_context;
  if (!addon_name.empty()) {

    lua_newtable(L);
    addon_context = {addon_name, lua_gettop(L)};
  }

  std::unordered_set<std::string> xml_stack;
  for (const auto& line : lines) {
    const auto path = NormalizeFrameXmlPath(line, base_dir);
    UiLoadStatusBuffer file_status;
    if (!path.empty()) {

      if (!ProcessFileInterleaved(
              L, path, &file_status, &result, &xml_stack, digest,
              addon_context)) {
        ++result.file_failures;
      }
    } else {
      file_status.AppendStatus(0, "FrameXmlLoader: empty TOC file entry in " + toc_path);
    }
    file_status.ReplayInto(diagnostics);

    if (progress && progress->callback && progress->completed < progress->total) {
      ++progress->completed;
      progress->callback(static_cast<float>(progress->completed)
                         / static_cast<float>(progress->total));
    }
  }

  result.ok = true;
  result.diagnostics = diagnostics.entries();
  result.prepared_xml_documents =
      prepared_xml_build_count_ - prepared_builds_before;
  result.prepared_xml_cache_hits =
      prepared_xml_cache_hit_count_ - prepared_hits_before;
  result.source_cache_hits = source_cache_hit_count_ - source_hits_before;
  result.preparation_time_us = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          preparation_finished - preparation_started)
          .count());
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "FrameXmlLoader: loaded " +
                         std::to_string(result.xml_files_loaded) + " XML + " +
                         std::to_string(result.lua_files_loaded) +
                         " Lua files; prepared_xml=" +
                         std::to_string(result.prepared_xml_documents) +
                         " prepared_cache_hits=" +
                         std::to_string(result.prepared_xml_cache_hits) +
                         " source_cache_hits=" +
                         std::to_string(result.source_cache_hits) +
                         " prepare_us=" +
                         std::to_string(result.preparation_time_us));
  return result;
}

FrameXmlLoadResult FrameXmlLoader::LoadDefaultFrameXml(lua_State* L,
                                                       TocLoadProgress* progress,
                                                       openwow::core::MD5Context* digest) {

  return LoadToc(L, "/Interface/FrameXML/FrameXML.toc", progress, digest);
}

std::string FrameXmlLoader::NormalizeFrameXmlPath(const std::string& filename,
                                                  const std::string& base_dir) const {
  return openwow::ui::framexml::ResolveVirtualPath(filename, base_dir);
}

std::vector<std::string> FrameXmlLoader::CollectScriptRefs(
    const std::string& xml_content, const std::string& base_dir) const {
  std::vector<std::string> out;
  if (xml_content.empty()) return out;

  const auto elements = openwow::ui::xml::ExtractFrameXmlTopLevelElements(xml_content);
  if (!elements.ok) {
    return out;
  }

  for (const auto& element : elements.elements) {
    if (element.kind != openwow::ui::xml::FrameXmlTopLevelElementKind::kScriptFile) {
      continue;
    }
    const auto path = NormalizeFrameXmlPath(element.value, base_dir);
    if (!path.empty() && vfs_ && vfs_->Exists(path)) {
      out.push_back(path);
    }
  }

  return out;
}

std::vector<std::string> FrameXmlLoader::CollectIncludeRefs(
    const std::string& xml_content, const std::string& base_dir) const {
  std::vector<std::string> out;
  if (xml_content.empty()) return out;

  const auto elements = openwow::ui::xml::ExtractFrameXmlTopLevelElements(xml_content);
  if (!elements.ok) {
    return out;
  }

  for (const auto& element : elements.elements) {
    if (element.kind != openwow::ui::xml::FrameXmlTopLevelElementKind::kInclude) {
      continue;
    }
    const auto path = NormalizeFrameXmlPath(element.value, base_dir);
    if (!path.empty()) {
      out.push_back(path);
    }
  }

  return out;
}

}
