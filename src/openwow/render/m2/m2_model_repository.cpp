#include "openwow/render/m2/m2_model_repository.h"

#include "openwow/foundation/text/ascii.h"

#include <algorithm>

namespace openwow::render::m2 {

M2ModelRepository::M2ModelRepository() {

  models_.rehash(kM2ModelBucketFloor);
  model_paths_.rehash(kM2ModelBucketFloor);
}

M2ModelIdentity M2ModelRepository::Canonicalize(const std::string& raw_path) {
  std::string path = raw_path;
  std::replace(path.begin(), path.end(), '\\', '/');
  while (!path.empty() && (path.front() == '/' || path.back() == '\0')) {
    if (path.front() == '/') {
      path.erase(path.begin());
    } else {
      path.pop_back();
    }
  }
  const std::size_t slash = path.find_last_of('/');
  const std::size_t dot = path.find_last_of('.');
  if (dot == std::string::npos ||
      (slash != std::string::npos && dot < slash)) {
    return {};
  }
  const std::string extension = text::ToLowerAscii(path.substr(dot));
  if (extension != ".m2" && extension != ".mdl" && extension != ".mdx") {
    return {};
  }
  path.resize(dot);
  return {
      .valid = true,
      .load_path = path + ".m2",
      .cache_key = text::ToLowerAscii(path),
  };
}

M2ModelIdentity M2ModelRepository::ReserveIdentity(const std::string& path) {
  M2ModelIdentity identity = Canonicalize(path);
  if (!identity.valid) {
    return identity;
  }
  std::lock_guard lock(mutex_);
  if (const auto found = model_paths_.find(identity.cache_key);
      found != model_paths_.end()) {
    identity.model_id = found->second;
    return identity;
  }
  identity.model_id = next_model_id_++;
  identity.created = true;
  model_paths_.emplace(identity.cache_key, identity.model_id);
  return identity;
}

bool M2ModelRepository::IsRenderReady(const std::uint32_t model_id) const {
  std::lock_guard lock(mutex_);
  const auto found = models_.find(model_id);
  return found != models_.end() && found->second->IsReadyForRender();
}

void M2ModelRepository::ForgetUnpublishedIdentity(
    const std::uint32_t model_id) {
  std::lock_guard lock(mutex_);
  if (models_.contains(model_id)) {
    return;
  }
  for (auto it = model_paths_.begin(); it != model_paths_.end(); ++it) {
    if (it->second == model_id) {
      model_paths_.erase(it);
      return;
    }
  }
}

void M2ModelRepository::Clear() {
  std::lock_guard lock(mutex_);
  models_.clear();
  model_paths_.clear();
  model_load_flights_.clear();
}

}
