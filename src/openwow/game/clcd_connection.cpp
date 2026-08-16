
#include "openwow/game/clcd_connection.h"

#include "openwow/game/cez_lcd_page_update.h"
#include "openwow/core/storm_string.h"

namespace openwow::game {

static constexpr const char* kEventNodeAllocTag =
    ".?AUCB_EVENT_NODE@CLCDConnection@@";

void CLCDConnectionEventSentinel::Initialize() {
    auto* self = AsSentinelNode();
    first = self;

    last = self;
}

bool CLCDConnectionEventSentinel::IsEmpty() const {
    return first == AsSentinelNode();
}

CLCDConnectionEventNode* CLCDConnectionEventSentinel::AsSentinelNode() {
    return reinterpret_cast<CLCDConnectionEventNode*>(this);
}

const CLCDConnectionEventNode*
CLCDConnectionEventSentinel::AsSentinelNode() const {
    return reinterpret_cast<const CLCDConnectionEventNode*>(this);
}

CLCDConnection::CLCDConnection() {
    event_cs_.Initialize();
    event_list_.Initialize();
}

CLCDConnection::~CLCDConnection() {
    event_cs_.Delete();
}

void CLCDConnection::OnEventQueued() {
}

void CLCDConnection::ReleaseMonochromePage() {
    if (mono_page_) {
        CEzLcdPage_Destroy(mono_page_);
        mono_page_ = nullptr;
    }
}

void CLCDConnection::ReleaseColorPage() {
    if (color_page_) {
        CEzLcdPage_Destroy(color_page_);
        color_page_ = nullptr;
    }
}

platform::StormCriticalSection& CLCDConnection::GetEventCriticalSection() {
    return event_cs_;
}

CLCDConnectionEventSentinel& CLCDConnection::GetEventList() {
    return event_list_;
}

const CLCDConnectionEventSentinel& CLCDConnection::GetEventList() const {
    return event_list_;
}

void InsertEventNodeAtHead(CLCDConnectionEventSentinel& sentinel,
                           CLCDConnectionEventNode* node) {
    CLCDConnectionEventNode* old_head = sentinel.first;
    node->next = old_head;
    node->prev = old_head->prev;
    old_head->prev = node;
    sentinel.first = node;
}

int CLCDConnection_CB_EVENT_NODE_Method(
    CLCDConnection* connection,
    std::uint32_t param1,
    std::uint32_t param2,
    std::uint32_t param3) {

    auto& cs = connection->GetEventCriticalSection();
    auto& event_list = connection->GetEventList();

    cs.Enter();

    auto* node = static_cast<CLCDConnectionEventNode*>(
        core::SMemAlloc(sizeof(CLCDConnectionEventNode),
                         kEventNodeAllocTag, -2, 8));

    node->next = nullptr;
    node->prev = nullptr;

    InsertEventNodeAtHead(event_list, node);

    node->event_type = CLCDConnectionEventType::kMethod;
    node->param1 = param1;
    node->param2 = param2;
    node->param3 = param3;

    cs.Leave();

    connection->OnEventQueued();

    return 0;
}

}
