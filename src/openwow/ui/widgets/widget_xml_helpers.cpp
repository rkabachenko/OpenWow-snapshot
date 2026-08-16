
#include "openwow/ui/widgets/widget_xml_helpers.h"

#include "openwow/core/storm_error.h"
#include "openwow/ui/widgets/script_region.h"
#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/ui/xml/frame_xml_parser.h"
#include "openwow/ui/xml/xml_value_helpers.h"
#include "openwow/foundation/text/ascii.h"

#include <string>

namespace openwow::ui::widgets {

namespace {

const char *FindNodeAttributeValue(const openwow::ui::xml::XMLNode &node,
                                   const char *name) {
  if (name == nullptr || *name == '\0') {
    return nullptr;
  }

  const std::string key(name);
  auto it = node.attributes.find(key);
  if (it != node.attributes.end()) {
    return it->second.empty() ? nullptr : it->second.c_str();
  }

  const auto lower_key = openwow::text::ToLowerAscii(key);
  for (const auto &[candidate, value] : node.attributes) {
    if (openwow::text::ToLowerAscii(candidate) == lower_key) {
      return value.empty() ? nullptr : value.c_str();
    }
  }

  return nullptr;
}

bool TryParseFramePointName(const char *value, FramePoint *out_point) {
  if (value == nullptr || out_point == nullptr) {
    return false;
  }

  if (openwow::text::EqualsIgnoreCaseAscii(value, "TOPLEFT")) {
    *out_point = FramePoint::TopLeft;
    return true;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(value, "TOP")) {
    *out_point = FramePoint::Top;
    return true;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(value, "TOPRIGHT")) {
    *out_point = FramePoint::TopRight;
    return true;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(value, "LEFT")) {
    *out_point = FramePoint::Left;
    return true;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(value, "CENTER")) {
    *out_point = FramePoint::Center;
    return true;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(value, "RIGHT")) {
    *out_point = FramePoint::Right;
    return true;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(value, "BOTTOMLEFT")) {
    *out_point = FramePoint::BottomLeft;
    return true;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(value, "BOTTOM")) {
    *out_point = FramePoint::Bottom;
    return true;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(value, "BOTTOMRIGHT")) {
    *out_point = FramePoint::BottomRight;
    return true;
  }

  return false;
}

}

const char *FindAttributeValue(const openwow::ui::xml::XMLFrameDef &frame_def,
                               const char *name) {
  const char *value = FindNodeAttributeValue(frame_def.raw_node, name);
  if (value != nullptr) {
    return value;
  }

  if (name == nullptr || *name == '\0') {
    return nullptr;
  }

  const std::string key(name);
  auto it = frame_def.attributes.find(key);
  if (it != frame_def.attributes.end()) {
    return it->second.empty() ? nullptr : it->second.c_str();
  }

  const auto lower_key = openwow::text::ToLowerAscii(key);
  for (const auto &[candidate, candidate_value] : frame_def.attributes) {
    if (openwow::text::ToLowerAscii(candidate) == lower_key) {
      return candidate_value.empty() ? nullptr : candidate_value.c_str();
    }
  }

  return nullptr;
}

void LoadRegionLayoutFromXML(CScriptRegion &region,
                             const openwow::ui::xml::XMLFrameDef &frame_def,
                             openwow::ui::xml::ErrorContext *error_handler) {
  const auto *size_node = frame_def.raw_node.FindChild("Size");
  if (size_node != nullptr) {
    float width = 0.0f;
    float height = 0.0f;
    if (openwow::ui::xml::RelDimension_ref(size_node, &width, &height,
                                           error_handler) != 0) {
      if (auto *frame = dynamic_cast<CSimpleFrame *>(&region); frame != nullptr) {
        frame->SetLayoutWidth(width);
        frame->SetLayoutHeight(height);
      } else {
        region.SetWidth(width);
        region.SetHeight(height);
      }
    }
  }

  CScriptRegion *const default_relative = region.GetParent();
  const char *set_all_points = FindAttributeValue(frame_def, "setAllPoints");
  const bool wants_all_points =
      set_all_points != nullptr &&
      openwow::text::EqualsIgnoreCaseAscii(set_all_points, "true");

  const auto *anchors_node = frame_def.raw_node.FindChild("Anchors");
  if (anchors_node != nullptr) {
    if (wants_all_points && error_handler != nullptr) {
      error_handler->ReportError(
          "SETALLPOINTS set to true in frame with anchors (ignored)");
    }

    for (const auto *anchor_node : anchors_node->FindChildren("Anchor")) {
      if (anchor_node == nullptr) {
        continue;
      }

      const char *point_name = FindNodeAttributeValue(*anchor_node, "point");
      FramePoint point = FramePoint::TopLeft;
      if (point_name == nullptr || !TryParseFramePointName(point_name, &point)) {
        if (error_handler != nullptr) {
          error_handler->ReportError("Invalid anchor point in frame: %s",
                                     point_name != nullptr ? point_name : "");
        }
        continue;
      }

      FramePoint relative_point = point;
      if (const char *relative_point_name =
              FindNodeAttributeValue(*anchor_node, "relativePoint");
          relative_point_name != nullptr && *relative_point_name != '\0') {
        if (!TryParseFramePointName(relative_point_name, &relative_point)) {
          if (error_handler != nullptr) {
            error_handler->ReportError("Invalid anchor point in frame: %s",
                                       relative_point_name);
          }
          continue;
        }
      }

      CScriptRegion *relative = default_relative;
      if (const char *relative_name = FindNodeAttributeValue(*anchor_node, "relativeTo");
          relative_name != nullptr && *relative_name != '\0') {
        relative = CSimpleFrame::FindNamedFrame(relative_name);
        if (relative == nullptr) {
          if (error_handler != nullptr) {
            error_handler->ReportError("Couldn't find relative frame: %s",
                                       relative_name);
          }
          continue;
        }
        if (relative == &region) {
          if (error_handler != nullptr) {
            error_handler->ReportError("Frame anchored to itself: %s",
                                       relative_name);
          }
          continue;
        }
      }

      if (relative == nullptr) {
        openwow::core::SErrSetLastError(87);
        continue;
      }

      float x_offset = 0.0f;
      float y_offset = 0.0f;
      if (const auto *offset_node = anchor_node->FindChild("Offset");
          offset_node != nullptr) {
        openwow::ui::xml::RelDimension_ref(offset_node, &x_offset, &y_offset,
                                           error_handler);
      } else {
        openwow::ui::xml::RelDimension_ref(anchor_node, &x_offset, &y_offset,
                                           error_handler);
      }

      RegionAnchor anchor;
      anchor.point = point;
      anchor.relativeTo = relative;
      anchor.relativePoint = relative_point;
      anchor.offsetX = x_offset;
      anchor.offsetY = y_offset;
      region.SetPoint(anchor);
    }
    return;
  }

  if (wants_all_points && default_relative != nullptr) {
    region.SetAllPoints(default_relative);
  }
}

}
