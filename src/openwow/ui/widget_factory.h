#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui {

enum class WidgetType : uint8_t {
  Frame,
  Button,
  EditBox,
  ScrollFrame,
  GameTooltip,
  StatusBar,
  Slider,
  CheckButton,
  ColorSelect,
  DressUpModel,
  MessageFrame,
  Minimap,
  Model,
  ScrollingMessageFrame,
  SimpleHTML,
  Cooldown,
  MovieFrame,
  COUNT_
};

struct WidgetCreateInfo {
  WidgetType type{WidgetType::Frame};
  std::string name;
  std::string parent;
  std::string inherits;
  uint32_t id{0};
  bool isVirtual{false};
};

class WidgetFactory {
 public:
  WidgetFactory();
  ~WidgetFactory() = default;

  void RegisterType(WidgetType type, const std::string& typeName);

  uint32_t CreateWidget(const WidgetCreateInfo& info);

  bool DestroyWidget(uint32_t id);

  [[nodiscard]] std::optional<WidgetCreateInfo> GetWidget(uint32_t id) const;

  [[nodiscard]] std::optional<uint32_t> GetWidgetByName(
      const std::string& name) const;

  [[nodiscard]] std::size_t GetWidgetCount() const;

  [[nodiscard]] std::vector<uint32_t> GetWidgetsByType(WidgetType type) const;

  [[nodiscard]] std::string GetTypeName(WidgetType type) const;

  [[nodiscard]] std::optional<WidgetType> GetTypeByName(
      const std::string& name) const;

  [[nodiscard]] std::vector<std::string> GetAllNames() const;

  void Reset();

 private:
  uint32_t nextId_{1};
  std::unordered_map<uint32_t, WidgetCreateInfo> widgets_;
  std::unordered_map<std::string, uint32_t> nameIndex_;
  std::unordered_map<WidgetType, std::string> typeNames_;
};

}
