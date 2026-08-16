#pragma once

#include <vector>

namespace openwow::ui {
struct AddonInfo;
}

namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::client {

class RealmAddonHandshakeComposition {
 public:
  RealmAddonHandshakeComposition();
  ~RealmAddonHandshakeComposition();

  RealmAddonHandshakeComposition(const RealmAddonHandshakeComposition&) = delete;
  RealmAddonHandshakeComposition& operator=(
      const RealmAddonHandshakeComposition&) = delete;

  static void PublishClientAddons(const std::vector<openwow::ui::AddonInfo>& addons);

  static void BindContentVfs(const openwow::vfs::VirtualFileSystem* vfs);
};

}
