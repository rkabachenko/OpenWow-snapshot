#include "openwow/ui/widget_factory.h"

#include <array>
#include <utility>

namespace openwow::ui {

namespace {

struct WidgetTypeMeta {
  WidgetType type;
  const char* name;
};

constexpr std::array kWidgetTypeMeta = {
    WidgetTypeMeta{WidgetType::Frame, "Frame"},
    WidgetTypeMeta{WidgetType::Button, "Button"},
    WidgetTypeMeta{WidgetType::EditBox, "EditBox"},
    WidgetTypeMeta{WidgetType::ScrollFrame, "ScrollFrame"},
    WidgetTypeMeta{WidgetType::GameTooltip, "GameTooltip"},
    WidgetTypeMeta{WidgetType::StatusBar, "StatusBar"},
    WidgetTypeMeta{WidgetType::Slider, "Slider"},
    WidgetTypeMeta{WidgetType::CheckButton, "CheckButton"},
    WidgetTypeMeta{WidgetType::ColorSelect, "ColorSelect"},
    WidgetTypeMeta{WidgetType::DressUpModel, "DressUpModel"},
    WidgetTypeMeta{WidgetType::MessageFrame, "MessageFrame"},
    WidgetTypeMeta{WidgetType::Minimap, "Minimap"},
    WidgetTypeMeta{WidgetType::Model, "Model"},
    WidgetTypeMeta{WidgetType::ScrollingMessageFrame, "ScrollingMessageFrame"},
    WidgetTypeMeta{WidgetType::SimpleHTML, "SimpleHTML"},
    WidgetTypeMeta{WidgetType::Cooldown, "Cooldown"},
    WidgetTypeMeta{WidgetType::MovieFrame, "MovieFrame"},
};

}

WidgetFactory::WidgetFactory() {
  for (const auto& m : kWidgetTypeMeta) {
    typeNames_[m.type] = m.name;
  }
}

void WidgetFactory::RegisterType(WidgetType type, const std::string& typeName) {
  typeNames_[type] = typeName;
}

uint32_t WidgetFactory::CreateWidget(const WidgetCreateInfo& info) {

  if (!info.name.empty() && nameIndex_.count(info.name)) return 0;

  uint32_t id = nextId_++;
  widgets_[id] = info;
  if (!info.name.empty()) nameIndex_[info.name] = id;
  return id;
}

bool WidgetFactory::DestroyWidget(uint32_t id) {
  auto it = widgets_.find(id);
  if (it == widgets_.end()) return false;
  if (!it->second.name.empty()) nameIndex_.erase(it->second.name);
  widgets_.erase(it);
  return true;
}

std::optional<WidgetCreateInfo> WidgetFactory::GetWidget(uint32_t id) const {
  auto it = widgets_.find(id);
  if (it == widgets_.end()) return std::nullopt;
  return it->second;
}

std::optional<uint32_t> WidgetFactory::GetWidgetByName(
    const std::string& name) const {
  auto it = nameIndex_.find(name);
  if (it == nameIndex_.end()) return std::nullopt;
  return it->second;
}

std::size_t WidgetFactory::GetWidgetCount() const { return widgets_.size(); }

std::vector<uint32_t> WidgetFactory::GetWidgetsByType(WidgetType type) const {
  std::vector<uint32_t> result;
  for (const auto& [id, info] : widgets_) {
    if (info.type == type) result.push_back(id);
  }
  return result;
}

std::string WidgetFactory::GetTypeName(WidgetType type) const {
  auto it = typeNames_.find(type);
  if (it != typeNames_.end()) return it->second;
  return "Unknown";
}

std::optional<WidgetType> WidgetFactory::GetTypeByName(
    const std::string& name) const {
  for (const auto& [t, n] : typeNames_) {
    if (n == name) return t;
  }
  return std::nullopt;
}

std::vector<std::string> WidgetFactory::GetAllNames() const {
  std::vector<std::string> result;
  result.reserve(nameIndex_.size());
  for (const auto& [name, _] : nameIndex_) result.push_back(name);
  return result;
}

void WidgetFactory::Reset() {
  widgets_.clear();
  nameIndex_.clear();
  nextId_ = 1;

  typeNames_.clear();
  for (const auto& m : kWidgetTypeMeta) {
    typeNames_[m.type] = m.name;
  }
}

}
