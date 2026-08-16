#pragma once

#include "openwow/ui/glue/glue_lua_value.h"
#include "openwow/ui/lua_run_result.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::glue {

class GlueLuaEventTrace {
 public:
  void RecordWidgetEvent(const std::string& widget_name,
                         const std::string& event_name,
                         const std::string& event_source,
                         const std::vector<GlueLuaValue>& args,
                         const LuaRunResult& result) {
    const std::string kind = (event_name == "OnEvent") ? "frame_event" : "widget_event";
    std::string line;
    line.reserve(128);
    line += std::to_string(next_index_++);
    line += '\t';
    AppendEscaped(line, kind);
    line += '\t';
    AppendEscaped(line, widget_name);
    line += '\t';
    AppendEscaped(line, event_name);
    line += '\t';
    AppendEscaped(line, event_source);
    line += '\t';
    AppendEscaped(line, SummarizeArgs(args));
    line += '\t';
    line += result.ok ? "1" : "0";
    line += '\t';
    AppendEscaped(line, result.ok ? std::string() : SanitizeError(result.error));
    line += '\n';
    lines_.push_back(std::move(line));
  }

  [[nodiscard]] std::string SerializeTsv() const {
    std::string out;
    out.reserve(64 + lines_.size() * 128);
    out.append("idx\tkind\twidget\tevent\tsource\targs\tok\terror\n");
    for (const auto& line : lines_) {
      out.append(line);
    }
    return out;
  }

  [[nodiscard]] bool WriteTsvFile(const std::filesystem::path& path,
                                  std::string* error = nullptr) const {
    std::error_code ec;
    const auto dir = path.parent_path();
    if (!dir.empty()) {
      std::filesystem::create_directories(dir, ec);
      if (ec) {
        if (error) *error = ec.message();
        return false;
      }
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
      if (error) *error = "failed to open";
      return false;
    }
    const std::string payload = SerializeTsv();
    f.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    if (!f.good()) {
      if (error) *error = "failed to write";
      return false;
    }
    return true;
  }

 private:
  static void AppendEscaped(std::string& out, const std::string& s) {
    for (const char ch : s) {
      switch (ch) {
        case '\\':
          out += "\\\\";
          break;
        case '\t':
          out += "\\t";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        default:
          out += ch;
          break;
      }
    }
  }

  static std::string EscapeForArgSummary(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char ch : s) {
      switch (ch) {
        case '\\':
          out += "\\\\";
          break;
        case '"':
          out += "\\\"";
          break;
        case '\t':
          out += "\\t";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        default:
          out += ch;
          break;
      }
    }
    return out;
  }

  static std::string SummarizeArgs(const std::vector<GlueLuaValue>& args) {
    std::string out;
    out.push_back('[');
    for (std::size_t i = 0; i < args.size(); ++i) {
      if (i != 0) out.push_back(',');
      const auto& v = args[i];
      switch (v.kind) {
        case GlueLuaValue::Kind::kNil:
          out.append("nil");
          break;
        case GlueLuaValue::Kind::kBoolean:
          out.append(v.bool_value ? "b:1" : "b:0");
          break;
        case GlueLuaValue::Kind::kNumber:

          out.append("n");
          break;
        case GlueLuaValue::Kind::kString: {
          std::string_view sv(v.string_value);
          constexpr std::size_t kMaxLen = 96;
          if (sv.size() > kMaxLen) {
            sv = sv.substr(0, kMaxLen);
          }
          out.append("s:\"");
          out.append(EscapeForArgSummary(sv));
          if (v.string_value.size() > kMaxLen) {
            out.append("…");
          }
          out.push_back('"');
          break;
        }
      }
    }
    out.push_back(']');
    return out;
  }

  static std::string SanitizeError(std::string error) {
    static const std::regex kHexPtr(R"(0x[0-9a-fA-F]+)");
    error = std::regex_replace(error, kHexPtr, "0xPTR");
    return error;
  }

  std::vector<std::string> lines_;
  std::uint64_t next_index_{0};
};

}
