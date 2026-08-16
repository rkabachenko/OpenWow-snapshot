#include "openwow/ui/runtime/lua/lua_binding.h"

#include "openwow/ui/runtime/lua/lua_runtime_state.h"

#include <atomic>
#include <memory>
#include <unordered_set>
#include <utility>

namespace openwow::ui::lua {

LuaStackRestore::LuaStackRestore(lua_State* state) noexcept
    : state_(state), top_(state != nullptr ? lua_gettop(state) : 0) {}

LuaStackRestore::~LuaStackRestore() noexcept {
  Restore();
}

void LuaStackRestore::Restore() noexcept {
  if (state_ != nullptr) {
    lua_settop(state_, top_);
    state_ = nullptr;
  }
}

int AbsoluteIndex(lua_State* state, const int index) noexcept {
  if (state == nullptr || index > 0 || index <= LUA_REGISTRYINDEX) {
    return index;
  }
  return lua_gettop(state) + index + 1;
}

bool CheckBoolean(lua_State* state, const int argument) {
  luaL_checktype(state, argument, LUA_TBOOLEAN);
  return lua_toboolean(state, argument) != 0;
}

lua_Integer CheckInteger(lua_State* state, const int argument) {
  return luaL_checkinteger(state, argument);
}

lua_Number CheckNumber(lua_State* state, const int argument) {
  return luaL_checknumber(state, argument);
}

std::string_view CheckString(lua_State* state, const int argument) {
  std::size_t size = 0;
  const char* value = luaL_checklstring(state, argument, &size);
  return {value, size};
}

std::optional<bool> CheckOptionalBoolean(lua_State* state,
                                         const int argument) {
  if (lua_isnoneornil(state, argument) != 0) {
    return std::nullopt;
  }
  return CheckBoolean(state, argument);
}

std::optional<lua_Integer> CheckOptionalInteger(lua_State* state,
                                                const int argument) {
  if (lua_isnoneornil(state, argument) != 0) {
    return std::nullopt;
  }
  return CheckInteger(state, argument);
}

std::optional<lua_Number> CheckOptionalNumber(lua_State* state,
                                              const int argument) {
  if (lua_isnoneornil(state, argument) != 0) {
    return std::nullopt;
  }
  return CheckNumber(state, argument);
}

std::optional<std::string_view> CheckOptionalString(lua_State* state,
                                                    const int argument) {
  if (lua_isnoneornil(state, argument) != 0) {
    return std::nullopt;
  }
  return CheckString(state, argument);
}

void PushTableField(lua_State* state, const int table_index,
                    const std::string_view field) {
  const int table = AbsoluteIndex(state, table_index);
  lua_pushlstring(state, field.empty() ? "" : field.data(), field.size());
  lua_gettable(state, table);
}

namespace {

std::atomic<std::uint64_t> next_binding_owner_id{1};

std::string CanonicalWidgetName(const std::string_view name) {
  std::string canonical;
  canonical.reserve(name.size());
  for (const unsigned char character : name) {
    canonical.push_back(character >= 'A' && character <= 'Z'
                            ? static_cast<char>(character - 'A' + 'a')
                            : static_cast<char>(character));
  }
  return canonical;
}

std::string BindingKey(const BindingDestination& destination,
                       const std::string_view name) {
  if (destination.kind == BindingKind::kGlobalFunction ||
      destination.kind == BindingKind::kConstant) {
    return "global:" + std::string(name);
  }
  const std::string owner =
      CanonicalWidgetName(destination.table_name.value_or(std::string{}));
  if (destination.kind == BindingKind::kWidgetType) {
    return "widget-type:" + owner;
  }
  return "widget-method:" + owner + ":" + std::string(name);
}

BindingDestination DestinationFor(const BindingDescriptor& descriptor) {
  return {descriptor.kind, descriptor.destination_table,
          descriptor.consume_after_create};
}

bool IsFunctionDestinationValid(const BindingDestination& destination) {
  switch (destination.kind) {
    case BindingKind::kGlobalFunction:
      return !destination.table_name && !destination.consume_after_create;
    case BindingKind::kWidgetMethod:
      return destination.table_name && !destination.table_name->empty() &&
             !destination.consume_after_create;
    case BindingKind::kConstant:
    case BindingKind::kWidgetType:
      return false;
  }
  return false;
}

void PushDestination(lua_State* state, const char* destination_table) {
  if (destination_table == nullptr) {
    return;
  }
  lua_getglobal(state, destination_table);
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    lua_newtable(state);
    lua_pushvalue(state, -1);
    lua_setglobal(state, destination_table);
  }
}

void Publish(lua_State* state, const char* destination_table,
             const char* public_name) {
  if (destination_table == nullptr) {
    lua_setglobal(state, public_name);
    return;
  }
  lua_setfield(state, -2, public_name);
  lua_pop(state, 1);
}

struct FunctionPublishContext final {
  const char* destination_table;
  const char* public_name;
  detail::LuaBindingLease* lease;
  lua_CFunction trampoline;
  lua_CFunction result_pusher;
};

int PublishFunctionProtected(lua_State* state) {
  const auto* context = static_cast<const FunctionPublishContext*>(
      lua_touserdata(state, 1));
  PushDestination(state, context->destination_table);
  lua_pushlightuserdata(state, context->lease);
  if (context->result_pusher != nullptr) {
    lua_pushcfunction(state, context->result_pusher);
  } else {
    lua_pushnil(state);
  }
  lua_pushstring(state, context->public_name);
  lua_pushcclosure(state, context->trampoline, 3);
  Publish(state, context->destination_table, context->public_name);
  return 0;
}

int InvokeRawBinding(lua_State* state) {
  const auto* lease = static_cast<const detail::LuaBindingLease*>(
      lua_touserdata(state, lua_upvalueindex(1)));
  if (lease == nullptr || lease->raw_handler == nullptr) {
    return luaL_error(state, "retired Lua binding");
  }
  int results = 0;
  bool failed = false;
  try {
    results = lease->raw_handler(state);
  } catch (...) {
    failed = true;
  }
  if (failed) {
    const char* name = lua_tostring(state, lua_upvalueindex(3));
    return luaL_error(state, "%s: handler failed",
                      name != nullptr ? name : "<native>");
  }
  return results;
}

struct ConstantPublishContext final {
  enum class ValueKind : std::uint8_t { kNumber, kBoolean, kString };

  const char* public_name;
  ValueKind kind;
  double number{0.0};
  bool boolean{false};
  const char* string_data{nullptr};
  std::size_t string_size{0};
};

ConstantPublishContext MakeConstantPublishContext(
    const char* public_name, const BindingConstantValue& value) {
  return std::visit(
      [public_name](const auto& current) {
        using Value = std::decay_t<decltype(current)>;
        if constexpr (std::same_as<Value, double>) {
          return ConstantPublishContext{
              public_name, ConstantPublishContext::ValueKind::kNumber,
              current};
        } else if constexpr (std::same_as<Value, bool>) {
          return ConstantPublishContext{
              public_name, ConstantPublishContext::ValueKind::kBoolean, 0.0,
              current};
        } else {
          return ConstantPublishContext{
              public_name, ConstantPublishContext::ValueKind::kString, 0.0,
              false, current.data(), current.size()};
        }
      },
      value);
}

int PublishConstantProtected(lua_State* state) {
  const auto* context = static_cast<const ConstantPublishContext*>(
      lua_touserdata(state, 1));
  switch (context->kind) {
    case ConstantPublishContext::ValueKind::kNumber:
      lua_pushnumber(state, context->number);
      break;
    case ConstantPublishContext::ValueKind::kBoolean:
      lua_pushboolean(state, context->boolean ? 1 : 0);
      break;
    case ConstantPublishContext::ValueKind::kString:
      lua_pushlstring(state, context->string_data, context->string_size);
      break;
  }
  lua_setglobal(state, context->public_name);
  return 0;
}

struct WidgetTypePublishContext final {
  const char* type_name;
};

int PublishWidgetTypeProtected(lua_State* state) {
  const auto* context = static_cast<const WidgetTypePublishContext*>(
      lua_touserdata(state, 1));
  lua_newtable(state);
  lua_setglobal(state, context->type_name);
  return 0;
}

struct Removal final {
  std::string key;
  const char* destination_table;
  const char* public_name;
  std::shared_ptr<detail::LuaBindingLease> lease;
};

struct RemovalContext final {
  const Removal* removals;
  std::size_t count;
};

int RemoveBindingsProtected(lua_State* state) {
  const auto* context = static_cast<const RemovalContext*>(
      lua_touserdata(state, 1));
  for (std::size_t index = 0; index < context->count; ++index) {
    const auto& removal = context->removals[index];
    if (removal.destination_table == nullptr) {
      lua_pushvalue(state, LUA_GLOBALSINDEX);
      lua_pushstring(state, removal.public_name);
      lua_pushnil(state);
      lua_rawset(state, -3);
      lua_pop(state, 1);
      continue;
    }

    lua_pushvalue(state, LUA_GLOBALSINDEX);
    lua_pushstring(state, removal.destination_table);
    lua_rawget(state, -2);
    if (lua_istable(state, -1) != 0) {
      lua_pushstring(state, removal.public_name);
      lua_pushnil(state);
      lua_rawset(state, -3);
    }
    lua_pop(state, 2);
  }
  return 0;
}

}

namespace detail {

void* ActiveBindingAdapter(lua_State* state) noexcept {
  const auto* lease = static_cast<const LuaBindingLease*>(
      lua_touserdata(state, lua_upvalueindex(1)));
  return lease == nullptr ? nullptr : lease->adapter;
}

const char* ActiveBindingName(lua_State* state) noexcept {
  const char* name = lua_tostring(state, lua_upvalueindex(3));
  return name != nullptr ? name : "<native>";
}

void* GlobalBindingAdapter(
    lua_State* state, const std::string_view global_name) noexcept {
  if (state == nullptr || global_name.empty()) {
    return nullptr;
  }
  LuaStackRestore restore(state);
  PushTableField(state, LUA_GLOBALSINDEX, global_name);
  if (lua_isfunction(state, -1) == 0 ||
      lua_getupvalue(state, -1, 1) == nullptr) {
    return nullptr;
  }
  const auto* lease = static_cast<const LuaBindingLease*>(
      lua_touserdata(state, -1));
  return lease != nullptr ? lease->adapter : nullptr;
}

}

BindingSet::BindingSet(std::string owner, const BindingScope scope,
                       BindingDestination destination)
    : owner_name_(std::move(owner)),
      scope_(scope),
      destination_(std::move(destination)),
      owner_id_(next_binding_owner_id.fetch_add(1,
                                                 std::memory_order_relaxed)) {}

BindingSet::~BindingSet() {
  Uninstall();
}

BindingSet::BindingSet(BindingSet&& other) noexcept
    : owner_name_(std::move(other.owner_name_)),
      scope_(other.scope_),
      destination_(std::move(other.destination_)),
      owner_id_(std::exchange(other.owner_id_, 0)),
      owner_(std::move(other.owner_)),
      registry_(std::move(other.registry_)),
      generation_(std::exchange(other.generation_, 0)),
      owned_leases_(std::move(other.owned_leases_)),
      descriptors_(std::move(other.descriptors_)) {}

BindingSet& BindingSet::operator=(BindingSet&& other) noexcept {
  if (this != &other) {
    Uninstall();
    owner_name_ = std::move(other.owner_name_);
    scope_ = other.scope_;
    destination_ = std::move(other.destination_);
    owner_id_ = std::exchange(other.owner_id_, 0);
    owner_ = std::move(other.owner_);
    registry_ = std::move(other.registry_);
    generation_ = std::exchange(other.generation_, 0);
    owned_leases_ = std::move(other.owned_leases_);
    descriptors_ = std::move(other.descriptors_);
  }
  return *this;
}

bool BindingSet::CanInstallInto(const LuaVm& vm) const noexcept {
  if (owner_id_ == 0 || !vm.alive() || !vm.owner_ || !vm.binding_registry_ ||
      !vm.owner_->alive) {
    return false;
  }
  if (generation_ == 0) {
    return true;
  }
  const auto owner = owner_.lock();
  return owner && owner.get() == vm.owner_.get() &&
         generation_ == vm.generation();
}

bool BindingSet::CanPublishBatch(
    const LuaVm& vm,
    const std::span<const BindingCandidate> candidates) const {
  if (!IsFunctionDestinationValid(destination_)) {
    return false;
  }
  std::unordered_set<std::string> widget_keys;
  widget_keys.reserve(candidates.size());
  for (const auto& candidate : candidates) {
    if ((candidate.kind == BindingKind::kConstant &&
         destination_.kind != BindingKind::kGlobalFunction) ||
        (candidate.kind != BindingKind::kConstant &&
         candidate.kind != BindingKind::kGlobalFunction &&
         candidate.kind != BindingKind::kWidgetMethod)) {
      return false;
    }
    BindingDestination candidate_destination = destination_;
    if (candidate.kind == BindingKind::kConstant) {
      candidate_destination = BindingDestination{BindingKind::kConstant};
    }
    std::string key = BindingKey(candidate_destination, candidate.name);
    if (candidate.kind != BindingKind::kWidgetMethod) {

      continue;
    }
    if (!widget_keys.insert(key).second) {
      return false;
    }
    const auto existing = vm.binding_registry_->claims.find(key);
    if (existing != vm.binding_registry_->claims.end()) {
      return false;
    }
  }
  return true;
}

bool BindingSet::InstallFunction(LuaVm& vm, const std::string_view name,
                                 const CollisionPolicy collision, void* adapter,
                                 const lua_CFunction trampoline,
                                 const lua_CFunction result_pusher,
                                 const lua_CFunction raw_handler) {
  const BindingKind kind = destination_.kind;
  BindingDescriptor descriptor{std::string(name), scope_, owner_name_, kind,
                               collision, destination_.table_name,
                               destination_.consume_after_create, trampoline,
                               false};
  const std::string key = BindingKey(destination_, name);
  auto& claims = vm.binding_registry_->claims;
  const auto existing = claims.find(key);
  if (existing != claims.end()) {
    if (kind == BindingKind::kWidgetMethod) {
      descriptors_.push_back(std::move(descriptor));
      return false;
    }
  }

  auto lease = std::make_shared<detail::LuaBindingLease>();
  lease->adapter = adapter;
  lease->raw_handler = raw_handler;
  owned_leases_.push_back(lease);
  vm.binding_registry_->leases.push_back(lease);
  FunctionPublishContext context{
      destination_.table_name ? destination_.table_name->c_str() : nullptr,
      descriptor.public_name.c_str(), lease.get(), trampoline, result_pusher};
  if (lua_cpcall(vm.state(), PublishFunctionProtected, &context) != 0) {
    lua_pop(vm.state(), 1);
    lease->adapter = nullptr;
    lease->raw_handler = nullptr;
    owned_leases_.pop_back();
    vm.binding_registry_->leases.pop_back();
    descriptors_.push_back(std::move(descriptor));
    return false;
  }
  if (existing != claims.end() && existing->second.lease) {
    existing->second.lease->adapter = nullptr;
    existing->second.lease->raw_handler = nullptr;
  }
  claims[key] = {owner_name_, static_cast<std::uint8_t>(kind), owner_id_, lease};
  descriptor.installed = true;
  descriptors_.push_back(std::move(descriptor));
  owner_ = vm.owner_;
  registry_ = vm.binding_registry_;
  generation_ = vm.generation();
  return true;
}

bool BindingSet::InstallConstant(LuaVm& vm,
                                 const ConstantBinding& definition) {
  BindingDescriptor descriptor{std::string(definition.name), scope_, owner_name_,
                               BindingKind::kConstant, definition.collision,
                               std::nullopt, false, nullptr, false};
  if (destination_.kind != BindingKind::kGlobalFunction) {
    descriptors_.push_back(std::move(descriptor));
    return false;
  }

  const BindingDestination constant_destination{BindingKind::kConstant};
  const std::string key = BindingKey(constant_destination, definition.name);
  auto& claims = vm.binding_registry_->claims;
  const auto existing = claims.find(key);

  ConstantPublishContext context = MakeConstantPublishContext(
      descriptor.public_name.c_str(), definition.value);
  if (lua_cpcall(vm.state(), PublishConstantProtected, &context) != 0) {
    lua_pop(vm.state(), 1);
    descriptors_.push_back(std::move(descriptor));
    return false;
  }
  if (existing != claims.end() && existing->second.lease) {
    existing->second.lease->adapter = nullptr;
    existing->second.lease->raw_handler = nullptr;
  }
  claims[key] = {owner_name_, static_cast<std::uint8_t>(BindingKind::kConstant),
                 owner_id_, nullptr};
  descriptor.installed = true;
  descriptors_.push_back(std::move(descriptor));
  owner_ = vm.owner_;
  registry_ = vm.binding_registry_;
  generation_ = vm.generation();
  return true;
}

bool BindingSet::InstallConstants(
    LuaVm& vm, const std::vector<ConstantBinding>& constants) {
  if (!CanInstallInto(vm)) {
    return false;
  }
  std::vector<BindingCandidate> candidates;
  candidates.reserve(constants.size());
  for (const auto& definition : constants) {
    candidates.push_back(CandidateFor(definition));
  }
  if (!CanPublishBatch(vm, candidates)) {
    return false;
  }
  bool succeeded = true;
  for (const auto& definition : constants) {
    succeeded = InstallConstant(vm, definition) && succeeded;
  }
  if (!succeeded) {
    Uninstall();
  }
  return succeeded;
}

bool BindingSet::InstallRawFunctions(
    LuaVm& vm, const std::vector<RawFunctionBinding>& functions) {
  if (!CanInstallInto(vm)) {
    return false;
  }
  std::vector<BindingCandidate> candidates;
  candidates.reserve(functions.size());
  for (const auto& function : functions) {
    if (function.handler == nullptr) {
      return false;
    }
    candidates.push_back(CandidateFor(function));
  }
  if (!CanPublishBatch(vm, candidates)) {
    return false;
  }
  bool succeeded = true;
  for (const auto& function : functions) {
    succeeded =
        InstallFunction(vm, function.name, function.collision,
                        function.adapter,
                        InvokeRawBinding, nullptr, function.handler) &&
        succeeded;
  }
  if (!succeeded) {
    Uninstall();
  }
  return succeeded;
}

bool BindingSet::ClaimWidgetMethods(
    LuaVm& vm, const std::vector<std::string_view>& methods) {
  if (!CanInstallInto(vm) || destination_.kind != BindingKind::kWidgetMethod ||
      !destination_.table_name || destination_.table_name->empty()) {
    return false;
  }

  std::unordered_set<std::string> batch_keys;
  batch_keys.reserve(methods.size());
  for (const auto method : methods) {
    if (method.empty()) {
      return false;
    }
    const std::string key = BindingKey(destination_, method);
    if (!batch_keys.insert(key).second ||
        vm.binding_registry_->claims.contains(key)) {
      return false;
    }
  }

  auto& claims = vm.binding_registry_->claims;
  for (const auto method : methods) {
    const std::string key = BindingKey(destination_, method);
    claims.emplace(key, detail::LuaBindingClaim{
                            owner_name_,
                            static_cast<std::uint8_t>(BindingKind::kWidgetMethod),
                            owner_id_, nullptr});
    descriptors_.push_back(BindingDescriptor{
        std::string(method), scope_, owner_name_, BindingKind::kWidgetMethod,
        CollisionPolicy::kReject, destination_.table_name, false, nullptr,
        true});
  }
  owner_ = vm.owner_;
  registry_ = vm.binding_registry_;
  generation_ = vm.generation();
  return true;
}

bool BindingSet::InstallWidgetType(LuaVm& vm) {
  if (!CanInstallInto(vm) || destination_.kind != BindingKind::kWidgetType ||
      !destination_.table_name || destination_.table_name->empty()) {
    return false;
  }
  BindingDescriptor descriptor{
      *destination_.table_name, scope_, owner_name_, BindingKind::kWidgetType,
      CollisionPolicy::kReject, destination_.table_name,
      destination_.consume_after_create, nullptr, false};
  const std::string key = BindingKey(destination_, *destination_.table_name);
  auto& registry = *vm.binding_registry_;
  if (registry.claims.contains(key) ||
      registry.consumed_widget_types.contains(key)) {
    descriptors_.push_back(std::move(descriptor));
    return false;
  }

  WidgetTypePublishContext context{descriptor.public_name.c_str()};
  if (lua_cpcall(vm.state(), PublishWidgetTypeProtected, &context) != 0) {
    lua_pop(vm.state(), 1);
    descriptors_.push_back(std::move(descriptor));
    return false;
  }
  registry.claims[key] = {
      owner_name_, static_cast<std::uint8_t>(BindingKind::kWidgetType),
      owner_id_, nullptr};
  descriptor.installed = true;
  descriptors_.push_back(std::move(descriptor));
  owner_ = vm.owner_;
  registry_ = vm.binding_registry_;
  generation_ = vm.generation();
  return true;
}

bool BindingSet::ConsumeWidgetType(LuaVm& vm) {
  if (!CanInstallInto(vm) || destination_.kind != BindingKind::kWidgetType ||
      !destination_.table_name || !destination_.consume_after_create) {
    return false;
  }
  const std::string key = BindingKey(destination_, *destination_.table_name);
  auto claim = vm.binding_registry_->claims.find(key);
  if (claim == vm.binding_registry_->claims.end() ||
      claim->second.owner_id != owner_id_) {
    return false;
  }
  vm.binding_registry_->consumed_widget_types.insert(key);
  Removal removal{key, nullptr, destination_.table_name->c_str(), nullptr};
  RemovalContext context{&removal, 1};
  if (lua_cpcall(vm.state(), RemoveBindingsProtected, &context) != 0) {
    lua_pop(vm.state(), 1);
    vm.binding_registry_->consumed_widget_types.erase(key);
    return false;
  }
  vm.binding_registry_->claims.erase(claim);
  for (auto& descriptor : descriptors_) {
    if (descriptor.kind == BindingKind::kWidgetType && descriptor.installed) {
      descriptor.installed = false;
    }
  }
  return true;
}

void BindingSet::Uninstall() noexcept {
  const auto owner = owner_.lock();
  const auto registry = registry_.lock();
  if (!owner || !registry || !owner->alive || owner->state == nullptr ||
      owner->generation != generation_) {
    for (const auto& lease : owned_leases_) {
      lease->adapter = nullptr;
      lease->raw_handler = nullptr;
    }
    owner_.reset();
    registry_.reset();
    generation_ = 0;
    owned_leases_.clear();
    descriptors_.clear();
    return;
  }

  std::vector<Removal> removals;
  try {
    removals.reserve(descriptors_.size());
    for (const auto& descriptor : descriptors_) {
      if (!descriptor.installed) {
        continue;
      }
      std::string key =
          BindingKey(DestinationFor(descriptor), descriptor.public_name);
      const auto claim = registry->claims.find(key);
      if (claim == registry->claims.end() ||
          claim->second.owner_id != owner_id_) {
        continue;
      }

      if (descriptor.kind == BindingKind::kWidgetMethod &&
          !claim->second.lease) {
        continue;
      }
      removals.push_back(
          {std::move(key),
           descriptor.kind == BindingKind::kWidgetMethod &&
                   descriptor.destination_table
               ? descriptor.destination_table->c_str()
               : nullptr,
           descriptor.public_name.c_str(), claim->second.lease});
    }
  } catch (const std::exception&) {
    return;
  }

  RemovalContext context{removals.data(), removals.size()};
  if (!removals.empty() &&
      lua_cpcall(owner->state, RemoveBindingsProtected,
                 &context) != 0) {
    lua_pop(owner->state, 1);
    return;
  }
  for (const auto& lease : owned_leases_) {
    lease->adapter = nullptr;
    lease->raw_handler = nullptr;
  }
  for (auto iterator = registry->claims.begin();
       iterator != registry->claims.end();) {
    if (iterator->second.owner_id == owner_id_) {
      iterator = registry->claims.erase(iterator);
    } else {
      ++iterator;
    }
  }

  descriptors_.clear();
  owner_.reset();
  registry_.reset();
  generation_ = 0;
  owned_leases_.clear();
}

}
