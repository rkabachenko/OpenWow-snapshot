#pragma once

#include "openwow/runtime/scheduling/jthread_compat.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::debug {

enum class InspectorShaderStage : std::uint8_t {
  kVertex,
  kPixel,
};

enum class InspectorShaderMode : std::uint8_t {
  kAuto = 0,
  kArb = 1,
  kGlsl = 2,
};

enum class InspectorShaderLanguage : std::uint8_t {
  kArb,
  kGlsl,
};

struct InspectorShaderModeEffect {
  bool arb_path_enabled{true};
  bool glsl_path_enabled{true};
  bool replay_selected_batch{true};

  bool operator==(const InspectorShaderModeEffect&) const = default;
};

struct InspectorShaderTextRange {
  std::size_t offset{0};
  std::size_t length{0};

  bool operator==(const InspectorShaderTextRange&) const = default;
};

[[nodiscard]] InspectorShaderTextRange RetailShaderLineRange(
    std::string_view source, int one_based_line,
    std::size_t starting_offset = 0) noexcept;

struct InspectorShaderCompileStatistics {
  std::uint32_t instructions{0};
  std::uint32_t temporaries{0};
  std::uint32_t parameters{0};
  std::uint32_t attributes{0};
  std::uint32_t address_registers{0};
  std::uint32_t alu_instructions{0};
  std::uint32_t texture_instructions{0};
  std::uint32_t texture_indirections{0};

  bool operator==(const InspectorShaderCompileStatistics&) const = default;
};

struct InspectorShaderBackendCompileResult {
  bool succeeded{false};
  std::string arb_log;
  std::string glsl_log;
  InspectorShaderCompileStatistics statistics;
};

struct InspectorShaderCompileRequest {
  std::uint64_t generation{0};
  std::uint64_t document_id{0};
  std::uint64_t source_revision{0};
  InspectorShaderStage stage{InspectorShaderStage::kVertex};
  InspectorShaderMode mode{InspectorShaderMode::kAuto};
  std::string edited_source;
  std::string previously_installed_source;
  openwow::core::stop_token cancellation;
};

struct InspectorShaderHighlight {
  InspectorShaderLanguage language{InspectorShaderLanguage::kArb};
  int reported_line{0};
  int reported_source_index{0};
  InspectorShaderTextRange range;

  bool operator==(const InspectorShaderHighlight&) const = default;
};

enum class InspectorShaderCompletionStatus : std::uint8_t {
  kStale,
  kReadyToInstall,
  kSucceeded,
  kFailed,
};

struct InspectorShaderCompletion {
  InspectorShaderCompletionStatus status{InspectorShaderCompletionStatus::kStale};
  std::uint64_t generation{0};
  std::uint64_t document_id{0};
  std::uint64_t source_revision{0};
  std::optional<std::string> source_to_install;
  std::optional<std::string> source_to_restore;
  std::string status_text;
  std::vector<InspectorShaderHighlight> highlights;
  bool replay_selected_batch{false};
};

struct InspectorShaderDocumentDescriptor {
  std::uint64_t document_id{0};
  InspectorShaderStage stage{InspectorShaderStage::kVertex};
  std::filesystem::path trusted_root;
  std::filesystem::path relative_path;
  std::string storage_version;
};

enum class InspectorShaderDocumentStatus : std::uint8_t {
  kApplied,
  kUnchanged,
  kStale,
  kConflict,
  kInvalidPath,
  kFailed,
};

struct InspectorShaderSaveRequest {
  std::uint64_t generation{0};
  std::uint64_t document_id{0};
  std::uint64_t source_revision{0};
  std::filesystem::path path;
  std::string source;
  std::string expected_storage_version;
  openwow::core::stop_token cancellation;
};

struct InspectorShaderSaveResult {
  bool succeeded{false};
  bool conflict{false};
  std::string storage_version;
};

struct InspectorShaderReloadRequest {
  std::uint64_t generation{0};
  std::uint64_t document_id{0};
  std::filesystem::path path;
  std::string expected_storage_version;
  openwow::core::stop_token cancellation;
};

struct InspectorShaderDocumentSnapshot {
  std::uint64_t document_id{0};
  std::uint64_t source_revision{0};
  InspectorShaderStage stage{InspectorShaderStage::kVertex};
  InspectorShaderMode mode{InspectorShaderMode::kAuto};
  std::filesystem::path path;
  std::string edited_source;
  std::string installed_source;
  std::string reference_source;
  std::string storage_version;
  std::string status_text;
  std::vector<InspectorShaderHighlight> highlights;
  bool dirty{false};
  bool can_undo{false};
  bool can_redo{false};
  bool compile_pending{false};
  bool save_pending{false};
  bool reload_pending{false};
};

class InspectorShaderEditorModel {
 public:
  InspectorShaderEditorModel(InspectorShaderStage stage,
                             std::string installed_source,
                             std::string reference_source,
                             std::size_t arb_starting_offset = 0);
  ~InspectorShaderEditorModel();

  InspectorShaderEditorModel(const InspectorShaderEditorModel&) = delete;
  InspectorShaderEditorModel& operator=(const InspectorShaderEditorModel&) =
      delete;

  [[nodiscard]] InspectorShaderDocumentSnapshot Snapshot() const;
  [[nodiscard]] InspectorShaderStage stage() const;
  [[nodiscard]] InspectorShaderMode mode() const;
  [[nodiscard]] std::string installed_source() const;
  [[nodiscard]] std::string reference_source() const;
  [[nodiscard]] std::string edited_source() const;
  [[nodiscard]] bool dirty() const;
  [[nodiscard]] bool compile_pending() const;

  [[nodiscard]] InspectorShaderDocumentStatus OpenDocument(
      InspectorShaderDocumentDescriptor descriptor, std::string source,
      std::string reference_source, bool discard_dirty = false);
  [[nodiscard]] InspectorShaderDocumentStatus SetEditedSource(
      std::string source);
  [[nodiscard]] InspectorShaderDocumentStatus Undo();
  [[nodiscard]] InspectorShaderDocumentStatus Redo();

  [[nodiscard]] InspectorShaderModeEffect SelectMode(
      InspectorShaderMode mode);
  [[nodiscard]] InspectorShaderCompileRequest BeginCompile(
      std::string edited_source);
  [[nodiscard]] InspectorShaderCompileRequest BeginCompile();
  [[nodiscard]] InspectorShaderCompletion CompleteCompile(
      std::uint64_t generation,
      const InspectorShaderBackendCompileResult& result);
  [[nodiscard]] InspectorShaderCompletion CompleteInstallation(
      std::uint64_t generation, bool succeeded, std::string status_text = {});
  void CancelCompile();

  [[nodiscard]] std::optional<InspectorShaderSaveRequest> BeginSave();
  [[nodiscard]] InspectorShaderDocumentStatus CompleteSave(
      std::uint64_t generation, const InspectorShaderSaveResult& result);
  [[nodiscard]] std::optional<InspectorShaderReloadRequest> BeginReload();
  [[nodiscard]] InspectorShaderDocumentStatus CompleteReload(
      std::uint64_t generation, std::string source,
      std::string storage_version, bool discard_dirty = false);
  void CancelFileOperations();

 private:
  struct PendingCompile {
    std::uint64_t generation{0};
    std::uint64_t document_epoch{0};
    std::uint64_t source_revision{0};
    std::shared_ptr<const std::string> edited_source;
    std::shared_ptr<const std::string> previous_source;
    bool ready_to_install{false};
    std::string success_status;
  };

  struct PendingSave {
    std::uint64_t generation{0};
    std::uint64_t document_epoch{0};
    std::uint64_t source_revision{0};
    std::shared_ptr<const std::string> source;
  };

  struct PendingReload {
    std::uint64_t generation{0};
    std::uint64_t document_epoch{0};
    std::uint64_t source_revision{0};
  };

  [[nodiscard]] static bool ResolvePath(
      const std::filesystem::path& trusted_root,
      const std::filesystem::path& relative_path,
      std::filesystem::path& resolved);
  [[nodiscard]] std::uint64_t NextGenerationLocked();
  [[nodiscard]] std::uint64_t NextRevisionLocked();
  void ReplaceEditedSourceLocked(std::shared_ptr<const std::string> source,
                                 bool record_history);
  void ResetHistoryLocked();
  void TrimHistoryLocked();
  void CancelCompileLocked() noexcept;
  void CancelFileOperationsLocked() noexcept;
  [[nodiscard]] std::string FormatSuccessStatus(
      const InspectorShaderCompileStatistics& statistics) const;
  [[nodiscard]] std::vector<InspectorShaderHighlight> ParseHighlights(
      const InspectorShaderBackendCompileResult& result,
      std::string_view edited_source) const;

  static constexpr std::size_t kMaxHistoryEntries = 64;
  static constexpr std::size_t kMaxHistoryBytes = 4U * 1024U * 1024U;
  static constexpr std::size_t kMaxStatusBytes = 128U * 1024U;

  mutable std::mutex mutex_;
  std::uint64_t document_id_{0};
  std::uint64_t document_epoch_{1};
  std::uint64_t source_revision_{1};
  InspectorShaderStage stage_;
  InspectorShaderMode mode_{InspectorShaderMode::kAuto};
  std::filesystem::path path_;
  std::string storage_version_;
  std::shared_ptr<const std::string> edited_source_;
  std::shared_ptr<const std::string> saved_source_;
  std::shared_ptr<const std::string> installed_source_;
  std::shared_ptr<const std::string> reference_source_;
  std::size_t arb_starting_offset_{0};
  std::uint64_t next_generation_{0};
  std::vector<std::shared_ptr<const std::string>> history_;
  std::size_t history_cursor_{0};
  std::size_t history_bytes_{0};
  std::string status_text_;
  std::vector<InspectorShaderHighlight> highlights_;
  openwow::core::stop_source compile_cancellation_;
  openwow::core::stop_source save_cancellation_;
  openwow::core::stop_source reload_cancellation_;
  std::optional<PendingCompile> pending_compile_;
  std::optional<PendingSave> pending_save_;
  std::optional<PendingReload> pending_reload_;
};

}
