
#include "openwow/ui/widgets/backdrop.h"

namespace openwow::ui::widgets {

void CBackdrop::CreateTextureElements(void* parent_frame) {
  if (!parent_frame) return;

  if (!bg_file_.empty()) {

  }

  if (!edge_file_.empty()) {

    if (edge_mask_ & 0x01) {

    }

    if (edge_mask_ & 0x02) {

    }

    if (edge_mask_ & 0x04) {

    }

    if (edge_mask_ & 0x08) {

    }

    if (edge_mask_ & 0x10) {

    }

    if (edge_mask_ & 0x20) {

    }

    if (edge_mask_ & 0x40) {

    }

    if (edge_mask_ & 0x80) {

    }
  }

  created_ = true;
}

}
