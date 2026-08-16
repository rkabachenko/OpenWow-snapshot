
#include "openwow/ui/widgets/csimple_message_scroll_frame_display_node.h"

namespace openwow::ui::widgets {

class CSimpleFontStringLine : public CSimpleFontString {
public:
    CSimpleFontStringLine() : CSimpleFontString() {}

    void AddRef() noexcept { ++refCount_; }
    void Release() noexcept {
        if (--refCount_ == 0) {
            delete this;
        }
    }

    [[nodiscard]] uint32_t GetRefCount() const noexcept { return refCount_; }

private:

    uint32_t refCount_{0};
};

CSimpleMessageScrollFrameDisplayNode::CSimpleMessageScrollFrameDisplayNode()
    : fontString_(new CSimpleFontStringLine()),
      messageLine_(nullptr) {
    fontString_->AddRef();
}

CSimpleMessageScrollFrameDisplayNode::CSimpleMessageScrollFrameDisplayNode(
    const CSimpleMessageScrollFrameDisplayNode& other)
    : fontString_(other.fontString_),
      messageLine_(other.messageLine_) {
    if (fontString_) {
        fontString_->AddRef();
    }
}

CSimpleMessageScrollFrameDisplayNode::CSimpleMessageScrollFrameDisplayNode(
    CSimpleMessageScrollFrameDisplayNode&& other) noexcept
    : fontString_(other.fontString_),
      messageLine_(other.messageLine_) {
    other.fontString_ = nullptr;
    other.messageLine_ = nullptr;
}

CSimpleMessageScrollFrameDisplayNode::~CSimpleMessageScrollFrameDisplayNode() {
    if (fontString_) {
        fontString_->Release();
        fontString_ = nullptr;
    }
    messageLine_ = nullptr;
}

}
