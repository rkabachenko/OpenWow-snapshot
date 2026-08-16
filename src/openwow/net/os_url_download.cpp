
#include "os_url_download.h"

#include "openwow/core/storm_thread.h"
#include "openwow/net/transport/tcp_client.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openwow::net {

namespace {

constexpr std::uint32_t kDefaultTimeoutMs = 10000;
constexpr int kMaxRedirects = 5;
constexpr std::size_t kMaxHttpHeaderBytes = 64u * 1024u;
constexpr std::size_t kMaxHttpResponseBytes = 256u * 1024u * 1024u;
constexpr std::uint32_t kDownloadRangeSuccessCode = 0;
constexpr std::uint32_t kDownloadRangeAlternateSuccessCode = 9;

std::uint64_t ComposeOffset64(const std::uint32_t lo,
                              const std::uint32_t hi) {
    return static_cast<std::uint64_t>(lo)
        | (static_cast<std::uint64_t>(hi) << 32);
}

std::uint32_t Low32(const std::uint64_t value) {
    return static_cast<std::uint32_t>(value & 0xFFFFFFFFu);
}

std::uint32_t High32(const std::uint64_t value) {
    return static_cast<std::uint32_t>(value >> 32);
}

bool IsSuccessfulDownloadRangeCompletionCode(const std::uint32_t completion_code) {
    return completion_code == kDownloadRangeSuccessCode
        || completion_code == kDownloadRangeAlternateSuccessCode;
}

bool InvokeUrlDownloadCallback(const OsUrlDownloadCallbackFn callback_fn,
                               void* const callback_data,
                               const std::uint8_t* const bytes,
                               const std::uint32_t byte_count,
                               const std::uint32_t event_flag,
                               const OsUrlDownloadCompletionCode completion_code) {
    if (!callback_fn) {
        return true;
    }

    return callback_fn(callback_data, bytes, byte_count, event_flag,
                       static_cast<std::uint32_t>(completion_code));
}

std::function<bool(std::string_view, std::string*)>& UrlDownloadHandler() {
    static std::function<bool(std::string_view, std::string*)> handler;
    return handler;
}

std::function<UrlDownloadTestResult(std::string_view, std::string*)>&
UrlDownloadResultHandler() {
    static std::function<UrlDownloadTestResult(std::string_view, std::string*)> handler;
    return handler;
}

std::function<void(std::string_view, std::uint32_t)>& UrlDownloadObserver() {
    static std::function<void(std::string_view, std::uint32_t)> observer;
    return observer;
}

bool AsciiEqualsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const unsigned char left = static_cast<unsigned char>(lhs[index]);
        const unsigned char right = static_cast<unsigned char>(rhs[index]);
        if (std::tolower(left) != std::tolower(right)) {
            return false;
        }
    }

    return true;
}

std::string_view TrimAsciiWhitespace(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

std::int64_t ParseRetailSignedInt32(std::string_view value) noexcept {
    value = TrimAsciiWhitespace(value);
    bool negative = false;
    if (!value.empty() && (value.front() == '+' || value.front() == '-')) {
        negative = value.front() == '-';
        value.remove_prefix(1);
    }

    std::uint64_t magnitude = 0;
    bool has_digit = false;
    constexpr std::uint64_t kNegativeLimit =
        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) + 1u;
    const std::uint64_t limit = negative
                                    ? kNegativeLimit
                                    : std::numeric_limits<std::int32_t>::max();
    for (const unsigned char ch : value) {
        if (ch < '0' || ch > '9') {
            break;
        }
        has_digit = true;
        const std::uint64_t digit = ch - '0';
        if (magnitude > (limit - digit) / 10u) {
            magnitude = limit;
            break;
        }
        magnitude = magnitude * 10u + digit;
    }
    if (!has_digit) {
        return 0;
    }
    if (negative) {
        return magnitude == kNegativeLimit
                   ? std::numeric_limits<std::int32_t>::min()
                   : -static_cast<std::int64_t>(magnitude);
    }
    return static_cast<std::int64_t>(magnitude);
}

struct ParsedHttpUrl {
    std::string host;
    std::uint16_t port{80};
    std::string path{"/"};
    bool secure{false};
    bool ipv6_literal{false};
};

bool ParseDecimalPort(std::string_view value, std::uint16_t* port) {
    if (!port || value.empty()) {
        return false;
    }

    std::uint32_t parsed = 0;
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), parsed, 10);
    if (error != std::errc{} || end != value.data() + value.size()
        || parsed == 0 || parsed > 0xFFFFu) {
        return false;
    }

    *port = static_cast<std::uint16_t>(parsed);
    return true;
}

bool ParseHttpUrl(std::string_view url, ParsedHttpUrl* parsed) {
    if (!parsed) {
        return false;
    }

    constexpr std::string_view kHttpPrefix = "http://";
    constexpr std::string_view kHttpsPrefix = "https://";
    std::size_t scheme_length = 0;
    bool secure = false;
    if (url.size() >= kHttpPrefix.size()
        && AsciiEqualsIgnoreCase(url.substr(0, kHttpPrefix.size()), kHttpPrefix)) {
        scheme_length = kHttpPrefix.size();
    } else if (url.size() >= kHttpsPrefix.size()
               && AsciiEqualsIgnoreCase(url.substr(0, kHttpsPrefix.size()),
                                        kHttpsPrefix)) {
        scheme_length = kHttpsPrefix.size();
        secure = true;
    } else {
        return false;
    }

    std::string_view remainder = url.substr(scheme_length);
    const std::size_t path_offset = remainder.find_first_of("/?#");
    std::string_view authority = path_offset == std::string_view::npos
                                     ? remainder
                                     : remainder.substr(0, path_offset);
    std::string_view target = path_offset == std::string_view::npos
                                  ? std::string_view{}
                                  : remainder.substr(path_offset);
    if (const std::size_t fragment = target.find('#');
        fragment != std::string_view::npos) {
        target = target.substr(0, fragment);
    }
    parsed->path = target.empty()
                       ? "/"
                       : target.front() == '?' ? "/" + std::string(target)
                                               : std::string(target);
    if (authority.empty()) {
        return false;
    }

    std::string_view host = authority;
    std::uint16_t port = secure ? 443u : 80u;
    bool ipv6_literal = false;
    if (authority.front() == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos) {
            return false;
        }
        host = authority.substr(1, close - 1);
        ipv6_literal = true;
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':'
                || !ParseDecimalPort(authority.substr(close + 2), &port)) {
                return false;
            }
        }
    } else if (const std::size_t colon = authority.rfind(':');
               colon != std::string_view::npos) {
        host = authority.substr(0, colon);
        if (!ParseDecimalPort(authority.substr(colon + 1), &port)) {
            return false;
        }
    }

    if (host.empty()
        || host.find_first_of("\r\n\t ") != std::string_view::npos) {
        return false;
    }

    parsed->host.assign(host);
    parsed->port = port;
    parsed->secure = secure;
    parsed->ipv6_literal = ipv6_literal;
    if (parsed->path.empty()) {
        parsed->path = "/";
    }
    return true;
}

bool ContainsHeaderDelimiter(const std::string_view value) {
    return value.find('\r') != std::string_view::npos
        || value.find('\n') != std::string_view::npos;
}

std::string PercentEscapeRequestTarget(const std::string_view target) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string escaped;
    escaped.reserve(target.size());
    for (const unsigned char ch : target) {
        const bool must_escape = ch <= 0x20u || ch >= 0x7Fu || ch == '"'
            || ch == '<' || ch == '>' || ch == '\\' || ch == '^'
            || ch == '`' || ch == '{' || ch == '|' || ch == '}';
        if (!must_escape) {
            escaped.push_back(static_cast<char>(ch));
            continue;
        }
        escaped.push_back('%');
        escaped.push_back(kHex[ch >> 4]);
        escaped.push_back(kHex[ch & 0x0Fu]);
    }
    return escaped;
}

std::optional<std::string> BuildHttpRequest(const ParsedHttpUrl& url,
                                            const BlizzardHttpRequest& options) {
    if ((options.post_body && options.content_type
         && ContainsHeaderDelimiter(*options.content_type))
        || (options.if_modified_since
            && ContainsHeaderDelimiter(*options.if_modified_since))
        || (options.cookie
            && (ContainsHeaderDelimiter(options.cookie->first)
                || ContainsHeaderDelimiter(options.cookie->second)))) {
        return std::nullopt;
    }

    std::string request;
    request.reserve(256 + url.path.size() + url.host.size()
                    + (options.post_body ? options.post_body->size() : 0u));
    request += (options.post_body
                    ? "POST "
                    : options.retrieve_body ? "GET " : "HEAD ");
    request += PercentEscapeRequestTarget(url.path);
    request += " HTTP/1.1\r\nHost: ";
    if (url.ipv6_literal) {
        request.push_back('[');
    }
    request += url.host;
    if (url.ipv6_literal) {
        request.push_back(']');
    }
    if (url.port != (url.secure ? 443u : 80u)) {
        request += ':';
        request += std::to_string(url.port);
    }
    request += "\r\nUser-Agent: Blizzard Web Client\r\nAccept: */*\r\n";
    if (options.post_body) {
        request += "Content-type: ";
        request += options.content_type.value_or("text/html");
        request += "\r\nContent-Length: ";
        request += std::to_string(options.post_body->size());
        request += "\r\n";
    }
    if (options.if_modified_since) {
        request += "If-Modified-Since: ";
        request += *options.if_modified_since;
        request += "\r\n";
    }
    if (options.inclusive_range
        && (options.inclusive_range->first != -1
            || options.inclusive_range->second != -1)) {
        request += "Range: bytes=";
        request += std::to_string(options.inclusive_range->first);
        request.push_back('-');
        request += std::to_string(options.inclusive_range->second);
        request += "\r\n";
    }
    if (options.cookie) {
        request += "Cookie: ";
        request += options.cookie->first;
        request.push_back('=');
        request += options.cookie->second;
        request += "\r\n";
    }
    request += "Accept-Encoding: identity\r\nConnection: close\r\n\r\n";
    if (options.post_body) {
        request += *options.post_body;
    }
    return request;
}

std::optional<std::string_view> FindHeaderValue(std::string_view header_block,
                                                std::string_view header_name) {
    std::size_t offset = 0;
    while (offset < header_block.size()) {
        const std::size_t line_end = header_block.find("\r\n", offset);
        const std::string_view line =
            line_end == std::string_view::npos
                ? header_block.substr(offset)
                : header_block.substr(offset, line_end - offset);
        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos
            && AsciiEqualsIgnoreCase(TrimAsciiWhitespace(line.substr(0, colon)),
                                     header_name)) {
            return TrimAsciiWhitespace(line.substr(colon + 1));
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        offset = line_end + 2;
    }

    return std::nullopt;
}

bool DecodeChunkedBody(std::string_view encoded, std::string* decoded) {
    if (!decoded) {
        return false;
    }

    decoded->clear();
    std::size_t offset = 0;
    while (offset < encoded.size()) {
        const std::size_t size_end = encoded.find("\r\n", offset);
        if (size_end == std::string_view::npos) {
            return false;
        }

        std::string_view size_token = encoded.substr(offset, size_end - offset);
        if (const std::size_t extension = size_token.find(';');
            extension != std::string_view::npos) {
            size_token = size_token.substr(0, extension);
        }
        size_token = TrimAsciiWhitespace(size_token);
        if (size_token.empty()) {
            return false;
        }

        std::uint64_t chunk_size = 0;
        const auto [token_end, error] = std::from_chars(
            size_token.data(), size_token.data() + size_token.size(),
            chunk_size, 16);
        if (error != std::errc{}
            || token_end != size_token.data() + size_token.size()
            || chunk_size > kMaxHttpResponseBytes) {
            return false;
        }
        offset = size_end + 2;
        if (chunk_size == 0) {

            return true;
        }

        if (chunk_size > encoded.size() - offset
            || encoded.size() - offset - static_cast<std::size_t>(chunk_size) < 2u
            || decoded->size() > kMaxHttpResponseBytes - chunk_size) {
            return false;
        }

        decoded->append(encoded.substr(offset, static_cast<std::size_t>(chunk_size)));
        offset += static_cast<std::size_t>(chunk_size);
        if (encoded.substr(offset, 2) != "\r\n") {
            return false;
        }
        offset += 2;
    }

    return false;
}

struct ParsedHttpResponse {
    BlizzardHttpResponse response;
    std::optional<std::string> redirect_location;
};

bool ParseHttpResponse(std::string_view raw_response, ParsedHttpResponse* response) {
    if (!response) {
        return false;
    }

    const std::size_t header_end = raw_response.find("\r\n\r\n");
    if (header_end == std::string_view::npos || header_end > kMaxHttpHeaderBytes) {
        return false;
    }

    const std::string_view header_block = raw_response.substr(0, header_end);
    const std::string_view body_block = raw_response.substr(header_end + 4);
    const std::size_t status_line_end = header_block.find("\r\n");
    const std::string_view status_line =
        status_line_end == std::string_view::npos
            ? header_block
            : header_block.substr(0, status_line_end);
    const std::size_t first_space = status_line.find(' ');
    if (first_space == std::string_view::npos) {
        return false;
    }

    const std::size_t second_space = status_line.find(' ', first_space + 1);
    const std::string_view status_token =
        second_space == std::string_view::npos
            ? status_line.substr(first_space + 1)
            : status_line.substr(first_space + 1, second_space - first_space - 1);
    int status_code = 0;
    const auto [status_end, status_error] = std::from_chars(
        status_token.data(), status_token.data() + status_token.size(),
        status_code, 10);
    if (status_error != std::errc{}
        || status_end != status_token.data() + status_token.size()
        || status_code < 100 || status_code > 999) {
        return false;
    }
    response->response = {};
    response->response.status_code = status_code;

    std::size_t header_offset = status_line_end == std::string_view::npos
                                    ? header_block.size()
                                    : status_line_end + 2u;
    while (header_offset < header_block.size()) {
        const std::size_t line_end = header_block.find("\r\n", header_offset);
        const std::string_view line =
            line_end == std::string_view::npos
                ? header_block.substr(header_offset)
                : header_block.substr(header_offset, line_end - header_offset);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            return false;
        }
        const std::string_view name = TrimAsciiWhitespace(line.substr(0, colon));
        const std::string_view value = TrimAsciiWhitespace(line.substr(colon + 1u));
        if (name.empty()) {
            return false;
        }
        response->response.headers.push_back(
            {.name = std::string(name), .value = std::string(value)});
        if (line_end == std::string_view::npos) {
            break;
        }
        header_offset = line_end + 2u;
    }

    response->redirect_location.reset();
    if (const auto location = FindHeaderValue(header_block, "Location")) {
        response->redirect_location = std::string(*location);
    }

    if (const auto transfer_encoding = FindHeaderValue(header_block, "Transfer-Encoding");
        transfer_encoding && AsciiEqualsIgnoreCase(TrimAsciiWhitespace(*transfer_encoding),
                                                   "chunked")) {
        return DecodeChunkedBody(body_block, &response->response.body);
    }

    if (const auto content_length = FindHeaderValue(header_block, "Content-Length");
        content_length) {
        std::uint64_t length = 0;
        const auto [length_end, length_error] = std::from_chars(
            content_length->data(), content_length->data() + content_length->size(),
            length, 10);
        if (length_error != std::errc{}
            || length_end != content_length->data() + content_length->size()
            || length > kMaxHttpResponseBytes || body_block.size() < length) {
            return false;
        }
        response->response.body.assign(
            body_block.substr(0, static_cast<std::size_t>(length)));
        return true;
    }

    if (body_block.size() > kMaxHttpResponseBytes) {
        return false;
    }
    response->response.body.assign(body_block);
    return true;
}

std::string ResolveRedirectUrl(const ParsedHttpUrl& base_url, std::string_view location) {
    if (location.empty()) {
        return {};
    }

    constexpr std::string_view kHttpPrefix = "http://";
    constexpr std::string_view kHttpsPrefix = "https://";
    if ((location.size() >= kHttpPrefix.size()
         && AsciiEqualsIgnoreCase(location.substr(0, kHttpPrefix.size()),
                                  kHttpPrefix))
        || (location.size() >= kHttpsPrefix.size()
            && AsciiEqualsIgnoreCase(location.substr(0, kHttpsPrefix.size()),
                                     kHttpsPrefix))) {
        return std::string(location);
    }

    std::string resolved = base_url.secure ? "https://" : "http://";
    if (base_url.ipv6_literal) {
        resolved.push_back('[');
    }
    resolved += base_url.host;
    if (base_url.ipv6_literal) {
        resolved.push_back(']');
    }
    if (base_url.port != (base_url.secure ? 443u : 80u)) {
        resolved += ':';
        resolved += std::to_string(base_url.port);
    }

    if (location.front() == '/') {
        resolved += location;
        return resolved;
    }

    std::string_view base_path = base_url.path;
    const std::size_t slash = base_path.rfind('/');
    if (slash == std::string_view::npos) {
        resolved += '/';
    } else {
        resolved.append(base_path.substr(0, slash + 1));
    }
    resolved += location;
    return resolved;
}

BlizzardHttpResult CompleteBlizzardHttpRequest(
    const BlizzardHttpCallbacks& callbacks,
    const OsUrlDownloadCompletionCode completion_code) {
    if (callbacks.on_complete) {
        callbacks.on_complete(completion_code);
    }
    return {
        .success = completion_code == OsUrlDownloadCompletionCode::kSuccess,
        .completion_code = completion_code,
    };
}

BlizzardHttpResult PerformBlizzardHttpRequestInternal(
    const BlizzardHttpRequest& request,
    const BlizzardHttpCallbacks& callbacks,
    const int redirect_depth) {
    if (redirect_depth > kMaxRedirects) {
        return CompleteBlizzardHttpRequest(
            callbacks, OsUrlDownloadCompletionCode::kFailure);
    }

    ParsedHttpUrl parsed_url;
    if (!ParseHttpUrl(request.url, &parsed_url)) {
        return CompleteBlizzardHttpRequest(
            callbacks, OsUrlDownloadCompletionCode::kFailure);
    }

    if (parsed_url.secure) {
        return CompleteBlizzardHttpRequest(
            callbacks, OsUrlDownloadCompletionCode::kFailure);
    }

    TcpClient client;
    if (!client.Connect(parsed_url.host, parsed_url.port, request.timeout_ms)) {
        return CompleteBlizzardHttpRequest(
            callbacks,
            OsUrlDownloadCompletionCode::kTransportDomainFailure);
    }

    const std::optional<std::string> wire_request =
        BuildHttpRequest(parsed_url, request);
    if (!wire_request) {
        return CompleteBlizzardHttpRequest(
            callbacks, OsUrlDownloadCompletionCode::kFailure);
    }
    const std::vector<std::uint8_t> request_bytes(
        wire_request->begin(), wire_request->end());
    if (!client.Write(request_bytes, request.timeout_ms)) {
        return CompleteBlizzardHttpRequest(
            callbacks,
            OsUrlDownloadCompletionCode::kTransportDomainFailure);
    }

    std::string raw_response;
    for (;;) {
        const std::vector<std::uint8_t> chunk =
            client.ReadSome(8192, request.timeout_ms);
        if (chunk.empty()) {
            break;
        }
        if (raw_response.size() > kMaxHttpResponseBytes - chunk.size()) {
            return CompleteBlizzardHttpRequest(
                callbacks, OsUrlDownloadCompletionCode::kFailure);
        }
        raw_response.append(reinterpret_cast<const char*>(chunk.data()), chunk.size());
    }

    if (raw_response.empty()) {
        return CompleteBlizzardHttpRequest(
            callbacks,
            OsUrlDownloadCompletionCode::kTransportDomainFailure);
    }

    ParsedHttpResponse response;
    if (!ParseHttpResponse(raw_response, &response)) {
        return CompleteBlizzardHttpRequest(
            callbacks, OsUrlDownloadCompletionCode::kFailure);
    }

    if (response.response.status_code >= 300
        && response.response.status_code < 400
        && response.redirect_location.has_value()) {
        if (!response.response.body.empty()) {
            constexpr std::string_view kRedirectAfterDataMessage =
                "ERROR: URL redirected after some data was received!\n";
            std::fwrite(kRedirectAfterDataMessage.data(), 1,
                        kRedirectAfterDataMessage.size(), stderr);
        }
        const std::string redirect =
            ResolveRedirectUrl(parsed_url, *response.redirect_location);
        if (redirect.empty()) {
            return CompleteBlizzardHttpRequest(
                callbacks, OsUrlDownloadCompletionCode::kFailure);
        }
        BlizzardHttpRequest redirected_request = request;
        redirected_request.url = redirect;
        return PerformBlizzardHttpRequestInternal(
            redirected_request, callbacks, redirect_depth + 1);
    }

    return ProcessBlizzardHttpResponse(response.response, callbacks);
}

bool DownloadUrlToStringWithResultImpl(const char* url,
                                       std::string* body,
                                       const std::uint32_t timeout_ms,
                                       OsUrlDownloadCompletionCode* completion_code,
                                       BlizzardHttpRequest options = {}) {
    if (!url || !body) {
        if (completion_code) {
            *completion_code = OsUrlDownloadCompletionCode::kFailure;
        }
        return false;
    }

    const std::uint32_t effective_timeout =
        timeout_ms != 0 ? timeout_ms : kDefaultTimeoutMs;
    if (auto& observer = UrlDownloadObserver(); observer) {
        observer(url, effective_timeout);
    }

    if (auto& handler = UrlDownloadResultHandler(); handler) {
        body->clear();
        const UrlDownloadTestResult result = handler(url, body);
        if (completion_code) {
            *completion_code = result.completion_code;
        }
        return result.success;
    }

    if (auto& handler = UrlDownloadHandler(); handler) {
        body->clear();
        const bool ok = handler(url, body);
        if (completion_code) {
            *completion_code = ok ? OsUrlDownloadCompletionCode::kSuccess
                                  : OsUrlDownloadCompletionCode::kFailure;
        }
        return ok;
    }

    options.url = url;
    options.timeout_ms = effective_timeout;
    std::string downloaded_body;
    BlizzardHttpCallbacks callbacks;
    callbacks.on_body = [&downloaded_body](
        const std::span<const std::uint8_t> bytes) {
        downloaded_body.assign(
            reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return true;
    };
    const BlizzardHttpResult result = PerformBlizzardHttpRequestInternal(
        options, callbacks, 0);
    if (completion_code) {
        *completion_code = result.completion_code;
    }
    if (result.success) {
        *body = std::move(downloaded_body);
    }
    return result.success;
}

}

OsUrlDownloadCompletionCode MapBlizzardHttpStatus(
    const int status_code) noexcept {
    if (status_code >= 200 && status_code <= 299) {
        return OsUrlDownloadCompletionCode::kSuccess;
    }
    switch (status_code) {
    case 400:
        return OsUrlDownloadCompletionCode::kBadRequest;
    case 304:
        return OsUrlDownloadCompletionCode::kNotModified;
    case 403:
        return OsUrlDownloadCompletionCode::kForbidden;
    case 404:
        return OsUrlDownloadCompletionCode::kNotFound;
    default:
        return OsUrlDownloadCompletionCode::kFailure;
    }
}

std::optional<std::string> SerializeBlizzardHttpRequest(
    const BlizzardHttpRequest& request) {
    ParsedHttpUrl parsed_url;
    if (!ParseHttpUrl(request.url, &parsed_url)) {
        return std::nullopt;
    }
    return BuildHttpRequest(parsed_url, request);
}

BlizzardHttpResult ProcessBlizzardHttpResponse(
    const BlizzardHttpResponse& response,
    const BlizzardHttpCallbacks& callbacks) {
    if (callbacks.on_status) {
        callbacks.on_status(response.status_code);
    }
    if (response.status_code < 200 || response.status_code > 299) {
        return CompleteBlizzardHttpRequest(
            callbacks, MapBlizzardHttpStatus(response.status_code));
    }

    if (callbacks.on_headers_received) {
        callbacks.on_headers_received();
    }
    for (const BlizzardHttpHeader& header : response.headers) {
        if (AsciiEqualsIgnoreCase(header.name, "Last-Modified")) {
            if (callbacks.on_last_modified) {
                callbacks.on_last_modified(header.value);
            }
        } else if (AsciiEqualsIgnoreCase(header.name, "Content-Length")) {
            if (callbacks.on_content_length) {
                callbacks.on_content_length(
                    ParseRetailSignedInt32(header.value));
            }
        }
    }

    if (callbacks.on_body) {
        const auto* const bytes = reinterpret_cast<const std::uint8_t*>(
            response.body.data());
        const bool accepted = callbacks.on_body(
            std::span<const std::uint8_t>(bytes, response.body.size()));
        if (callbacks.should_cancel && callbacks.should_cancel()) {
            return CompleteBlizzardHttpRequest(
                callbacks, OsUrlDownloadCompletionCode::kCancelled);
        }
        if (!accepted) {
            return CompleteBlizzardHttpRequest(
                callbacks, OsUrlDownloadCompletionCode::kBodyRejected);
        }
    }
    return CompleteBlizzardHttpRequest(
        callbacks, OsUrlDownloadCompletionCode::kSuccess);
}

BlizzardHttpResult PerformBlizzardHttpRequest(
    const BlizzardHttpRequest& request,
    const BlizzardHttpCallbacks& callbacks) {
    return PerformBlizzardHttpRequestInternal(request, callbacks, 0);
}

UrlDownloadRangeBuffer::UrlDownloadRangeBuffer(
    const std::uint32_t expected_byte_count) {
    ResetForDownload(expected_byte_count);
}

void UrlDownloadRangeBuffer::ResetForDownload(
    const std::uint32_t expected_byte_count) {

    std::string empty_body;
    body_.swap(empty_body);
    if (expected_byte_count != 0 && body_.capacity() < expected_byte_count) {
        body_.reserve(expected_byte_count);
    }

    completion_code_ = 0;
    copied_byte_count_ = 0;
    reached_window_end_ = false;

    std::lock_guard lock(completion_mutex_);
    completed_ = false;
    success_ = false;
}

void UrlDownloadRangeBuffer::SetInclusiveByteWindowParts(
    const std::uint32_t start_lo,
    const std::uint32_t start_hi,
    const std::uint32_t end_lo,
    const std::uint32_t end_hi) {
    range_start_ = ComposeOffset64(start_lo, start_hi);
    range_end_ = ComposeOffset64(end_lo, end_hi);
}

std::array<std::uint32_t, 4> UrlDownloadRangeBuffer::GetInclusiveByteWindowParts() const {
    return {Low32(range_start_), High32(range_start_),
            Low32(range_end_), High32(range_end_)};
}

void UrlDownloadRangeBuffer::SetCookiePair(
    const std::string_view cookie_name,
    const std::string_view cookie_value) {
    cookie_name_.assign(cookie_name);
    cookie_value_.assign(cookie_value);
}

std::pair<std::string_view, std::string_view> UrlDownloadRangeBuffer::GetCookiePair() const {
    return {cookie_name_, cookie_value_};
}

bool UrlDownloadRangeBuffer::AppendBodyChunk(const std::uint8_t* const bytes,
                                             const std::uint32_t byte_count) {
    const std::uint64_t window_length =
        range_end_ >= range_start_ ? (range_end_ - range_start_ + 1u) : 0u;
    if (copied_byte_count_ >= window_length) {
        reached_window_end_ = true;
    }

    if (byte_count == 0 || !bytes) {
        return true;
    }

    const std::uint64_t remaining_window =
        copied_byte_count_ < window_length ? (window_length - copied_byte_count_) : 0u;
    const std::uint32_t copied_now = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(remaining_window, byte_count));
    body_.append(reinterpret_cast<const char*>(bytes),
                 static_cast<std::size_t>(copied_now));
    copied_byte_count_ += copied_now;
    if (copied_now != byte_count) {
        reached_window_end_ = true;
    }

    return true;
}

void UrlDownloadRangeBuffer::MarkCompleted(
    const std::uint32_t completion_code) {
    {
        std::lock_guard lock(completion_mutex_);
        completion_code_ = completion_code;
        success_ = IsSuccessfulDownloadRangeCompletionCode(completion_code);
        completed_ = true;
    }
    completion_cv_.notify_all();
}

bool UrlDownloadRangeBuffer::Callback(void* const callback_data,
                                      const std::uint8_t* const bytes,
                                      const std::uint32_t byte_count,
                                      const std::uint32_t event_flag,
                                      const std::uint32_t completion_code) {
    auto* const buffer = static_cast<UrlDownloadRangeBuffer*>(callback_data);
    if (buffer == nullptr) {
        return false;
    }

    if (event_flag != 0) {
        buffer->MarkCompleted(completion_code);
        return true;
    }

    if (!buffer->AppendBodyChunk(bytes, byte_count)) {
        return false;
    }
    return !buffer->reached_window_end();
}

void UrlDownloadRangeBuffer::WaitForCompletion() {
    std::unique_lock lock(completion_mutex_);
    completion_cv_.wait(lock, [this]() { return completed_; });
}

bool UrlDownloadRangeBuffer::WaitForCompletionFor(
    const std::chrono::milliseconds timeout) {
    std::unique_lock lock(completion_mutex_);
    return completion_cv_.wait_for(lock, timeout, [this]() { return completed_; });
}

void OsUrlDownloadCallback(void* hInternet, uintptr_t dwContext,
                            uint32_t dwInternetStatus,
                            void* lpvStatusInformation,
                            uint32_t dwStatusInformationLength) {

    (void)hInternet; (void)dwContext; (void)dwInternetStatus;
    (void)lpvStatusInformation; (void)dwStatusInformationLength;
}

bool OsURLDownload_Start(const char* url,
                         const OsUrlDownloadCallbackFn callback_fn,
                         void* const callback_data,
                         const int timeout_ms) {
    if (!url || *url == '\0') {
        return false;
    }

    if (auto& handler = UrlDownloadHandler(); !handler) {
        ParsedHttpUrl parsed_url;
        if (!ParseHttpUrl(url, &parsed_url)) {
            return false;
        }
    }

    const std::string request_url(url);
    const std::uint32_t effective_timeout =
        timeout_ms > 0 ? static_cast<std::uint32_t>(timeout_ms) : kDefaultTimeoutMs;
    BlizzardHttpRequest request_options;
    if (callback_fn == &UrlDownloadRangeBuffer::Callback && callback_data != nullptr) {
        const auto* const range_buffer =
            static_cast<const UrlDownloadRangeBuffer*>(callback_data);
        const auto range_parts = range_buffer->GetInclusiveByteWindowParts();
        const std::uint64_t range_start_bits =
            ComposeOffset64(range_parts[0], range_parts[1]);
        const std::uint64_t range_end_bits =
            ComposeOffset64(range_parts[2], range_parts[3]);
        request_options.inclusive_range = std::pair{
            std::bit_cast<std::int64_t>(range_start_bits),
            std::bit_cast<std::int64_t>(range_end_bits)};
        const auto [cookie_name, cookie_value] = range_buffer->GetCookiePair();
        if (!cookie_name.empty()) {
            request_options.cookie = std::pair{
                std::string(cookie_name), std::string(cookie_value)};
        }
    }

    return openwow::core::StormThread::Instance().SubmitToSingletonWorkerPool(
        [request_url, effective_timeout, callback_fn, callback_data, request_options](void*) -> int {
            std::string body;
            OsUrlDownloadCompletionCode completion_code =
                OsUrlDownloadCompletionCode::kFailure;
            const bool ok = DownloadUrlToStringWithResultImpl(
                request_url.c_str(), &body, effective_timeout, &completion_code,
                request_options);
            if (ok && !body.empty()) {
                const bool accepted = InvokeUrlDownloadCallback(
                    callback_fn, callback_data,
                    reinterpret_cast<const std::uint8_t*>(body.data()),
                    static_cast<std::uint32_t>(body.size()), 0,
                    OsUrlDownloadCompletionCode::kSuccess);
                if (!accepted) {
                    completion_code = OsUrlDownloadCompletionCode::kBodyRejected;
                }
            }

            (void)InvokeUrlDownloadCallback(
                callback_fn, callback_data, nullptr, 0, 1, completion_code);
            return 0;
        });
}

bool DownloadUrlToString(const char* url,
                         std::string* body,
                         std::uint32_t timeout_ms) {
    return DownloadUrlToStringWithResult(url, body, timeout_ms, nullptr);
}

bool DownloadUrlToStringWithResult(const char* url,
                                   std::string* body,
                                   std::uint32_t timeout_ms,
                                   OsUrlDownloadCompletionCode* completion_code) {
    return DownloadUrlToStringWithResultImpl(
        url, body, timeout_ms, completion_code, BlizzardHttpRequest{});
}

void SetUrlDownloadHandlerForTests(
    std::function<bool(std::string_view, std::string*)> handler) {
    UrlDownloadHandler() = std::move(handler);
}

void SetUrlDownloadResultHandlerForTests(
    std::function<UrlDownloadTestResult(std::string_view, std::string*)> handler) {
    UrlDownloadResultHandler() = std::move(handler);
}

void SetUrlDownloadObserverForTests(
    std::function<void(std::string_view, std::uint32_t)> observer) {
    UrlDownloadObserver() = std::move(observer);
}

}
