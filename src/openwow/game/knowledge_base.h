
#pragma once

#include "openwow/net/os_url_download.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game {

enum class KBState : std::uint32_t {
  kIdle    = 0,
  kLoading = 1,
  kLoaded  = 2,
  kError   = 3,
};

struct KBArticleHeader {
  std::uint32_t id = 0;
  std::optional<std::string> subject;
  std::uint32_t language_id = 0;
  bool          hot_issue = false;
  bool          updated = false;
};

struct KBCategory {
  std::uint32_t id = 0;
  std::optional<std::string> name;
  std::vector<KBCategory> subcategories;
};

struct KBLanguage {
  std::uint32_t id = 0;
  std::optional<std::string> name;
};

struct KBArticle {
  std::uint32_t id = 0;
  std::uint32_t language_id = 0;
  std::optional<std::string> subject;
  std::optional<std::string> subject_alt;
  std::optional<std::string> body;
  std::optional<std::string> keyword;
  bool                       hot_issue = false;
};

class KnowledgeBase {
 public:
  static KnowledgeBase& Get();

  bool BeginSetupLoading(std::uint32_t num_articles, std::uint32_t page_number);

  bool BeginQueryLoading(const std::string& search_query,
                         std::int32_t category_id,
                         std::int32_t num_articles,
                         std::int32_t page_number);

  bool BeginArticleLoading(std::int32_t article_id,
                           std::int32_t search_type);

  bool OnHttpResponse(const void* data, std::size_t len, bool is_error,
                      std::uint32_t http_status);

  [[nodiscard]] KBState GetSetupState() const { return setup_state_; }

  [[nodiscard]] std::uint32_t GetLanguageCount() const {
    return static_cast<std::uint32_t>(languages_.size());
  }
  [[nodiscard]] const KBLanguage* GetLanguage(std::uint32_t index) const;

  [[nodiscard]] std::uint32_t GetCategoryCount() const {
    return static_cast<std::uint32_t>(categories_.size());
  }
  [[nodiscard]] const KBCategory* GetCategory(std::uint32_t index) const;

  [[nodiscard]] std::uint32_t GetSubCategoryCount(std::uint32_t cat_index) const;
  [[nodiscard]] const KBCategory* GetSubCategory(std::uint32_t cat_index,
                                                  std::uint32_t sub_index) const;

  [[nodiscard]] std::uint32_t GetSetupArticleHeaderCount() const {
    return static_cast<std::uint32_t>(setup_article_headers_.size());
  }
  [[nodiscard]] const KBArticleHeader* GetSetupArticleHeader(
      std::uint32_t index) const;
  [[nodiscard]] std::int32_t GetSetupTotalArticleCount() const {
    return setup_total_article_count_;
  }

  [[nodiscard]] KBState GetQueryState() const { return query_state_; }

  [[nodiscard]] std::uint32_t GetQueryArticleHeaderCount() const {
    return static_cast<std::uint32_t>(query_article_headers_.size());
  }
  [[nodiscard]] const KBArticleHeader* GetQueryArticleHeader(
      std::uint32_t index) const;

  [[nodiscard]] std::int32_t GetQueryTotalArticleCount() const {
    return query_total_article_count_;
  }

  [[nodiscard]] KBState GetArticleState() const { return article_state_; }
  [[nodiscard]] const KBArticle& GetArticle() const { return article_; }

  void SetSystemMotdLines(const std::vector<std::string>& lines);
  [[nodiscard]] const std::string& GetSystemMotd() const {
    return system_motd_;
  }

  void SetFormattedServerMessage(std::uint32_t message_type,
                                 const std::string& formatted_text);
  [[nodiscard]] const std::string& GetServerStatus() const {
    return server_status_;
  }
  [[nodiscard]] const std::string& GetServerNotice() const {
    return server_notice_;
  }
  void ResetSystemMessages();

  enum class RequestKind : std::uint8_t {
    kSetup,
    kQuery,
    kArticle,
  };

  [[nodiscard]] bool Pump();

  void SetDownloadStartFnForTests(
      std::function<bool(const char*, openwow::net::OsUrlDownloadCallbackFn,
                         void*, int)> download_start_fn);

  void ShutdownRequestData();
  void Reset();

 private:
  struct QueuedRequestFinalization {
    bool ready = false;
    bool is_error = false;
    std::uint32_t completion_code = 0;
  };

  struct DownloadContext {
    KnowledgeBase* owner = nullptr;
    RequestKind kind = RequestKind::kSetup;
  };

  using DownloadStartFn =
      std::function<bool(const char*, openwow::net::OsUrlDownloadCallbackFn,
                         void*, int)>;

  KnowledgeBase();

  [[nodiscard]] static bool DownloadCallback(
      void* callback_data,
      const std::uint8_t* bytes,
      std::uint32_t byte_count,
      std::uint32_t event_flag,
      std::uint32_t completion_code);
  [[nodiscard]] static std::string NormalizeKnowledgeBaseLocale(
      std::string_view locale_code);
  [[nodiscard]] static const char* GetSupportBaseUrlForLocale(
      std::string_view locale_code);
  [[nodiscard]] static int ResolveKnowledgeBaseLanguageId(
      std::string_view locale_code);
  [[nodiscard]] static std::string BuildSetupRequestUrl(
      std::uint32_t num_articles, std::uint32_t page_number);
  [[nodiscard]] static std::string BuildQueryRequestUrl(
      std::string_view search_query,
      std::int32_t category_id,
      std::int32_t num_articles,
      std::int32_t page_number);
  [[nodiscard]] static std::string BuildArticleRequestUrl(
      std::int32_t article_id, std::int32_t search_type);
  [[nodiscard]] bool ApplyHttpResponse(RequestKind kind,
                                       const void* data,
                                       std::size_t len,
                                       bool is_error,
                                       std::uint32_t completion_code,
                                       std::vector<const char*>* script_events);
  [[nodiscard]] bool HasPendingRequestDataLocked() const;
  void ResetRequestDataLocked();

  mutable std::mutex mutex_;

  KBState setup_state_ = KBState::kIdle;
  std::vector<KBLanguage> languages_;
  std::vector<KBCategory> categories_;
  std::vector<KBArticleHeader> setup_article_headers_;
  std::int32_t setup_total_article_count_ = 0;

  KBState query_state_ = KBState::kIdle;
  std::vector<KBArticleHeader> query_article_headers_;
  std::int32_t query_total_article_count_ = 0;

  KBState article_state_ = KBState::kIdle;
  KBArticle article_;

  std::string system_motd_;
  std::string server_status_;
  std::string server_notice_;

  bool setup_starting_ = false;
  bool query_starting_ = false;
  bool article_starting_ = false;
  bool setup_pending_ = false;
  bool query_pending_ = false;
  bool article_pending_ = false;
  std::string pending_setup_response_;
  std::string pending_query_response_;
  std::string pending_article_response_;
  QueuedRequestFinalization setup_finalization_;
  QueuedRequestFinalization query_finalization_;
  QueuedRequestFinalization article_finalization_;
  DownloadContext setup_download_context_;
  DownloadContext query_download_context_;
  DownloadContext article_download_context_;
  DownloadStartFn download_start_fn_;
};

}
