#pragma once

#include "openwow/game/battlenet_api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace openwow::game {

inline constexpr std::size_t kTumorMaxAdapters = 20;
inline constexpr std::size_t kTumorMaxEventAdapters = 20;
inline constexpr std::size_t kTumorMaxEventBindings = 20;
inline constexpr std::size_t kTumorMaxConnectionBindings = 64;
inline constexpr std::size_t kTumorMaxTokenBindings = 64;

enum class TumorMessageType : std::uint32_t {
  kByReference = 0,
  kByValue = 1,
};

struct TumorMessage {
  TumorMessageType type{TumorMessageType::kByReference};
  std::uint32_t value{0};
  std::uint32_t source{0};
  std::uint32_t flags{0};
};

class ITumorMessageTarget {
public:
  virtual ~ITumorMessageTarget() = default;
  virtual int OnTumorMessage(const TumorMessage& message,
                             std::uintptr_t context) = 0;
};

class ITumorAdapter : public ITumorMessageTarget {
public:
  [[nodiscard]] virtual std::uint32_t BindingId() const noexcept {
    return static_cast<std::uint32_t>(-1);
  }
  virtual void OnAttached(std::size_t) {}
  virtual void OnDetached() {}
  virtual void OnBroadcast(std::uintptr_t) {}
  virtual int OnConnectionComplete(std::uint32_t) { return 0; }
};

enum class TumorConnectionMode : std::uint8_t {
  kPrimary,
  kSecondary,
};

class ITumorTransport {
public:
  virtual ~ITumorTransport() = default;
  virtual std::uint32_t CreateConnection(TumorConnectionMode mode,
                                         std::int32_t argument,
                                         std::uint32_t* in_out_payload) = 0;
  virtual void SendEvent(std::uint8_t event_id, std::uint32_t destination,
                         std::int32_t payload) = 0;
};

class Tumor final {
public:
  explicit Tumor(ITumorTransport* transport = nullptr)
      : transport_(transport) {}
  ~Tumor();

  Tumor(const Tumor&) = delete;
  Tumor& operator=(const Tumor&) = delete;

  void SetTransport(ITumorTransport* transport) noexcept {
    transport_ = transport;
  }

  [[nodiscard]] std::optional<std::size_t>
  AddAdapter(std::shared_ptr<ITumorAdapter> adapter);
  [[nodiscard]] std::optional<std::size_t>
  AddEventAdapter(std::shared_ptr<ITumorAdapter> adapter);
  bool RemoveAdapter(std::size_t index, const ITumorAdapter* expected);
  bool RemoveEventAdapter(std::size_t index, const ITumorAdapter* expected);

  [[nodiscard]] std::optional<std::uint32_t> AllocateAdapterId();
  void Broadcast(std::uintptr_t value);

  bool BindEvent(std::uint32_t key,
                 std::shared_ptr<ITumorMessageTarget> target,
                 std::uint32_t metadata);
  std::size_t DispatchMessage(const TumorMessage& message,
                              std::uintptr_t context);

  [[nodiscard]] std::uint32_t CreateConnectionBinding(
      TumorConnectionMode mode, std::size_t adapter_index,
      std::int32_t argument, std::uint32_t* in_out_payload);
  int CompleteConnection(std::uint32_t connection_id);

  [[nodiscard]] std::optional<std::size_t> AllocateTokenBinding();

  [[nodiscard]] std::size_t AdapterCount() const noexcept;
  [[nodiscard]] std::size_t EventAdapterCount() const noexcept;
  [[nodiscard]] std::size_t ActiveEventBindingCount() const noexcept;
  [[nodiscard]] std::size_t ActiveConnectionBindingCount() const noexcept;

private:
  struct EventBinding {
    std::uint32_t key{0};
    std::weak_ptr<ITumorMessageTarget> target;
    bool active{false};
    std::uint32_t metadata{0};
  };

  struct ConnectionBinding {
    std::uint32_t connection_id{0};
    std::weak_ptr<ITumorAdapter> adapter;
    bool active{false};
    std::uint32_t metadata{0};
  };

  struct TokenBinding {
    std::uint32_t index{0};
    bool active{false};
  };

  ITumorTransport* transport_{nullptr};
  std::array<std::shared_ptr<ITumorAdapter>, kTumorMaxAdapters> adapters_{};
  std::array<std::shared_ptr<ITumorAdapter>, kTumorMaxEventAdapters>
      event_adapters_{};
  std::uint32_t next_adapter_id_{0};
  std::array<EventBinding, kTumorMaxEventBindings> event_bindings_{};
  std::array<ConnectionBinding, kTumorMaxConnectionBindings>
      connection_bindings_{};
  std::array<TokenBinding, kTumorMaxTokenBindings> token_bindings_{};
};

class ITumorTendril {
public:
  virtual ~ITumorTendril() = default;
  virtual void OnAddedToTumorManager() = 0;
};

class TumorManager final {
public:
  explicit TumorManager(ITumorTransport* transport = nullptr)
      : transport_(transport) {}
  ~TumorManager();

  [[nodiscard]] bool AddTendril(std::shared_ptr<ITumorTendril> tendril);
  bool RemoveTendril(const ITumorTendril* tendril);
  [[nodiscard]] std::size_t TendrilCount() const noexcept;

  [[nodiscard]] BNetVariant SendEvent(
      std::span<const BNetVariant> arguments) const;
  static BNetVariant ScriptSendEvent(
      void* context, std::span<const BNetVariant> arguments);

private:
  ITumorTransport* transport_{nullptr};
  std::array<std::shared_ptr<ITumorTendril>, kTumorMaxAdapters> tendrils_{};
};

}
