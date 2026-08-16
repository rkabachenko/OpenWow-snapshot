
#include "openwow/game/knowledge_base.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/game/client_config.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/xml/xml_tree.h"
#include "openwow/foundation/text/ascii.h"

#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <thread>
#include <utility>

namespace openwow::game {

namespace {

constexpr int kKnowledgeBaseRequestTimeoutMs = 0;

constexpr std::array<const char *, 9> kKnowledgeBaseLanguageTable = {
    "enUS", "koKR", "frFR", "deDE", "zhCN", "zhTW", "esES", "esMX", "ruRU",
};

bool IsAsciiLetterOrDigit(const unsigned char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
}

void AppendPercentEscapedQuery(std::string &output, const std::string_view search_query) {
  static constexpr char kHexDigits[] = "0123456789ABCDEF";

  for (const unsigned char ch : search_query) {
    if (IsAsciiLetterOrDigit(ch)) {
      output.push_back(static_cast<char>(ch));
      continue;
    }

    output.push_back('%');
    output.push_back(kHexDigits[ch >> 4]);
    output.push_back(kHexDigits[ch & 0x0F]);
  }
}

const char *RequestEventName(const KnowledgeBase::RequestKind kind, const bool is_error) {
  switch (kind) {
  case KnowledgeBase::RequestKind::kSetup:
    return is_error ? ui::game::events::KNOWLEDGE_BASE_SETUP_LOAD_FAILURE
                    : ui::game::events::KNOWLEDGE_BASE_SETUP_LOAD_SUCCESS;
  case KnowledgeBase::RequestKind::kQuery:
    return is_error ? ui::game::events::KNOWLEDGE_BASE_QUERY_LOAD_FAILURE
                    : ui::game::events::KNOWLEDGE_BASE_QUERY_LOAD_SUCCESS;
  case KnowledgeBase::RequestKind::kArticle:
    return is_error ? ui::game::events::KNOWLEDGE_BASE_ARTICLE_LOAD_FAILURE
                    : ui::game::events::KNOWLEDGE_BASE_ARTICLE_LOAD_SUCCESS;
  }

  return nullptr;
}

const ui::xml::CXMLNode *FindChildByTag(const ui::xml::CXMLNode *node, const std::string_view tag) {
  const std::string child_tag(tag);
  return ui::xml::XMLNode_FindChildByNameNoCase(node, child_tag.c_str());
}

const char *FindAttributeValue(const ui::xml::CXMLNode *node, const std::string_view name) {
  if (node == nullptr) {
    return nullptr;
  }

  for (const auto &attribute : node->attributes.entries) {
    if (text::EqualsIgnoreCaseAscii(attribute.name, name)) {
      return attribute.value.c_str();
    }
  }

  return nullptr;
}

std::optional<std::string> ReadOptionalAttribute(const ui::xml::CXMLNode *node,
                                                 const std::string_view name) {
  const char *const value = FindAttributeValue(node, name);
  if (value == nullptr) {
    return std::nullopt;
  }

  return std::string(value);
}

std::optional<std::string> ReadOptionalNodeText(const ui::xml::CXMLNode *node) {
  if (node == nullptr || node->text == nullptr) {
    return std::nullopt;
  }

  return std::string(node->text, node->text_size);
}

std::optional<std::string> ReadOptionalChildNodeText(const ui::xml::CXMLNode *node,
                                                     const std::string_view child_tag) {
  return ReadOptionalNodeText(FindChildByTag(node, child_tag));
}

std::uint32_t ParseUnsignedAttribute(const ui::xml::CXMLNode *node, const std::string_view name) {
  const char *const value = FindAttributeValue(node, name);
  if (value == nullptr) {
    return 0;
  }

  return static_cast<std::uint32_t>(std::strtoul(value, nullptr, 10));
}

std::int32_t ParseSignedDecimalPrefixAsAtol32(const char *text) {
  if (text == nullptr) {
    return 0;
  }

  const auto *cursor = reinterpret_cast<const unsigned char *>(text);
  while (*cursor != '\0' && std::isspace(*cursor) != 0) {
    ++cursor;
  }

  bool is_negative = false;
  if (*cursor == '+' || *cursor == '-') {
    is_negative = *cursor == '-';
    ++cursor;
  }

  const std::int64_t limit =
      is_negative ? static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1
                  : std::numeric_limits<std::int32_t>::max();
  std::int64_t value = 0;
  bool saw_digit = false;
  while (*cursor != '\0' && std::isdigit(*cursor) != 0) {
    saw_digit = true;
    const std::int64_t digit = *cursor - '0';
    if (value > (limit - digit) / 10) {
      return is_negative ? std::numeric_limits<std::int32_t>::min()
                         : std::numeric_limits<std::int32_t>::max();
    }

    value = value * 10 + digit;
    ++cursor;
  }

  if (!saw_digit) {
    return 0;
  }

  if (!is_negative) {
    return static_cast<std::int32_t>(value);
  }

  if (value == limit) {
    return std::numeric_limits<std::int32_t>::min();
  }

  return static_cast<std::int32_t>(-value);
}

std::int32_t ParseSignedAttributeAsAtol32(const ui::xml::CXMLNode *node,
                                          const std::string_view name) {
  return ParseSignedDecimalPrefixAsAtol32(FindAttributeValue(node, name));
}

std::uint32_t ParseUnsignedChildNodeText(const ui::xml::CXMLNode *node,
                                         const std::string_view child_tag) {
  const auto value = ReadOptionalChildNodeText(node, child_tag);
  if (!value.has_value()) {
    return 0;
  }

  return static_cast<std::uint32_t>(std::strtoul(value->c_str(), nullptr, 10));
}

bool ParseTrueAttribute(const ui::xml::CXMLNode *node, const std::string_view name) {
  const char *const value = FindAttributeValue(node, name);
  return value != nullptr && core::SStrCmpNoCase(value, "true", 0x7FFFFFFFu) == 0;
}

bool ParseTrueChildNodeText(const ui::xml::CXMLNode *node, const std::string_view child_tag) {
  const auto value = ReadOptionalChildNodeText(node, child_tag);
  return value.has_value() && core::SStrCmpNoCase(value->c_str(), "true", 0x7FFFFFFFu) == 0;
}

bool ParseArticleResponse(const void *data, const std::size_t len, KBArticle *const out_article) {
  if (out_article == nullptr) {
    return false;
  }

  if (data == nullptr || len == 0) {
    *out_article = KBArticle{};
    return false;
  }

  *out_article = KBArticle{};

  auto *const tree = ui::xml::XMLTree_Parse(data, len);
  if (tree == nullptr || tree->root == nullptr) {
    ui::xml::XMLTree_Free(tree);
    return false;
  }

  if (const auto *const article_node = FindChildByTag(tree->root, "Article");
      article_node != nullptr) {
    out_article->id = ParseUnsignedChildNodeText(article_node, "id");
    out_article->language_id = ParseUnsignedChildNodeText(article_node, "languageId");
    out_article->subject = ReadOptionalChildNodeText(article_node, "subject");
    out_article->subject_alt = ReadOptionalChildNodeText(article_node, "subjectAlt");
    out_article->keyword = ReadOptionalChildNodeText(article_node, "keyword");
    out_article->hot_issue = ParseTrueChildNodeText(article_node, "hotIssue");
    out_article->body = ReadOptionalChildNodeText(article_node, "ArticleText");
  }

  ui::xml::XMLTree_Free(tree);
  return true;
}

struct ParsedSetupData {
  std::vector<KBLanguage> languages;
  std::vector<KBCategory> categories;
  std::vector<KBArticleHeader> article_headers;
  std::int32_t total_article_count = 0;
};

struct ParsedArticleHeadersData {
  std::vector<KBArticleHeader> article_headers;
  std::int32_t total_article_count = 0;
};

void ParseArticleHeadersNode(const ui::xml::CXMLNode *const article_headers_node,
                             ParsedArticleHeadersData *const out_headers) {
  if (article_headers_node == nullptr || out_headers == nullptr) {
    return;
  }

  out_headers->total_article_count = ParseSignedAttributeAsAtol32(article_headers_node, "articleCount");
  for (auto *child = article_headers_node->first_child; child != nullptr;
       child = child->right_sibling) {
    KBArticleHeader header;
    header.id = ParseUnsignedAttribute(child, "id");
    header.subject = ReadOptionalAttribute(child, "subject");
    header.hot_issue = ParseTrueAttribute(child, "hotIssue");
    header.updated = ParseTrueAttribute(child, "updated");
    out_headers->article_headers.push_back(std::move(header));
  }
}

KBCategory ParseSetupCategoryNode(const ui::xml::CXMLNode *const category_node) {
  KBCategory category;
  if (category_node == nullptr) {
    return category;
  }

  category.id = ParseUnsignedAttribute(category_node, "id");
  category.name = ReadOptionalAttribute(category_node, "caption");

  if (const auto *const subcategories = FindChildByTag(category_node, "SubCategories");
      subcategories != nullptr) {
    for (auto *child = subcategories->first_child; child != nullptr; child = child->right_sibling) {
      KBCategory subcategory;
      subcategory.id = ParseUnsignedAttribute(child, "id");
      subcategory.name = ReadOptionalAttribute(child, "caption");
      category.subcategories.push_back(std::move(subcategory));
    }
  }

  return category;
}

bool ParseSetupResponse(const void *data, const std::size_t len, ParsedSetupData *const out_setup) {
  if (out_setup == nullptr) {
    return false;
  }

  *out_setup = {};
  if (data == nullptr || len == 0) {
    return false;
  }

  auto *const tree = ui::xml::XMLTree_Parse(data, len);
  if (tree == nullptr || tree->root == nullptr) {
    ui::xml::XMLTree_Free(tree);
    return false;
  }

  if (const auto *const languages = FindChildByTag(tree->root, "Languages"); languages != nullptr) {
    for (auto *child = languages->first_child; child != nullptr; child = child->right_sibling) {
      KBLanguage language;
      language.id = ParseUnsignedAttribute(child, "id");
      language.name = ReadOptionalAttribute(child, "caption");
      out_setup->languages.push_back(std::move(language));
    }
  }

  if (const auto *const categories = FindChildByTag(tree->root, "Categories");
      categories != nullptr) {
    for (auto *child = categories->first_child; child != nullptr; child = child->right_sibling) {
      out_setup->categories.push_back(ParseSetupCategoryNode(child));
    }
  }

  if (const auto *const article_headers = FindChildByTag(tree->root, "ArticleHeaders");
      article_headers != nullptr) {
    ParsedArticleHeadersData parsed_headers;
    ParseArticleHeadersNode(article_headers, &parsed_headers);
    out_setup->article_headers = std::move(parsed_headers.article_headers);
    out_setup->total_article_count = parsed_headers.total_article_count;
  }

  ui::xml::XMLTree_Free(tree);
  return true;
}

bool ParseQueryResponse(const void *data, const std::size_t len,
                        ParsedArticleHeadersData *const out_query) {
  if (out_query == nullptr) {
    return false;
  }

  *out_query = {};
  if (data == nullptr || len == 0) {
    return false;
  }

  auto *const tree = ui::xml::XMLTree_Parse(data, len);
  if (tree == nullptr || tree->root == nullptr) {
    ui::xml::XMLTree_Free(tree);
    return false;
  }

  ParseArticleHeadersNode(FindChildByTag(tree->root, "ArticleHeaders"), out_query);

  ui::xml::XMLTree_Free(tree);
  return true;
}

}

KnowledgeBase &KnowledgeBase::Get() {
  static KnowledgeBase instance;
  return instance;
}

KnowledgeBase::KnowledgeBase()
    : setup_download_context_{this, RequestKind::kSetup},
      query_download_context_{this, RequestKind::kQuery},
      article_download_context_{this, RequestKind::kArticle},
      download_start_fn_([](const char *url, openwow::net::OsUrlDownloadCallbackFn callback_fn,
                            void *callback_data, int timeout_ms) {
        return openwow::net::OsURLDownload_Start(url, callback_fn, callback_data, timeout_ms);
      }) {}

bool KnowledgeBase::BeginSetupLoading(const std::uint32_t num_articles,
                                      const std::uint32_t page_number) {
  const auto request_url = BuildSetupRequestUrl(num_articles, page_number);
  std::lock_guard<std::mutex> lock(mutex_);

  if (setup_state_ == KBState::kLoading || setup_pending_)
    return false;

  setup_starting_ = true;
  pending_setup_response_.clear();
  setup_finalization_ = {};
  if (!download_start_fn_ ||
      !download_start_fn_(request_url.c_str(), &KnowledgeBase::DownloadCallback,
                          &setup_download_context_, kKnowledgeBaseRequestTimeoutMs)) {
    setup_starting_ = false;
    setup_state_ = KBState::kError;
    pending_setup_response_.clear();
    setup_finalization_ = {};
    return false;
  }

  languages_.clear();
  categories_.clear();
  setup_article_headers_.clear();
  setup_total_article_count_ = 0;
  setup_state_ = KBState::kLoading;
  setup_pending_ = true;
  setup_starting_ = false;
  return true;
}

bool KnowledgeBase::BeginQueryLoading(const std::string &search_query,
                                      const std::int32_t category_id,
                                      const std::int32_t num_articles,
                                      const std::int32_t page_number) {
  const auto request_url =
      BuildQueryRequestUrl(search_query, category_id, num_articles, page_number);
  std::lock_guard<std::mutex> lock(mutex_);

  if (query_state_ == KBState::kLoading || query_pending_)
    return false;

  query_starting_ = true;
  pending_query_response_.clear();
  query_finalization_ = {};
  if (!download_start_fn_ ||
      !download_start_fn_(request_url.c_str(), &KnowledgeBase::DownloadCallback,
                          &query_download_context_, kKnowledgeBaseRequestTimeoutMs)) {
    query_starting_ = false;
    query_state_ = KBState::kError;
    pending_query_response_.clear();
    query_finalization_ = {};
    return false;
  }

  query_article_headers_.clear();
  query_total_article_count_ = 0;
  query_state_ = KBState::kLoading;
  query_pending_ = true;
  query_starting_ = false;
  return true;
}

bool KnowledgeBase::BeginArticleLoading(const std::int32_t article_id,
                                        const std::int32_t search_type) {
  const auto request_url = BuildArticleRequestUrl(article_id, search_type);
  std::lock_guard<std::mutex> lock(mutex_);

  if (article_state_ == KBState::kLoading || article_pending_)
    return false;

  article_starting_ = true;
  pending_article_response_.clear();
  article_finalization_ = {};
  if (!download_start_fn_ ||
      !download_start_fn_(request_url.c_str(), &KnowledgeBase::DownloadCallback,
                          &article_download_context_, kKnowledgeBaseRequestTimeoutMs)) {
    article_starting_ = false;
    article_state_ = KBState::kError;
    pending_article_response_.clear();
    article_finalization_ = {};
    return false;
  }

  article_ = KBArticle{};
  article_state_ = KBState::kLoading;
  article_pending_ = true;
  article_starting_ = false;
  return true;
}

bool KnowledgeBase::OnHttpResponse(const void *data, std::size_t len, bool is_error,
                                   std::uint32_t http_status) {
  std::vector<const char *> script_events;
  bool handled_request = false;
  bool handled_successfully = true;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (setup_pending_) {
      handled_request = true;
      handled_successfully = ApplyHttpResponse(RequestKind::kSetup, data, len, is_error,
                                               http_status, &script_events) &&
                             handled_successfully;
    }
    if (query_pending_) {
      handled_request = true;
      handled_successfully = ApplyHttpResponse(RequestKind::kQuery, data, len, is_error,
                                               http_status, &script_events) &&
                             handled_successfully;
    }
    if (article_pending_) {
      handled_request = true;
      handled_successfully = ApplyHttpResponse(RequestKind::kArticle, data, len, is_error,
                                               http_status, &script_events) &&
                             handled_successfully;
    }
  }

  for (const char *event_name : script_events) {
    ui::game::ScriptEventDispatch::Get().FireEvent(event_name);
  }

  return handled_request && handled_successfully;
}

bool KnowledgeBase::Pump() {
  std::vector<const char *> script_events;
  bool completed_any_request = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (setup_pending_ && setup_finalization_.ready) {
      completed_any_request = true;
      (void)ApplyHttpResponse(RequestKind::kSetup, pending_setup_response_.data(),
                              pending_setup_response_.size(), setup_finalization_.is_error,
                              setup_finalization_.completion_code, &script_events);
      pending_setup_response_.clear();
      setup_finalization_ = {};
    }

    if (query_pending_ && query_finalization_.ready) {
      completed_any_request = true;
      (void)ApplyHttpResponse(RequestKind::kQuery, pending_query_response_.data(),
                              pending_query_response_.size(), query_finalization_.is_error,
                              query_finalization_.completion_code, &script_events);
      pending_query_response_.clear();
      query_finalization_ = {};
    }

    if (article_pending_ && article_finalization_.ready) {
      completed_any_request = true;
      (void)ApplyHttpResponse(RequestKind::kArticle, pending_article_response_.data(),
                              pending_article_response_.size(), article_finalization_.is_error,
                              article_finalization_.completion_code, &script_events);
      pending_article_response_.clear();
      article_finalization_ = {};
    }
  }

  for (const char *event_name : script_events) {
    ui::game::ScriptEventDispatch::Get().FireEvent(event_name);
  }

  return completed_any_request;
}

void KnowledgeBase::SetDownloadStartFnForTests(DownloadStartFn download_start_fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  download_start_fn_ = std::move(download_start_fn);
}

bool KnowledgeBase::DownloadCallback(void *callback_data, const std::uint8_t *bytes,
                                     const std::uint32_t byte_count, const std::uint32_t event_flag,
                                     const std::uint32_t completion_code) {
  auto *const context = static_cast<DownloadContext *>(callback_data);
  if (context == nullptr || context->owner == nullptr) {
    return false;
  }

  auto &knowledge_base = *context->owner;
  std::lock_guard<std::mutex> lock(knowledge_base.mutex_);

  auto *response = &knowledge_base.pending_setup_response_;
  auto *finalization = &knowledge_base.setup_finalization_;
  auto *starting = &knowledge_base.setup_starting_;
  auto *pending = &knowledge_base.setup_pending_;
  switch (context->kind) {
  case RequestKind::kSetup:
    break;
  case RequestKind::kQuery:
    response = &knowledge_base.pending_query_response_;
    finalization = &knowledge_base.query_finalization_;
    starting = &knowledge_base.query_starting_;
    pending = &knowledge_base.query_pending_;
    break;
  case RequestKind::kArticle:
    response = &knowledge_base.pending_article_response_;
    finalization = &knowledge_base.article_finalization_;
    starting = &knowledge_base.article_starting_;
    pending = &knowledge_base.article_pending_;
    break;
  }

  if (!*pending && !*starting) {
    return true;
  }

  if (event_flag == 0) {
    if (bytes != nullptr && byte_count != 0) {
      response->append(reinterpret_cast<const char *>(bytes), byte_count);
    }
    return true;
  }

  finalization->ready = true;
  finalization->is_error =
      completion_code !=
      static_cast<std::uint32_t>(openwow::net::OsUrlDownloadCompletionCode::kSuccess);
  finalization->completion_code = completion_code;
  return !finalization->is_error;
}

std::string KnowledgeBase::NormalizeKnowledgeBaseLocale(const std::string_view locale_code) {
  const auto &locale_ring = openwow::data::GetStartupLocaleRing();
  const int locale_index = openwow::data::FindStartupLocaleRingIndexOrEnUSFallback(locale_code);
  return locale_ring[static_cast<std::size_t>(locale_index)];
}

const char *KnowledgeBase::GetSupportBaseUrlForLocale(const std::string_view locale_code) {
  switch (openwow::data::FindStartupLocaleRingIndexOrEnUSFallback(locale_code)) {
  case 0:
  case 1:
  case 3:
  case 4:
  case 11:
    return "http://support.wow-europe.com/kb/";
  case 5:
    return "http://support.worldofwarcraft.co.kr/kb/";
  case 6:
  case 8:
    return "http://cn.kbase.blizzard.com/kb/wow/";
  case 7:
  case 9:
    return "http://support.wowtaiwan.com.tw/kb/";
  default:
    return "http://support.worldofwarcraft.com/kb/";
  }
}

int KnowledgeBase::ResolveKnowledgeBaseLanguageId(const std::string_view locale_code) {
  const auto normalized_locale = NormalizeKnowledgeBaseLocale(locale_code);
  if (normalized_locale == "enGB") {
    return 0;
  }

  for (std::size_t index = 0; index < kKnowledgeBaseLanguageTable.size(); ++index) {
    if (normalized_locale == kKnowledgeBaseLanguageTable[index]) {
      return static_cast<int>(index);
    }
  }

  return 0;
}

std::string KnowledgeBase::BuildSetupRequestUrl(const std::uint32_t num_articles,
                                                const std::uint32_t page_number) {
  const auto locale = NormalizeKnowledgeBaseLocale(ClientConfig::Get().GetLocale());
  std::string url = GetSupportBaseUrlForLocale(locale);
  url += "getKBSetup.xml?languageId=";
  url += std::to_string(ResolveKnowledgeBaseLanguageId(locale));
  url += "&numArticles=";
  url += std::to_string(num_articles);
  url += "&pageNumber=";
  url += std::to_string(page_number);
  url += "&locale=";
  url += locale;
  return url;
}

std::string KnowledgeBase::BuildQueryRequestUrl(const std::string_view search_query,
                                                const std::int32_t category_id,
                                                const std::int32_t num_articles,
                                                const std::int32_t page_number) {
  const auto locale = NormalizeKnowledgeBaseLocale(ClientConfig::Get().GetLocale());
  std::string url = GetSupportBaseUrlForLocale(locale);
  url += "sendKBQuery.xml?";

  if (!search_query.empty()) {
    url += "searchQuery=";
    AppendPercentEscapedQuery(url, search_query);
    if (category_id != -1) {
      url += "&categoryId=";
      url += std::to_string(category_id);
    }
    url += '&';
  } else if (category_id != -1) {
    url += "categoryId=";
    url += std::to_string(category_id);
    url += '&';
  }

  url += "languageId=";
  url += std::to_string(ResolveKnowledgeBaseLanguageId(locale));
  url += "&numArticles=";
  url += std::to_string(num_articles);
  url += "&pageNumber=";
  url += std::to_string(page_number);
  url += "&locale=";
  url += locale;
  return url;
}

std::string KnowledgeBase::BuildArticleRequestUrl(const std::int32_t article_id,
                                                  const std::int32_t search_type) {
  const auto locale = NormalizeKnowledgeBaseLocale(ClientConfig::Get().GetLocale());
  std::string url = GetSupportBaseUrlForLocale(locale);
  url += "sendKBArticleQuery.xml?articleId=";
  url += std::to_string(article_id);
  url += "&languageId=";
  url += std::to_string(ResolveKnowledgeBaseLanguageId(locale));
  url += "&searchType=";
  url += std::to_string(search_type);
  url += "&locale=";
  url += locale;
  return url;
}

bool KnowledgeBase::ApplyHttpResponse(RequestKind kind, const void *data, const std::size_t len,
                                      const bool is_error, const std::uint32_t ,
                                      std::vector<const char *> *const script_events) {
  bool request_succeeded = !is_error;
  switch (kind) {
  case RequestKind::kSetup: {
    if (request_succeeded) {
      ParsedSetupData parsed_setup;
      request_succeeded = ParseSetupResponse(data, len, &parsed_setup);
      if (request_succeeded) {
        languages_ = std::move(parsed_setup.languages);
        categories_ = std::move(parsed_setup.categories);
        setup_article_headers_ = std::move(parsed_setup.article_headers);
        setup_total_article_count_ = parsed_setup.total_article_count;
      }
    }

    setup_state_ = request_succeeded ? KBState::kLoaded : KBState::kError;
    setup_pending_ = false;
    break;
  }
  case RequestKind::kQuery: {
    if (request_succeeded) {
      ParsedArticleHeadersData parsed_query;
      request_succeeded = ParseQueryResponse(data, len, &parsed_query);
      if (request_succeeded) {
        query_article_headers_ = std::move(parsed_query.article_headers);
        query_total_article_count_ = parsed_query.total_article_count;
      } else {
        query_article_headers_.clear();
        query_total_article_count_ = 0;
      }
    } else {
      query_article_headers_.clear();
      query_total_article_count_ = 0;
    }

    query_state_ = request_succeeded ? KBState::kLoaded : KBState::kError;
    query_pending_ = false;
    break;
  }
  case RequestKind::kArticle: {
    if (request_succeeded) {
      KBArticle parsed_article;
      request_succeeded = ParseArticleResponse(data, len, &parsed_article);
      if (request_succeeded) {
        article_ = std::move(parsed_article);
      } else {
        article_ = KBArticle{};
      }
    } else {
      article_ = KBArticle{};
    }

    article_state_ = request_succeeded ? KBState::kLoaded : KBState::kError;
    article_pending_ = false;
    break;
  }
  }

  if (script_events != nullptr) {
    script_events->push_back(RequestEventName(kind, !request_succeeded));
  }

  return request_succeeded;
}

const KBLanguage *KnowledgeBase::GetLanguage(std::uint32_t index) const {
  if (index >= languages_.size())
    return nullptr;
  return &languages_[index];
}

const KBCategory *KnowledgeBase::GetCategory(std::uint32_t index) const {
  if (index >= categories_.size())
    return nullptr;
  return &categories_[index];
}

std::uint32_t KnowledgeBase::GetSubCategoryCount(std::uint32_t cat_index) const {
  if (cat_index >= categories_.size())
    return 0;
  return static_cast<std::uint32_t>(categories_[cat_index].subcategories.size());
}

const KBCategory *KnowledgeBase::GetSubCategory(std::uint32_t cat_index,
                                                std::uint32_t sub_index) const {
  if (cat_index >= categories_.size())
    return nullptr;
  const auto &subs = categories_[cat_index].subcategories;
  if (sub_index >= subs.size())
    return nullptr;
  return &subs[sub_index];
}

const KBArticleHeader *KnowledgeBase::GetSetupArticleHeader(std::uint32_t index) const {
  if (index >= setup_article_headers_.size())
    return nullptr;
  return &setup_article_headers_[index];
}

const KBArticleHeader *KnowledgeBase::GetQueryArticleHeader(std::uint32_t index) const {
  if (index >= query_article_headers_.size())
    return nullptr;
  return &query_article_headers_[index];
}

void KnowledgeBase::SetSystemMotdLines(const std::vector<std::string> &lines) {
  system_motd_.clear();

  for (std::size_t index = 0; index < lines.size(); ++index) {
    if (index != 0) {
      system_motd_.push_back('\n');
    }
    system_motd_ += lines[index];
  }
}

void KnowledgeBase::SetFormattedServerMessage(const std::uint32_t message_type,
                                              const std::string &formatted_text) {
  if (message_type == 3) {
    server_status_ = formatted_text;
    return;
  }

  server_notice_ = formatted_text;
}

void KnowledgeBase::ResetSystemMessages() {
  system_motd_.clear();
  server_status_.clear();
  server_notice_.clear();
}

bool KnowledgeBase::HasPendingRequestDataLocked() const {
  return setup_starting_ || query_starting_ || article_starting_ || setup_pending_ ||
         query_pending_ || article_pending_;
}

void KnowledgeBase::ResetRequestDataLocked() {
  setup_state_ = KBState::kIdle;
  languages_.clear();
  categories_.clear();
  setup_article_headers_.clear();
  setup_total_article_count_ = 0;
  query_state_ = KBState::kIdle;
  query_article_headers_.clear();
  query_total_article_count_ = 0;
  article_state_ = KBState::kIdle;
  article_ = KBArticle{};
  setup_starting_ = false;
  query_starting_ = false;
  article_starting_ = false;
  setup_pending_ = false;
  query_pending_ = false;
  article_pending_ = false;
  pending_setup_response_.clear();
  pending_query_response_.clear();
  pending_article_response_.clear();
  setup_finalization_ = {};
  query_finalization_ = {};
  article_finalization_ = {};
}

void KnowledgeBase::ShutdownRequestData() {
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!HasPendingRequestDataLocked()) {
        break;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    (void)Pump();
  }

  std::lock_guard<std::mutex> lock(mutex_);
  ResetRequestDataLocked();
}

void KnowledgeBase::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  ResetRequestDataLocked();
  ResetSystemMessages();
  download_start_fn_ = [](const char *url, openwow::net::OsUrlDownloadCallbackFn callback_fn,
                          void *callback_data, int timeout_ms) {
    return openwow::net::OsURLDownload_Start(url, callback_fn, callback_data, timeout_ms);
  };
}

}
