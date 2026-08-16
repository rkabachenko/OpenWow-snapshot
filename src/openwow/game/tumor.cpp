#include "openwow/game/tumor.h"

#include "openwow/debug/diagnostics/error_handler.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace openwow::game {
namespace {

void ReportTumorAssert(const char* message) {
  openwow::debug::ErrorHandler::Get().Report(
      openwow::debug::ErrorSeverity::Assert, message, __FILE__, __LINE__);
}

const char* VariantString(const BNetVariant& variant) {
  if (variant.type == BNetVariantType::kInlineString ||
      variant.type == BNetVariantType::kStringPtr) {
    return variant.str_ptr;
  }
  return nullptr;
}

std::uint32_t PackDestination(const char* text) {
  std::uint32_t value = 0;
  if (!text) {
    return value;
  }
  while (*text) {
    value = (value << 8u) | static_cast<unsigned char>(*text++);
  }
  return value;
}

std::uint8_t EventId(const BNetVariant& variant) {
  std::uint32_t value = 0;
  switch (variant.type) {
  case BNetVariantType::kBool:
    value = variant.bool_val ? 1u : 0u;
    break;
  case BNetVariantType::kInt32:
  case BNetVariantType::kPresenceId:
    value = static_cast<std::uint32_t>(variant.int_val);
    break;
  case BNetVariantType::kFloat64:

    {
      double rounded_input = variant.float_val;
      constexpr double kSignedBoundary = 9223372036854775808.0;
      if (rounded_input >= kSignedBoundary) {
        rounded_input -= kSignedBoundary;
      }
      value = static_cast<std::uint32_t>(std::llrint(rounded_input));
    }
    break;
  case BNetVariantType::kInlineString:
    if (variant.str_ptr) {
      for (const char* p = variant.str_ptr; *p; ++p) {
        value = static_cast<unsigned char>(*p);
      }
    }
    break;
  case BNetVariantType::kStringPtr:
    if (variant.str_ptr) {
      value = static_cast<std::uint32_t>(
          std::strtoull(variant.str_ptr, nullptr, 0));
    }
    break;
  default:
    break;
  }
  return static_cast<std::uint8_t>(value);
}

}

Tumor::~Tumor() {

  for (auto& adapter : adapters_) {
    if (adapter) {
      adapter->OnDetached();
      adapter.reset();
    }
  }
  for (auto& adapter : event_adapters_) {
    if (adapter) {
      adapter->OnDetached();
      adapter.reset();
    }
  }
}

std::optional<std::size_t>
Tumor::AddAdapter(std::shared_ptr<ITumorAdapter> adapter) {
  if (!adapter) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index < adapters_.size(); ++index) {
    if (!adapters_[index]) {
      adapters_[index] = std::move(adapter);
      adapters_[index]->OnAttached(index);
      return index;
    }
  }
  ReportTumorAssert("No space in Tumor::m_adapters for new Tendril");
  return std::nullopt;
}

std::optional<std::size_t>
Tumor::AddEventAdapter(std::shared_ptr<ITumorAdapter> adapter) {
  if (!adapter) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index < event_adapters_.size(); ++index) {
    if (!event_adapters_[index]) {
      event_adapters_[index] = std::move(adapter);
      event_adapters_[index]->OnAttached(index);
      return index;
    }
  }
  ReportTumorAssert("No space in Tumor::m_eventAdapters for new Tendril");
  return std::nullopt;
}

bool Tumor::RemoveAdapter(std::size_t index, const ITumorAdapter* expected) {
  if (index >= adapters_.size() || adapters_[index].get() != expected) {
    return false;
  }
  adapters_[index]->OnDetached();
  adapters_[index].reset();
  return true;
}

bool Tumor::RemoveEventAdapter(std::size_t index,
                               const ITumorAdapter* expected) {
  if (index >= event_adapters_.size() ||
      event_adapters_[index].get() != expected) {
    return false;
  }
  event_adapters_[index]->OnDetached();
  event_adapters_[index].reset();
  return true;
}

std::optional<std::uint32_t> Tumor::AllocateAdapterId() {
  const std::uint32_t first = next_adapter_id_++;
  std::uint32_t candidate = first;
  do {
    const bool collision = std::any_of(
        event_adapters_.begin(), event_adapters_.end(),
        [candidate](const std::shared_ptr<ITumorAdapter>& adapter) {
          return adapter && adapter->BindingId() == candidate;
        });
    if (!collision) {
      return candidate;
    }
    candidate = next_adapter_id_++;
  } while (candidate != first);
  return std::nullopt;
}

void Tumor::Broadcast(std::uintptr_t value) {
  for (const auto& adapter : adapters_) {
    if (adapter) {
      adapter->OnBroadcast(value);
    }
  }
}

bool Tumor::BindEvent(std::uint32_t key,
                      std::shared_ptr<ITumorMessageTarget> target,
                      std::uint32_t metadata) {
  if (!target) {
    return false;
  }
  for (EventBinding& binding : event_bindings_) {
    if (!binding.active) {
      binding.key = key;
      binding.target = std::move(target);
      binding.active = true;
      binding.metadata = metadata;
      return true;
    }
  }
  ReportTumorAssert("Ran out of room in Tumor::m_eventBindings");
  return false;
}

std::size_t Tumor::DispatchMessage(const TumorMessage& message,
                                   std::uintptr_t context) {
  std::size_t dispatched = 0;
  if (message.type == TumorMessageType::kByReference) {
    for (EventBinding& binding : event_bindings_) {
      if (!binding.active || binding.key != message.value) {
        continue;
      }
      if (auto target = binding.target.lock()) {
        target->OnTumorMessage(message, context);
        ++dispatched;
      } else {
        binding.active = false;
      }
    }
  } else if (message.type == TumorMessageType::kByValue &&
             message.value < event_adapters_.size()) {
    const auto& adapter = event_adapters_[message.value];
    if (adapter) {
      adapter->OnTumorMessage(message, context);
      dispatched = 1;
    }
  }
  return dispatched;
}

std::uint32_t Tumor::CreateConnectionBinding(
    TumorConnectionMode mode, std::size_t adapter_index,
    std::int32_t argument, std::uint32_t* in_out_payload) {
  auto free = std::find_if(connection_bindings_.begin(),
                           connection_bindings_.end(),
                           [](const ConnectionBinding& binding) {
                             return !binding.active;
                           });
  if (free == connection_bindings_.end()) {
    ReportTumorAssert("Ran out of room in Tumor::m_connectionBindings");
    ReportTumorAssert("Couldn't get a connectionId!");
    return std::numeric_limits<std::uint32_t>::max();
  }
  if (!transport_) {
    ReportTumorAssert("Couldn't get a connectionId!");
    return std::numeric_limits<std::uint32_t>::max();
  }

  const std::uint32_t connection_id =
      transport_->CreateConnection(mode, argument, in_out_payload);
  free->connection_id = connection_id;
  free->adapter = adapter_index < adapters_.size()
                      ? std::weak_ptr<ITumorAdapter>(adapters_[adapter_index])
                      : std::weak_ptr<ITumorAdapter>{};
  free->active = true;
  free->metadata = in_out_payload ? *in_out_payload : 0;
  return connection_id;
}

int Tumor::CompleteConnection(std::uint32_t connection_id) {
  for (ConnectionBinding& binding : connection_bindings_) {
    if (!binding.active || binding.connection_id != connection_id) {
      continue;
    }

    int result = 0;
    if (auto adapter = binding.adapter.lock()) {
      result = adapter->OnConnectionComplete(connection_id);
    }
    binding.connection_id = 0;
    binding.adapter.reset();
    binding.active = false;
    return result;
  }
  return 0;
}

std::optional<std::size_t> Tumor::AllocateTokenBinding() {
  for (std::size_t index = 0; index < token_bindings_.size(); ++index) {
    TokenBinding& binding = token_bindings_[index];
    if (!binding.active) {
      binding.index = static_cast<std::uint32_t>(index);
      binding.active = true;
      return index;
    }
  }
  ReportTumorAssert("Ran out of room in Tumor::m_tokenBindings");
  return std::nullopt;
}

std::size_t Tumor::AdapterCount() const noexcept {
  return std::count_if(adapters_.begin(), adapters_.end(),
                       [](const auto& value) { return value != nullptr; });
}

std::size_t Tumor::EventAdapterCount() const noexcept {
  return std::count_if(event_adapters_.begin(), event_adapters_.end(),
                       [](const auto& value) { return value != nullptr; });
}

std::size_t Tumor::ActiveEventBindingCount() const noexcept {
  return std::count_if(event_bindings_.begin(), event_bindings_.end(),
                       [](const EventBinding& value) { return value.active; });
}

std::size_t Tumor::ActiveConnectionBindingCount() const noexcept {
  return std::count_if(
      connection_bindings_.begin(), connection_bindings_.end(),
      [](const ConnectionBinding& value) { return value.active; });
}

TumorManager::~TumorManager() {

  for (auto& tendril : tendrils_) {
    tendril.reset();
  }
}

bool TumorManager::AddTendril(std::shared_ptr<ITumorTendril> tendril) {
  if (!tendril) {
    return false;
  }
  for (auto& slot : tendrils_) {
    if (!slot) {
      slot = std::move(tendril);
      slot->OnAddedToTumorManager();
      return true;
    }
  }
  return false;
}

bool TumorManager::RemoveTendril(const ITumorTendril* tendril) {
  for (auto& slot : tendrils_) {
    if (slot.get() == tendril) {
      slot.reset();
      return true;
    }
  }
  return false;
}

std::size_t TumorManager::TendrilCount() const noexcept {
  return std::count_if(tendrils_.begin(), tendrils_.end(),
                       [](const auto& value) { return value != nullptr; });
}

BNetVariant TumorManager::SendEvent(
    std::span<const BNetVariant> arguments) const {
  if (arguments.size() != 3) {
    ReportTumorAssert("Usage: SendEvent(eventId, dest, event)");
    return {};
  }
  if (!transport_) {
    return {};
  }
  const std::uint8_t event_id = EventId(arguments[0]);
  const std::uint32_t destination = PackDestination(VariantString(arguments[1]));
  const std::int32_t payload =
      arguments[2].type == BNetVariantType::kRefCounted
          ? arguments[2].int_val
          : 0;
  transport_->SendEvent(event_id, destination, payload);
  return {};
}

BNetVariant TumorManager::ScriptSendEvent(
    void* context, std::span<const BNetVariant> arguments) {
  return context ? static_cast<TumorManager*>(context)->SendEvent(arguments)
                 : BNetVariant{};
}

}
