#pragma once

#include <memory>

namespace openwow::vfs {

class RuntimeFile;

class RuntimeFileRegistry {
public:
  RuntimeFileRegistry();
  ~RuntimeFileRegistry();
  RuntimeFileRegistry(const RuntimeFileRegistry &) = delete;
  RuntimeFileRegistry &operator=(const RuntimeFileRegistry &) = delete;

  int Store(std::shared_ptr<RuntimeFile> file);
  [[nodiscard]] std::shared_ptr<RuntimeFile> LookupRetained(int handle) const;
  bool Remove(int handle);
  void ResetForTests();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

RuntimeFileRegistry &RetailRuntimeFileRegistry();

}
