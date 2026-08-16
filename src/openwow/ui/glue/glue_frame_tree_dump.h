#pragma once

#include "openwow/ui/glue/glue_widget_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace openwow::ui::glue {

class GlueFrameTreeDump {
 public:
  [[nodiscard]] std::string SerializeTsv(const GlueWidgetRuntime& widgets) const {
    const std::vector<std::string> order = StableWidgetOrder(widgets);
    std::unordered_set<std::string> present;
    present.reserve(order.size());
    for (const auto& n : order) {
      present.insert(n);
    }

    std::vector<std::string> roots;
    roots.reserve(order.size());
    std::vector<std::pair<std::string, std::string>> parent_by_name;
    parent_by_name.reserve(order.size());
    for (const auto& name : order) {
      const auto w = widgets.GetWidget(name);
      if (!w.has_value()) continue;
      parent_by_name.emplace_back(name, w->parent);
      if (w->parent.empty() || !present.contains(w->parent)) {
        roots.push_back(name);
      }
    }

    std::vector<std::pair<std::string, std::string>> edges;
    edges.reserve(order.size());
    for (const auto& [name, parent] : parent_by_name) {
      if (parent.empty()) continue;
      if (!present.contains(parent)) continue;
      edges.emplace_back(parent, name);
    }

    std::vector<std::string> parents;
    parents.reserve(order.size());
    for (const auto& name : order) {
      parents.push_back(name);
    }

    struct ParentChildren {
      std::string parent;
      std::vector<std::string> children;
    };
    std::vector<ParentChildren> child_lists;
    child_lists.reserve(order.size());
    for (const auto& p : parents) {
      child_lists.push_back(ParentChildren{p, {}});
    }
    auto find_children = [&](const std::string& parent) -> std::vector<std::string>* {
      for (auto& pc : child_lists) {
        if (pc.parent == parent) return &pc.children;
      }
      return nullptr;
    };
    for (const auto& [parent, child] : edges) {
      if (auto* v = find_children(parent); v != nullptr) {
        v->push_back(child);
      }
    }

    std::string out;
    out.reserve(256 + order.size() * 128);
    out.append("idx\tdepth\tname\tkind\tparent\tvisible\tvirtual\tx\ty\tw\th\tanchors\n");

    std::uint64_t idx = 0;
    const auto emit = [&](const std::string& name, int depth, auto&& self) -> void {
      const auto w = widgets.GetWidget(name);
      if (!w.has_value()) return;

      out.append(std::to_string(idx++));
      out.push_back('\t');
      out.append(std::to_string(depth));
      out.push_back('\t');
      AppendEscaped(out, w->name);
      out.push_back('\t');
      AppendEscaped(out, w->kind);
      out.push_back('\t');
      AppendEscaped(out, w->parent);
      out.push_back('\t');
      out.append(widgets.IsVisible(w->name) ? "1" : "0");
      out.push_back('\t');
      out.append(w->virtual_template ? "1" : "0");
      out.push_back('\t');
      out.append(std::to_string(w->x));
      out.push_back('\t');
      out.append(std::to_string(w->y));
      out.push_back('\t');
      out.append(std::to_string(w->width));
      out.push_back('\t');
      out.append(std::to_string(w->height));
      out.push_back('\t');
      AppendEscaped(out, SummarizeAnchors(widgets, w->name));
      out.push_back('\n');

      if (auto* children = find_children(name); children != nullptr) {
        for (const auto& child : *children) {
          self(child, depth + 1, self);
        }
      }
    };
    for (const auto& root : roots) {
      emit(root, 0, emit);
    }
    return out;
  }

  [[nodiscard]] bool WriteTsvFile(const std::filesystem::path& path,
                                  const GlueWidgetRuntime& widgets,
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
    const std::string payload = SerializeTsv(widgets);
    f.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    if (!f.good()) {
      if (error) *error = "failed to write";
      return false;
    }
    return true;
  }

 private:
  static void AppendEscaped(std::string& out, std::string_view s) {
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

  static int PointCode(std::string_view point) {

    if (point == "TOPLEFT") return 0;
    if (point == "TOP") return 1;
    if (point == "TOPRIGHT") return 2;
    if (point == "LEFT") return 3;
    if (point == "CENTER") return 4;
    if (point == "RIGHT") return 5;
    if (point == "BOTTOMLEFT") return 6;
    if (point == "BOTTOM") return 7;
    if (point == "BOTTOMRIGHT") return 8;
    return 9;
  }

  static std::string FormatFloat(float v) {
    if (std::abs(v) < 1e-6f) {
      v = 0.0f;
    }
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss.setf(std::ios::fixed);
    ss << std::setprecision(3) << v;
    return ss.str();
  }

  static std::vector<std::string> StableWidgetOrder(const GlueWidgetRuntime& widgets) {
    auto order = widgets.WidgetNamesInSourceOrder();
    if (order.empty()) {
      order = widgets.WidgetNames();
      std::sort(order.begin(), order.end());
    }
    return order;
  }

  static std::string SummarizeAnchors(const GlueWidgetRuntime& widgets,
                                      const std::string& name) {
    std::vector<openwow::ui::framexml::UiAnchor> anchors;
    const int n = widgets.GetNumPoints(name);
    anchors.reserve(static_cast<std::size_t>(std::max(0, n)));

    for (int i = 1; i <= n; ++i) {
      const auto a = widgets.GetPoint(name, i);
      if (a.has_value()) {
        anchors.push_back(*a);
      }
    }
    std::sort(anchors.begin(), anchors.end(),
              [](const auto& a, const auto& b) {
                const int ac = PointCode(a.point);
                const int bc = PointCode(b.point);
                if (ac != bc) return ac < bc;
                if (a.relative_to != b.relative_to) return a.relative_to < b.relative_to;
                if (a.relative_point != b.relative_point) return a.relative_point < b.relative_point;
                if (a.x != b.x) return a.x < b.x;
                return a.y < b.y;
              });

    std::string out;
    for (std::size_t i = 0; i < anchors.size(); ++i) {
      if (i != 0) out.push_back(';');
      const auto& a = anchors[i];
      out.append(a.point);
      out.push_back('@');
      out.append(a.relative_to);
      out.push_back('@');
      out.append(a.relative_point);
      out.push_back('@');
      out.append(FormatFloat(a.x));
      out.push_back('@');
      out.append(FormatFloat(a.y));
    }
    return out;
  }
};

}
