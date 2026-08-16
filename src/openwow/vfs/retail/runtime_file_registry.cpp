#include "openwow/vfs/retail/runtime_file_registry.h"

#include "openwow/vfs/retail/runtime_file.h"

#include <mutex>
#include <unordered_map>

namespace openwow::vfs {

class RuntimeFileRegistry::Impl {
public:
  mutable std::mutex mutex;
  std::unordered_map<int, std::shared_ptr<RuntimeFile>> files;
  int next_handle = 1;
};

RuntimeFileRegistry::RuntimeFileRegistry() : impl_(std::make_unique<Impl>()) {}
RuntimeFileRegistry::~RuntimeFileRegistry() = default;

int RuntimeFileRegistry::Store(std::shared_ptr<RuntimeFile> file) {
  std::lock_guard lock(impl_->mutex);
  const int handle = impl_->next_handle++;
  file->handle_id = handle;
  impl_->files.emplace(handle, std::move(file));
  return handle;
}

std::shared_ptr<RuntimeFile> RuntimeFileRegistry::LookupRetained(const int handle) const {
  std::lock_guard lock(impl_->mutex);
  const auto it = impl_->files.find(handle);
  return it == impl_->files.end() ? std::shared_ptr<RuntimeFile>{} : it->second;
}

bool RuntimeFileRegistry::Remove(const int handle) {
  if (handle == 0) {
    return false;
  }

  std::shared_ptr<RuntimeFile> removed;
  {
    std::lock_guard lock(impl_->mutex);
    const auto it = impl_->files.find(handle);
    if (it == impl_->files.end()) {
      return false;
    }
    removed = std::move(it->second);
    impl_->files.erase(it);
  }
  return true;
}

void RuntimeFileRegistry::ResetForTests() {
  std::unordered_map<int, std::shared_ptr<RuntimeFile>> removed;
  {
    std::lock_guard lock(impl_->mutex);
    removed.swap(impl_->files);
    impl_->next_handle = 1;
  }
}

RuntimeFileRegistry &RetailRuntimeFileRegistry() {
  static RuntimeFileRegistry registry;
  return registry;
}

}
