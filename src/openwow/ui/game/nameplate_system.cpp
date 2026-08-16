#include "openwow/ui/game/nameplate_system.h"

#include <algorithm>
#include <atomic>
#include <utility>

namespace openwow::ui {

NameplateSystem::NameplateSystem()
    : snapshot_(std::make_shared<const Snapshot>()) {}

NameplateSystem::SnapshotLease NameplateSystem::AcquireSnapshot() const noexcept {
  return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
}

std::size_t NameplateSystem::GetNumNameplates() const noexcept {
  return AcquireSnapshot()->size();
}

void NameplateSystem::PublishLocked(Snapshot next) {
  auto immutable = std::make_shared<const Snapshot>(std::move(next));
  std::atomic_store_explicit(&snapshot_, std::move(immutable),
                             std::memory_order_release);
}

void NameplateSystem::ReplaceSnapshot(Snapshot nameplates) {
  std::lock_guard lock(writer_mutex_);
  PublishLocked(std::move(nameplates));
}

void NameplateSystem::RemoveNameplate(const std::uint64_t guid) {
  std::lock_guard lock(writer_mutex_);
  Snapshot next = *AcquireSnapshot();
  std::erase_if(next, [guid](const NameplateInfo& plate) {
    return plate.guid == guid;
  });
  PublishLocked(std::move(next));
}

void NameplateSystem::ClearAll() {
  std::lock_guard lock(writer_mutex_);
  PublishLocked({});
}

NameplateFrameChannel::NameplateFrameChannel()
    : layout_(std::make_shared<const NameplateScreenLayout>()) {}

NameplateFrameChannel& NameplateFrameChannel::Get() {
  static NameplateFrameChannel instance;
  return instance;
}

void NameplateFrameChannel::PublishLayout(NameplateScreenLayout layout) {
  auto immutable =
      std::make_shared<const NameplateScreenLayout>(std::move(layout));
  std::lock_guard lock(writer_mutex_);
  std::atomic_store_explicit(&layout_, std::move(immutable),
                             std::memory_order_release);
}

NameplateFrameChannel::LayoutLease NameplateFrameChannel::AcquireLayout()
    const noexcept {
  return std::atomic_load_explicit(&layout_, std::memory_order_acquire);
}

void NameplateFrameChannel::RecordWidgetUpdate(
    const Evidence evidence) noexcept {
  std::lock_guard lock(evidence_mutex_);
  evidence_ = evidence;
}

NameplateFrameChannel::Evidence NameplateFrameChannel::widget_evidence()
    const noexcept {
  std::lock_guard lock(evidence_mutex_);
  return evidence_;
}

void NameplateFrameChannel::Reset() {
  PublishLayout({});
  RecordWidgetUpdate({});
}

}
