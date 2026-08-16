
#include "openwow/game/comsat_sound_io.h"

#include "openwow/game/comsat_sound_output_channel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace openwow::game {
class ComSatSoundIOBridge {
public:
  virtual ~ComSatSoundIOBridge() = default;

  virtual void Initialize(ComSatSoundIOState &state, std::uint32_t max_players,
                          float master_volume) = 0;
  virtual void DestroySlot(ComSatSoundIOState &state, std::uint32_t slot_index) = 0;
};

namespace {

constexpr char kVoiceChatSoundLabel[] = "<Voice Chat>";
constexpr std::uint16_t kComSatVariableBitrateBaselineBits = 360;
constexpr std::uint32_t kComSatVariableBitrateCompleteBatchMs = 100;
constexpr std::uint16_t kComSatFixedWidthVoiceFrameBits = 960;
constexpr std::size_t kComSatFixedWidthVoiceFrameBytes = kComSatFixedWidthVoiceFrameBits / 8u;
constexpr std::size_t kComSatVoiceBatchPacketCapacityBytes = 2560u;
constexpr std::size_t kComSatPlaybackBlocksPerUpdate = 5u;
constexpr std::uint32_t kComSatPlaybackDecoderResyncPeriod = 32u;
constexpr double kComSatPlaybackFrameDurationSeconds = 0.1;
constexpr std::uint8_t kComSatDefaultSelectionValue = 0x80u;

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

SocketHandle NativeToSocketHandle(const ComSatDatagramSocket::NativeHandle native_handle) {
  return static_cast<SocketHandle>(native_handle);
}

ComSatDatagramSocket::NativeHandle SocketHandleToNative(const SocketHandle handle) {
  return static_cast<ComSatDatagramSocket::NativeHandle>(handle);
}

std::size_t ResolveTalkerSlotIndex(const std::uint32_t talker_id) {
  return static_cast<std::size_t>(static_cast<std::uint8_t>(talker_id));
}

std::uint64_t ComposeComSatIdentifier(const std::uint32_t low, const std::uint32_t high) {
  return (static_cast<std::uint64_t>(high) << 32u) | static_cast<std::uint64_t>(low);
}

std::int32_t AddWrappedInt32(const std::int32_t lhs, const std::int32_t rhs) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(lhs) +
                                   static_cast<std::uint32_t>(rhs));
}

void SplitComSatIdentifier(const std::uint64_t identifier, std::uint32_t &low,
                           std::uint32_t &high) {
  low = static_cast<std::uint32_t>(identifier);
  high = static_cast<std::uint32_t>(identifier >> 32u);
}

ComSatSoundIOSlot *FindAllocatedTalkerSlot(ComSatSoundIOState &state,
                                           const std::uint32_t talker_id) {
  const std::size_t slot_index = ResolveTalkerSlotIndex(talker_id);
  if (slot_index >= state.slots.size()) {
    return nullptr;
  }

  ComSatSoundIOSlot &slot = state.slots[slot_index];
  if (!slot.allocated) {
    return nullptr;
  }

  return &slot;
}

ComSatSoundIOSlot *FindActiveTalkerSlot(ComSatSoundIOState &state, const std::uint32_t talker_id) {
  ComSatSoundIOSlot *const slot = FindAllocatedTalkerSlot(state, talker_id);
  if (slot == nullptr || !slot->active || slot->member_id == 0) {
    return nullptr;
  }

  return slot;
}

ComSatSoundIOSlot *FindTalkerSlot(ComSatSoundIOState &state, const std::uint32_t talker_id) {
  const std::size_t slot_index = ResolveTalkerSlotIndex(talker_id);
  if (slot_index >= state.slots.size()) {
    return nullptr;
  }

  return &state.slots[slot_index];
}

ComSatSoundIOSessionState *FindTrackedSession(ComSatSoundIOState &state,
                                              const std::uint64_t session_id) {
  const auto it = state.sessions.find(session_id);
  if (it == state.sessions.end()) {
    return nullptr;
  }

  return &it->second;
}

bool SlotContainsSession(const ComSatSoundIOSlot &slot, const std::uint64_t session_id) {
  return std::find(slot.bound_sessions.begin(), slot.bound_sessions.end(), session_id) !=
         slot.bound_sessions.end();
}

ComSatSoundIOSlot::SessionBinding &EnsureTalkerSessionBinding(ComSatSoundIOSlot &slot,
                                                             const std::uint64_t session_id) {
  if (!SlotContainsSession(slot, session_id)) {
    slot.bound_sessions.push_back(session_id);
  }
  return slot.session_bindings[session_id];
}

bool SlotContainsSecondarySession(const ComSatSoundIOSlot &slot, const std::uint64_t session_id) {
  return std::find(slot.secondary_session_ids.begin(), slot.secondary_session_ids.end(),
                   session_id) != slot.secondary_session_ids.end();
}

bool SessionContainsTransportMember(const ComSatSoundIOSessionState &session,
                                    const std::uint64_t member_id) {
  return std::find(session.transport_member_ids.begin(), session.transport_member_ids.end(),
                   member_id) != session.transport_member_ids.end();
}

bool HasDetachedSessionMemberById(const ComSatSoundIOState &state, const std::uint64_t member_id) {
  return std::any_of(state.sessions.begin(), state.sessions.end(),
                     [member_id](const auto &entry) {
                       const ComSatSoundIOSessionState &session = entry.second;
                       return std::any_of(session.detached_members.begin(),
                                          session.detached_members.end(),
                                          [member_id](const ComSatSoundIOSessionMember &member) {
                                            return member.member_id == member_id;
                                          });
                     });
}

void EnsureSessionTransportMember(ComSatSoundIOSessionState &session,
                                  const std::uint64_t member_id) {
  if (!SessionContainsTransportMember(session, member_id)) {
    session.transport_member_ids.push_back(member_id);
  }
}

bool SlotOwnsMemberId(const ComSatSoundIOSlot &slot, const std::uint64_t member_id) {
  return slot.allocated && slot.member_id != 0 && slot.member_id == member_id;
}

ComSatSoundIOSlot *FindLastTalkerSlotByMemberId(ComSatSoundIOState &state,
                                                const std::uint64_t member_id) {
  ComSatSoundIOSlot *matching_slot = nullptr;
  for (auto &slot : state.slots) {
    if (SlotOwnsMemberId(slot, member_id)) {
      matching_slot = &slot;
    }
  }

  return matching_slot;
}

std::uint8_t EnsureSessionMemberSelectionValue(ComSatSoundIOSessionState &session,
                                               const std::uint64_t member_id) {
  const auto [it, inserted] =
      session.member_selection_values.try_emplace(member_id, kComSatDefaultSelectionValue);
  (void)inserted;
  return it->second;
}

std::size_t ResolvePlaybackWindowSize(const std::int32_t window) {
  return window > 0 ? static_cast<std::size_t>(window) : 0u;
}

void ResetDetachedSessionMemberPlaybackRuntime(ComSatSoundIOSessionMember &member,
                                               const ComSatSoundIOState &state) {
  member.playback_block_cursor = 0.0;

  member.playback_frames.assign(
      ResolvePlaybackWindowSize(state.playback_update_window),
      ComSatSoundIOSessionMember::PlaybackFrame{});
  for (auto &frame : member.playback_frames) {
    frame.ClearOccupiedFlag();
  }

  const std::size_t sample_count = ResolvePlaybackWindowSize(state.playback_update_window_shadow);
  member.playback_volume_history.samples.assign(sample_count,
                                                static_cast<double>(state.master_volume));
  member.playback_volume_history.sum = static_cast<double>(sample_count) * state.master_volume;
}

ComSatSoundIOSessionMember *FindDetachedSessionMember(ComSatSoundIOSessionState &session,
                                                      const std::uint64_t member_id) {
  const auto it = std::find_if(session.detached_members.begin(), session.detached_members.end(),
                               [member_id](const ComSatSoundIOSessionMember &member) {
                                 return member.member_id == member_id;
                               });
  if (it == session.detached_members.end()) {
    return nullptr;
  }

  return &*it;
}

bool RemoveDetachedSessionMember(ComSatSoundIOSessionState &session,
                                 const std::uint64_t member_id) {
  const auto original_size = session.detached_members.size();
  session.detached_members.erase(
      std::remove_if(session.detached_members.begin(), session.detached_members.end(),
                     [member_id](const ComSatSoundIOSessionMember &member) {
                       return member.member_id == member_id;
                     }),
      session.detached_members.end());
  return session.detached_members.size() != original_size;
}

void ClearPendingTalkerVoiceBatchState(ComSatSoundIOSlot &slot) {
  slot.pending_voice_frame_count = 0;
  slot.pending_voice_duration_ms = 0;
}

void ClearTalkerTransportSessionReference(ComSatSoundIOSlot &slot, const std::uint64_t session_id) {
  if (slot.transport_session_id != session_id) {
    return;
  }

  slot.transport_session_id = 0;
  ClearPendingTalkerVoiceBatchState(slot);
}

void RemoveTalkerSessionBinding(ComSatSoundIOSlot &slot, const std::uint64_t session_id) {
  slot.bound_sessions.erase(
      std::remove(slot.bound_sessions.begin(), slot.bound_sessions.end(), session_id),
      slot.bound_sessions.end());
  slot.session_bindings.erase(session_id);
}

void RemoveTalkerSecondarySessionBinding(ComSatSoundIOSlot &slot, const std::uint64_t session_id) {
  slot.secondary_session_ids.erase(
      std::remove(slot.secondary_session_ids.begin(), slot.secondary_session_ids.end(), session_id),
      slot.secondary_session_ids.end());
}

void RemoveSessionTransportMember(ComSatSoundIOSessionState &session, const std::uint64_t member_id) {
  session.transport_member_ids.erase(
      std::remove(session.transport_member_ids.begin(), session.transport_member_ids.end(),
                  member_id),
      session.transport_member_ids.end());
}

bool RemoveSessionBindingFromMatchingTalkerSlots(ComSatSoundIOState &state,
                                                 const std::uint64_t member_id,
                                                 const std::uint64_t session_id) {
  bool removed_or_matched = false;
  for (auto &slot : state.slots) {
    if (!SlotOwnsMemberId(slot, member_id)) {
      continue;
    }

    RemoveTalkerSessionBinding(slot, session_id);
    removed_or_matched = true;
  }

  return removed_or_matched;
}

void ClearPendingVoiceFrames(ComSatPendingVoiceBatch &pending_batch) {
  pending_batch.batch.frames.clear();
  pending_batch.batch.accumulated_duration_ms = 0;
}

void ResetTalkerVoiceBatchState(ComSatSoundIOSlot &slot) {
  slot.transport_session_id = 0;
  slot.voice_batch_open = false;
  ClearPendingTalkerVoiceBatchState(slot);
}

void ClearTalkerRuntimeState(ComSatSoundIOSlot &slot) {
  slot.active = false;
  slot.playback_enabled = false;
  slot.playback_metering_enabled = false;
  slot.playback_queue_active = false;
  slot.playback_decoder_resync_pending = false;
  slot.voice_data_received = false;
  slot.talker_id = 0;
  slot.member_id = 0;
  slot.stream_source_id = 0;
  slot.playback_decoder_resync_anchor = 0;
  slot.playback_frame_sequence = 0;
  slot.playback_pcm_scratch.fill(0);
  slot.bound_sessions.clear();
  slot.secondary_session_ids.clear();
  slot.session_bindings.clear();
  ResetTalkerVoiceBatchState(slot);
}

void InitializeTalkerRuntimeState(ComSatSoundIOSlot &slot, const std::uint32_t talker_id,
                                  const std::uint64_t member_id, const float initial_volume,
                                  const std::uint64_t stream_source_id) {
  ClearTalkerRuntimeState(slot);
  slot.active = true;
  slot.talker_id = talker_id;
  slot.member_id = member_id;
  slot.stream_source_id = stream_source_id;
  slot.volume = initial_volume;
}

bool MutateSessionSet(std::vector<std::uint64_t> &session_ids, const std::uint64_t session_id,
                      const ComSatTalkerSessionSetMutation mutation) {
  auto it = std::find(session_ids.begin(), session_ids.end(), session_id);

  switch (mutation) {
  case ComSatTalkerSessionSetMutation::AddIfMissing:
    if (it == session_ids.end()) {
      session_ids.push_back(session_id);
    }
    return true;
  case ComSatTalkerSessionSetMutation::Remove:
    if (it == session_ids.end()) {
      return false;
    }
    session_ids.erase(it);
    return true;
  case ComSatTalkerSessionSetMutation::Toggle:
    if (it == session_ids.end()) {
      session_ids.push_back(session_id);
    } else {
      session_ids.erase(it);
    }
    return true;
  }

  return false;
}

ComSatSoundIOSlot::SessionBinding &UpsertSessionBinding(ComSatSoundIOSlot &slot,
                                                        const std::uint64_t session_id) {
  return slot.session_bindings[session_id];
}

void ComSatSoundIO_MeasurePcmLevel(const std::uint8_t *pcm_data,
                                   const std::uint32_t byte_count,
                                   float &out_linear_peak_to_peak,
                                   float &out_db_level) {
  out_linear_peak_to_peak = 0.0f;
  out_db_level = 0.0f;

  const std::size_t sample_count = byte_count / sizeof(std::int16_t);
  if (sample_count == 0) {
    return;
  }

  std::int16_t minimum_sample = std::numeric_limits<std::int16_t>::max();
  std::int16_t maximum_sample = std::numeric_limits<std::int16_t>::min();
  for (std::size_t sample_index = 0; sample_index < sample_count; ++sample_index) {
    std::int16_t sample = 0;
    std::memcpy(&sample, pcm_data + sample_index * sizeof(sample), sizeof(sample));
    minimum_sample = std::min(minimum_sample, sample);
    maximum_sample = std::max(maximum_sample, sample);
  }

  if (minimum_sample > maximum_sample) {
    return;
  }

  const auto sample_span =
      static_cast<std::int32_t>(maximum_sample) - static_cast<std::int32_t>(minimum_sample);

  out_linear_peak_to_peak =
      static_cast<float>(static_cast<double>(sample_span) * 0.000015258789);

  const double scaled = static_cast<double>(sample_span) * 0.0020000001;
  if (scaled > 0.0) {
    out_db_level = static_cast<float>(20.0 * std::log10(scaled));
  }
}

float MeasurePlaybackPeakToPeakLinear(const std::uint8_t *pcm_data,
                                      const std::uint32_t byte_count) {
  float linear = 0.0f;
  float db = 0.0f;
  ComSatSoundIO_MeasurePcmLevel(pcm_data, byte_count, linear, db);
  return linear;
}

void UpdatePlaybackDecoderResyncState(ComSatSoundIOSlot &slot) {
  std::uint32_t adjusted_frame_sequence = slot.playback_frame_sequence;
  if (adjusted_frame_sequence < slot.playback_decoder_resync_anchor) {
    adjusted_frame_sequence += 0x10000u;
  }

  if (adjusted_frame_sequence - slot.playback_decoder_resync_anchor >=
          kComSatPlaybackDecoderResyncPeriod &&
      (slot.playback_frame_sequence % kComSatPlaybackDecoderResyncPeriod) == 0u) {
    slot.playback_decoder_resync_pending = true;
  }
}

bool ResolveSessionMemberOutput(const ComSatSoundIOState &state,
                                const std::uint64_t stored_member_id,
                                const std::uint8_t stored_talker_slot_index,
                                std::uint32_t &member_id_low, std::uint32_t &member_id_high,
                                std::uint8_t &talker_slot_index) {
  std::uint64_t resolved_member_id = stored_member_id;
  talker_slot_index = stored_talker_slot_index;
  if (stored_talker_slot_index != 0xFFu) {
    const std::size_t slot_index = stored_talker_slot_index;
    if (slot_index >= state.slots.size()) {
      return false;
    }

    resolved_member_id = state.slots[slot_index].member_id;
  }

  if (resolved_member_id == 0) {
    return false;
  }

  SplitComSatIdentifier(resolved_member_id, member_id_low, member_id_high);
  return true;
}

class LittleEndianBitPacker {
public:
  LittleEndianBitPacker(std::uint8_t *buffer, const std::size_t capacity_bytes) noexcept
      : buffer_(buffer), capacity_bytes_(capacity_bytes) {}

  [[nodiscard]] bool Append(const std::uint8_t *source, const std::size_t bit_count) {
    if (source == nullptr || !CanAppend(bit_count)) {
      return false;
    }

    const std::size_t whole_bytes = bit_count / 8u;
    const std::size_t tail_bits = bit_count % 8u;

    if (bit_offset_ == 0u) {
      std::memcpy(buffer_ + byte_offset_, source, whole_bytes);
      byte_offset_ += whole_bytes;
    } else {
      for (std::size_t i = 0; i < whole_bytes; ++i) {
        const auto value = source[i];
        buffer_[byte_offset_] |= static_cast<std::uint8_t>(value << bit_offset_);
        buffer_[byte_offset_ + 1u] |= static_cast<std::uint8_t>(value >> (8u - bit_offset_));
        ++byte_offset_;
      }
    }

    if (tail_bits != 0u) {
      const auto tail_mask = static_cast<std::uint8_t>((1u << tail_bits) - 1u);
      const auto tail_value = static_cast<std::uint8_t>(source[whole_bytes] & tail_mask);
      buffer_[byte_offset_] |= static_cast<std::uint8_t>(tail_value << bit_offset_);

      const std::size_t combined_bits = bit_offset_ + tail_bits;
      if (combined_bits >= 8u) {
        buffer_[byte_offset_ + 1u] |= static_cast<std::uint8_t>(tail_value >> (8u - bit_offset_));
        ++byte_offset_;
        bit_offset_ = combined_bits - 8u;
      } else {
        bit_offset_ = combined_bits;
      }
    }

    return true;
  }

  [[nodiscard]] std::size_t Finalize() noexcept {
    if (bit_offset_ != 0u) {
      ++byte_offset_;
      bit_offset_ = 0u;
    }

    return byte_offset_;
  }

private:
  [[nodiscard]] bool CanAppend(const std::size_t additional_bits) const noexcept {
    const std::size_t used_bits = (byte_offset_ * 8u) + bit_offset_;
    return used_bits + additional_bits <= capacity_bytes_ * 8u;
  }

  std::uint8_t *buffer_{nullptr};
  std::size_t capacity_bytes_{0};
  std::size_t byte_offset_{0};
  std::size_t bit_offset_{0};
};

class LittleEndianBitReader {
public:
  LittleEndianBitReader(const std::uint8_t *buffer, const std::size_t capacity_bytes) noexcept
      : buffer_(buffer), capacity_bits_(capacity_bytes * 8u) {}

  [[nodiscard]] bool Read(std::uint8_t *dest, const std::size_t bit_count) {
    if (dest == nullptr || consumed_bits_ + bit_count > capacity_bits_) {
      return false;
    }

    const std::size_t whole_bytes = bit_count / 8u;
    const std::size_t tail_bits = bit_count % 8u;

    if (bit_offset_ == 0u) {
      std::memcpy(dest, buffer_ + byte_offset_, whole_bytes);
      byte_offset_ += whole_bytes;
    } else {
      for (std::size_t i = 0; i < whole_bytes; ++i) {
        const auto lo = static_cast<std::uint8_t>(buffer_[byte_offset_] >> bit_offset_);
        ++byte_offset_;
        const auto hi =
            static_cast<std::uint8_t>(buffer_[byte_offset_] << (8u - bit_offset_));
        dest[i] = static_cast<std::uint8_t>(lo | hi);
      }
    }

    if (tail_bits != 0u) {
      const auto mask = static_cast<std::uint8_t>((1u << tail_bits) - 1u);
      const std::size_t combined = bit_offset_ + tail_bits;
      if (combined > 8u) {
        auto value = static_cast<std::uint8_t>(buffer_[byte_offset_] >> bit_offset_);
        ++byte_offset_;
        value = static_cast<std::uint8_t>(
            value | (buffer_[byte_offset_] << (8u - (combined - 8u) - (8u - tail_bits))));
        dest[whole_bytes] = static_cast<std::uint8_t>(value & mask);
        bit_offset_ = combined - 8u;
      } else {
        dest[whole_bytes] =
            static_cast<std::uint8_t>((buffer_[byte_offset_] >> bit_offset_) & mask);
        bit_offset_ = combined % 8u;
        if (combined == 8u) {
          ++byte_offset_;
        }
      }
    }

    consumed_bits_ += bit_count;
    return true;
  }

private:
  const std::uint8_t *buffer_{nullptr};
  std::size_t capacity_bits_{0};
  std::size_t byte_offset_{0};
  std::size_t bit_offset_{0};
  std::size_t consumed_bits_{0};
};

sockaddr *EndpointSockAddr(ComSatDatagramEndpoint &endpoint) {
  return reinterpret_cast<sockaddr *>(endpoint.storage.data());
}

const sockaddr *EndpointSockAddr(const ComSatDatagramEndpoint &endpoint) {
  return reinterpret_cast<const sockaddr *>(endpoint.storage.data());
}

sockaddr_in *EndpointSockAddrIn(ComSatDatagramEndpoint &endpoint) {
  return reinterpret_cast<sockaddr_in *>(endpoint.storage.data());
}

const sockaddr_in *EndpointSockAddrIn(const ComSatDatagramEndpoint &endpoint) {
  return reinterpret_cast<const sockaddr_in *>(endpoint.storage.data());
}

void ResetIpv4Endpoint(ComSatDatagramEndpoint &endpoint, const std::uint32_t address_network_order,
                       const std::uint16_t host_port) {
  endpoint.length = sizeof(sockaddr_in);
  endpoint.storage.fill(std::byte{0});
  auto *address = EndpointSockAddrIn(endpoint);
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
  address->sin_len = static_cast<decltype(address->sin_len)>(sizeof(sockaddr_in));
#endif
  address->sin_family = AF_INET;
  address->sin_port = htons(host_port);
  address->sin_addr.s_addr = address_network_order;
}

void SetSocketNonBlocking(const SocketHandle handle) {
  if (handle == kInvalidSocket) {
    return;
  }
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket(handle, FIONBIO, &mode);
#else
  const int flags = fcntl(handle, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(handle, F_SETFL, flags | O_NONBLOCK);
  }
#endif
}

void CloseSocketHandle(const SocketHandle handle) {
  if (handle == kInvalidSocket) {
    return;
  }
#ifdef _WIN32
  closesocket(handle);
#else
  ::close(handle);
#endif
}

void SetSocketReceiveBufferSize(const SocketHandle handle, const int size) {
  if (handle == kInvalidSocket) {
    return;
  }
  setsockopt(handle, SOL_SOCKET, SO_RCVBUF,
#ifdef _WIN32
             reinterpret_cast<const char *>(&size),
#else
             &size,
#endif
             sizeof(size));
}

class NullComSatSoundIOBridge final : public ComSatSoundIOBridge {
public:
  void Initialize(ComSatSoundIOState &state, const std::uint32_t max_players,
                  const float master_volume) override {
    state.initialized = true;
    state.capture_enabled = false;
    state.master_volume = master_volume;
    state.active_slots = max_players;
    state.playback_update_window = 5;
    state.playback_update_window_shadow = 5;
    state.sessions.clear();
    state.slots.assign(max_players, ComSatSoundIOSlot{});
    for (auto &slot : state.slots) {
      slot.Reset(master_volume);
    }
  }

  void DestroySlot(ComSatSoundIOState &state, const std::uint32_t slot_index) override {
    if (slot_index >= state.slots.size()) {
      return;
    }

    auto &slot = state.slots[slot_index];
    if (!slot.allocated) {
      return;
    }

    slot.allocated = false;
    ClearTalkerRuntimeState(slot);
    slot.volume = state.master_volume;
    slot.priority = 3;
    slot.pending_voice_sequence = 0;
    slot.sound_label.clear();
    if (state.active_slots != 0) {
      --state.active_slots;
    }
  }
};

void NotifyTalkerPlaybackShutdown(ComSatSoundIOState &state, const ComSatSoundIOSlot &slot) {
  if (state.playback_observer == nullptr || slot.talker_id == 0u) {
    return;
  }

  if (!slot.playback_enabled && !slot.playback_queue_active) {
    return;
  }

  state.playback_observer->OnTalkerPlaybackDisabled(slot.talker_id);
}

}

void ComSatSoundIOState::ResetOwnedState(const bool preserve_bridge, const bool preserve_observer) {
  for (std::size_t slot_index = 0; slot_index < slots.size(); ++slot_index) {
    const auto &slot = slots[slot_index];
    if (!slot.allocated) {
      continue;
    }

    NotifyTalkerPlaybackShutdown(*this, slot);

    if (bridge_ != nullptr) {
      bridge_->DestroySlot(*this, static_cast<std::uint32_t>(slot_index));
    }
  }

  initialized = false;
  capture_enabled = false;
  active_slots = 0;
  playback_update_window = 5;
  playback_update_window_shadow = 5;
  slots.clear();
  sessions.clear();
  output_channel_ = nullptr;
  if (!preserve_observer) {
    playback_observer = nullptr;
  }
  if (!preserve_bridge) {
    bridge_.reset();
  }
}

ComSatSoundIOFrameCoordinator::ComSatSoundIOFrameCoordinator(
    ComSatSoundIOUpdateStage &receive_stage, ComSatSoundIOUpdateStage &playback_stage) noexcept
    : receive_stage_(receive_stage), playback_stage_(playback_stage) {}

void ComSatSoundIOFrameCoordinator::Update(const std::uint32_t tick_count_ms) {
  receive_stage_.Update(tick_count_ms);
  playback_stage_.Update(tick_count_ms);
}

ComSatSoundIOPlaybackUpdateStage::ComSatSoundIOPlaybackUpdateStage(
    ComSatSoundIOState &state, ComSatSoundIOPlaybackRuntime &runtime,
    ComSatSoundIOTalkerFrameAdvancer *const advancer) noexcept
    : state_(state), runtime_(runtime), advancer_(advancer) {}

void ComSatSoundIOPlaybackUpdateStage::SetChainedStage(
    ComSatSoundIOUpdateStage *const stage) noexcept {
  chained_stage_ = stage;
}

void ComSatSoundIOPlaybackUpdateStage::SetPendingPlayback(
    const ComSatPendingTalkerPlayback *const entries, const std::size_t count) noexcept {
  pending_playback_ = entries;
  pending_playback_count_ = count;
}

void ComSatSoundIOPlaybackUpdateStage::Update(const std::uint32_t tick_count_ms) {

  if (chained_stage_ != nullptr) {
    chained_stage_->Update(tick_count_ms);
  }

  ComSatSoundIO_AdvancePlayback(state_, runtime_, tick_count_ms, advancer_);
  ComSatSoundIO_ProcessPendingTalkerPlayback(state_, pending_playback_, pending_playback_count_);

  pending_playback_ = nullptr;
  pending_playback_count_ = 0;
}

void ComSatSoundIOSlot::Reset(const float slot_volume) {
  allocated = true;
  ClearTalkerRuntimeState(*this);
  volume = slot_volume;
  priority = 3;
  pending_voice_sequence = 0;
  sound_label = kVoiceChatSoundLabel;
}

ComSatSoundIOState::ComSatSoundIOState() = default;
ComSatSoundIOState::~ComSatSoundIOState() {
  ResetOwnedState(false, false);
}
ComSatSoundIOState::ComSatSoundIOState(ComSatSoundIOState &&other) noexcept
    : initialized(other.initialized), capture_enabled(other.capture_enabled),
      active_slots(other.active_slots), master_volume(other.master_volume),
      playback_update_window(other.playback_update_window),
      playback_update_window_shadow(other.playback_update_window_shadow),
      slots(std::move(other.slots)), sessions(std::move(other.sessions)),
      playback_observer(other.playback_observer), bridge_(std::move(other.bridge_)),
      output_channel_(other.output_channel_) {
  other.initialized = false;
  other.capture_enabled = false;
  other.active_slots = 0;
  other.master_volume = 1.0f;
  other.playback_update_window = 5;
  other.playback_update_window_shadow = 5;
  other.slots.clear();
  other.sessions.clear();
  other.playback_observer = nullptr;
  other.output_channel_ = nullptr;
}

ComSatSoundIOState &ComSatSoundIOState::operator=(ComSatSoundIOState &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  ResetOwnedState(false, false);

  initialized = other.initialized;
  capture_enabled = other.capture_enabled;
  active_slots = other.active_slots;
  master_volume = other.master_volume;
  playback_update_window = other.playback_update_window;
  playback_update_window_shadow = other.playback_update_window_shadow;
  slots = std::move(other.slots);
  sessions = std::move(other.sessions);
  playback_observer = other.playback_observer;
  bridge_ = std::move(other.bridge_);
  output_channel_ = other.output_channel_;

  other.initialized = false;
  other.capture_enabled = false;
  other.active_slots = 0;
  other.master_volume = 1.0f;
  other.playback_update_window = 5;
  other.playback_update_window_shadow = 5;
  other.slots.clear();
  other.sessions.clear();
  other.playback_observer = nullptr;
  other.output_channel_ = nullptr;
  return *this;
}

void ComSatSoundIO_Initialize(ComSatSoundIOState &state, const std::uint32_t max_players,
                              const float master_vol) {
  if (!state.bridge_) {
    state.bridge_ = std::make_unique<NullComSatSoundIOBridge>();
  }

  state.ResetOwnedState(true, true);
  state.bridge_->Initialize(state, max_players, master_vol);
}

void ComSatSoundIO_DestroySlot(ComSatSoundIOState &state, const std::uint32_t slot_index) {
  if (slot_index < state.slots.size()) {
    NotifyTalkerPlaybackShutdown(state, state.slots[slot_index]);
  }
  if (!state.bridge_) {
    state.bridge_ = std::make_unique<NullComSatSoundIOBridge>();
  }
  state.bridge_->DestroySlot(state, slot_index);
}

std::int32_t ComSatSoundIO_GetVoiceOutputDriverIndex(const ComSatSoundIOState &state) {
  if (state.output_channel_) {
    return state.output_channel_->GetVoiceOutputDriverIndex();
  }
  return -1;
}

std::int32_t ComSatSoundIO_GetVoiceInputDriverIndex(const ComSatSoundIOState &state) {
  if (state.output_channel_) {
    return state.output_channel_->GetVoiceInputDriverIndex();
  }
  return -1;
}

bool ComSatSoundIO_SetVoiceOutputDriverIndex(ComSatSoundIOState &state,
                                             const std::int32_t index) {
  if (state.output_channel_) {
    return state.output_channel_->SetVoiceOutputDriverIndex(index);
  }
  return false;
}

bool ComSatSoundIO_SetVoiceInputDriverIndex(ComSatSoundIOState &state,
                                            const std::int32_t index) {
  if (state.output_channel_) {
    return state.output_channel_->SetVoiceInputDriverIndex(index);
  }
  return false;
}

void ComSatSoundIO_SetOutputChannel(ComSatSoundIOState &state,
                                    ComSatSoundOutputChannel *channel) noexcept {
  state.output_channel_ = channel;
}

void ComSatSoundIO_SetPlaybackObserver(ComSatSoundIOState &state,
                                       ComSatSoundIOPlaybackObserver *observer) noexcept {
  state.playback_observer = observer;
}

std::int32_t ComSatSoundIO_AdjustPlaybackUpdateWindow(ComSatSoundIOState &state,
                                                      const std::int32_t delta) {
  state.playback_update_window = AddWrappedInt32(state.playback_update_window, delta);
  state.playback_update_window_shadow = state.playback_update_window;
  return state.playback_update_window;
}

void ComSatSoundIO_RegisterSession(ComSatSoundIOState &state, const std::uint32_t session_id_low,
                                   const std::uint32_t session_id_high) {
  state.sessions.try_emplace(ComposeComSatIdentifier(session_id_low, session_id_high));
}

void ComSatSoundIO_UnregisterSession(ComSatSoundIOState &state, const std::uint32_t session_id_low,
                                     const std::uint32_t session_id_high) {
  const std::uint64_t session_id = ComposeComSatIdentifier(session_id_low, session_id_high);
  for (auto &slot : state.slots) {
    ClearTalkerTransportSessionReference(slot, session_id);
  }

  const auto session_it = state.sessions.find(session_id);
  if (session_it == state.sessions.end()) {
    return;
  }

  state.sessions.erase(session_it);
  for (auto &slot : state.slots) {
    RemoveTalkerSessionBinding(slot, session_id);
  }
}

bool ComSatSoundIO_SetSessionTransportRoutingId(ComSatSoundIOState &state,
                                                const std::uint32_t session_id_low,
                                                const std::uint32_t session_id_high,
                                                const std::uint16_t transport_routing_id) {
  const std::uint64_t session_id = ComposeComSatIdentifier(session_id_low, session_id_high);
  ComSatSoundIOSessionState *const session = FindTrackedSession(state, session_id);
  if (session == nullptr) {
    return false;
  }

  session->transport_routing_id = transport_routing_id;
  return true;
}

bool ComSatSoundIO_BindSessionMemberToTalkerSlot(ComSatSoundIOState &state,
                                                 const std::uint32_t session_id_low,
                                                 const std::uint32_t session_id_high,
                                                 const std::uint32_t talker_id,
                                                 const std::uint32_t member_id_low,
                                                 const std::uint32_t member_id_high) {
  const std::uint64_t session_id = ComposeComSatIdentifier(session_id_low, session_id_high);
  ComSatSoundIOSessionState *const session = FindTrackedSession(state, session_id);
  if (session == nullptr) {
    return false;
  }

  const std::size_t slot_index = ResolveTalkerSlotIndex(talker_id);
  if (slot_index >= state.slots.size()) {
    return false;
  }

  ComSatSoundIOSlot &slot = state.slots[slot_index];
  const std::uint64_t member_id = ComposeComSatIdentifier(member_id_low, member_id_high);
  if (slot.member_id == 0) {
    slot.member_id = member_id;
  }

  if (!SlotContainsSession(slot, session_id)) {
    slot.bound_sessions.push_back(session_id);
  }
  EnsureSessionTransportMember(*session, member_id);

  if (!slot.active) {
    return false;
  }

  ComSatSoundIOSlot::SessionBinding &binding = EnsureTalkerSessionBinding(slot, session_id);
  binding.selection_value = kComSatDefaultSelectionValue;
  binding.volume_scale = 1.0f;
  RemoveTalkerSecondarySessionBinding(slot, session_id);

  if (slot.transport_session_id == 0) {
    (void)ComSatSoundIO_SetTalkerTransportSession(state, talker_id, session_id_low,
                                                  session_id_high);
  }

  return true;
}

bool ComSatSoundIO_SetTalkerTransportSession(ComSatSoundIOState &state,
                                             const std::uint32_t talker_id,
                                             const std::uint32_t session_id_low,
                                             const std::uint32_t session_id_high) {
  ComSatSoundIOSlot *const slot = FindAllocatedTalkerSlot(state, talker_id);
  if (slot == nullptr) {
    return false;
  }

  const std::uint64_t session_id = ComposeComSatIdentifier(session_id_low, session_id_high);
  if (slot->transport_session_id != 0 && slot->transport_session_id == session_id) {
    return true;
  }

  ClearPendingTalkerVoiceBatchState(*slot);
  if (session_id == 0) {
    slot->transport_session_id = 0;
    return true;
  }

  if (FindTrackedSession(state, session_id) == nullptr) {
    slot->transport_session_id = 0;
    return false;
  }

  slot->transport_session_id = session_id;
  return true;
}

bool ComSatSoundIO_AddSessionMember(ComSatSoundIOState &state, const std::uint32_t session_id_low,
                                    const std::uint32_t session_id_high,
                                    const std::uint32_t member_id_low,
                                    const std::uint32_t member_id_high) {
  const std::uint64_t session_id = ComposeComSatIdentifier(session_id_low, session_id_high);
  ComSatSoundIOSessionState *const session = FindTrackedSession(state, session_id);
  if (session == nullptr) {
    return false;
  }

  const std::uint64_t member_id = ComposeComSatIdentifier(member_id_low, member_id_high);
  if (HasDetachedSessionMemberById(state, member_id)) {
    return false;
  }

  const std::uint8_t selection_value = EnsureSessionMemberSelectionValue(*session, member_id);
  ComSatSoundIOSessionMember member{.member_id = member_id,
                                    .status_flags = 0u,
                                    .talker_slot_index = 0xFFu,
                                    .selection_value = selection_value,
                                    .volume_scale = 1.0f};
  ResetDetachedSessionMemberPlaybackRuntime(member, state);
  session->detached_members.push_back(std::move(member));

  return true;
}

bool ComSatSoundIO_SetBoundSessionMemberStatusFlags(ComSatSoundIOState &state,
                                                    const std::uint32_t session_id_low,
                                                    const std::uint32_t session_id_high,
                                                    const std::uint32_t member_id_low,
                                                    const std::uint32_t member_id_high,
                                                    const std::uint8_t status_flags) {
  const std::uint64_t session_id = ComposeComSatIdentifier(session_id_low, session_id_high);
  if (FindTrackedSession(state, session_id) == nullptr) {
    return false;
  }

  const std::uint64_t member_id = ComposeComSatIdentifier(member_id_low, member_id_high);
  ComSatSoundIOSlot *const slot = FindLastTalkerSlotByMemberId(state, member_id);
  if (slot == nullptr || !SlotContainsSession(*slot, session_id)) {
    return false;
  }

  UpsertSessionBinding(*slot, session_id).member_status_flags = status_flags;
  return true;
}

bool ComSatSoundIO_SetDetachedSessionMemberStatusFlags(ComSatSoundIOState &state,
                                                       const std::uint32_t session_id_low,
                                                       const std::uint32_t session_id_high,
                                                       const std::uint32_t member_id_low,
                                                       const std::uint32_t member_id_high,
                                                       const std::uint8_t status_flags) {
  const std::uint64_t session_id = ComposeComSatIdentifier(session_id_low, session_id_high);
  ComSatSoundIOSessionState *const session = FindTrackedSession(state, session_id);
  if (session == nullptr) {
    return false;
  }

  const std::uint64_t member_id = ComposeComSatIdentifier(member_id_low, member_id_high);
  ComSatSoundIOSessionMember *const member = FindDetachedSessionMember(*session, member_id);
  if (member == nullptr) {
    return false;
  }

  member->status_flags = status_flags;
  return true;
}

bool ComSatSoundIO_SetSessionMemberSelectionValue(ComSatSoundIOState &state,
                                                  const std::uint32_t session_id_low,
                                                  const std::uint32_t session_id_high,
                                                  const std::uint32_t member_id_low,
                                                  const std::uint32_t member_id_high,
                                                  const std::uint8_t selection_value) {
  const std::uint64_t session_id = ComposeComSatIdentifier(session_id_low, session_id_high);
  ComSatSoundIOSessionState *const session = FindTrackedSession(state, session_id);
  if (session == nullptr) {
    return false;
  }

  const std::uint64_t member_id = ComposeComSatIdentifier(member_id_low, member_id_high);
  session->member_selection_values[member_id] = selection_value;
  if (ComSatSoundIOSessionMember *const member = FindDetachedSessionMember(*session, member_id);
      member != nullptr) {
    member->selection_value = selection_value;
  }

  return true;
}

bool ComSatSoundIO_RemoveSessionMember(ComSatSoundIOState &state,
                                       const std::uint32_t session_id_low,
                                       const std::uint32_t session_id_high,
                                       const std::uint32_t member_id_low,
                                       const std::uint32_t member_id_high) {
  const std::uint64_t session_id = ComposeComSatIdentifier(session_id_low, session_id_high);
  ComSatSoundIOSessionState *const session = FindTrackedSession(state, session_id);
  if (session == nullptr) {
    return false;
  }

  const std::uint64_t member_id = ComposeComSatIdentifier(member_id_low, member_id_high);
  RemoveSessionTransportMember(*session, member_id);
  session->member_selection_values.erase(member_id);
  const bool removed_detached_member = RemoveDetachedSessionMember(*session, member_id);
  if (RemoveSessionBindingFromMatchingTalkerSlots(state, member_id, session_id)) {
    return true;
  }

  return removed_detached_member;
}

int ComSatSoundIO_GetSessionMemberCount(ComSatSoundIOState &state,
                                        const std::uint32_t session_id_low,
                                        const std::uint32_t session_id_high) {
  const std::uint64_t session_id = ComposeComSatIdentifier(session_id_low, session_id_high);
  const ComSatSoundIOSessionState *const session = FindTrackedSession(state, session_id);
  if (session == nullptr) {
    return -1;
  }

  int count = static_cast<int>(session->detached_members.size());
  for (const auto &slot : state.slots) {
    if (slot.allocated && SlotContainsSession(slot, session_id)) {
      ++count;
    }
  }

  return count;
}

bool ComSatSoundIO_GetSessionMemberByIndex(
    ComSatSoundIOState &state, const std::uint32_t session_id_low,
    const std::uint32_t session_id_high, const std::uint32_t member_index,
    std::uint32_t &member_id_low, std::uint32_t &member_id_high, std::uint8_t &talker_slot_index) {
  const std::uint64_t session_id = ComposeComSatIdentifier(session_id_low, session_id_high);
  const ComSatSoundIOSessionState *const session = FindTrackedSession(state, session_id);
  if (session == nullptr) {
    return false;
  }

  if (member_index >= static_cast<std::uint32_t>(ComSatSoundIO_GetSessionMemberCount(
                          state, session_id_low, session_id_high))) {
    return false;
  }

  std::uint32_t resolved_index = 0;
  for (std::size_t slot_index = 0; slot_index < state.slots.size(); ++slot_index) {
    const ComSatSoundIOSlot &slot = state.slots[slot_index];
    if (!slot.allocated || !SlotContainsSession(slot, session_id)) {
      continue;
    }

    if (resolved_index == member_index) {
      return ResolveSessionMemberOutput(state, slot.member_id,
                                        static_cast<std::uint8_t>(slot_index), member_id_low,
                                        member_id_high, talker_slot_index);
    }

    ++resolved_index;
  }

  for (const auto &member : session->detached_members) {
    if (resolved_index == member_index) {
      return ResolveSessionMemberOutput(state, member.member_id, member.talker_slot_index,
                                        member_id_low, member_id_high, talker_slot_index);
    }

    ++resolved_index;
  }

  return false;
}

bool ComSatSoundIO_CreateTalkerSlot(ComSatSoundIOState &state, const std::uint32_t talker_id,
                                    const std::uint32_t member_id_low,
                                    const std::uint32_t member_id_high, const float initial_volume,
                                    const std::uint32_t stream_source_low,
                                    const std::uint32_t stream_source_high) {
  ComSatSoundIOSlot *const slot = FindTalkerSlot(state, talker_id);
  if (slot == nullptr || !slot->allocated) {
    return false;
  }

  const bool talker_already_active = slot->active || slot->member_id != 0;
  InitializeTalkerRuntimeState(
      *slot, talker_id, ComposeComSatIdentifier(member_id_low, member_id_high), initial_volume,
      ComposeComSatIdentifier(stream_source_low, stream_source_high));
  return !talker_already_active;
}

bool ComSatSoundIO_DestroyTalkerSlot(ComSatSoundIOState &state, const std::uint32_t talker_id) {
  ComSatSoundIOSlot *const slot = FindTalkerSlot(state, talker_id);
  if (slot == nullptr || !slot->allocated || slot->member_id == 0) {
    return true;
  }

  ClearTalkerRuntimeState(*slot);
  return true;
}

bool ComSatSoundIO_SetTalkerPlaybackEnabled(ComSatSoundIOState &state,
                                            const std::uint32_t talker_id, const bool enabled) {
  ComSatSoundIOSlot *slot = FindActiveTalkerSlot(state, talker_id);
  if (slot == nullptr) {
    return false;
  }

  if (!enabled && slot->playback_enabled && state.playback_observer != nullptr) {
    state.playback_observer->OnTalkerPlaybackDisabled(talker_id);
  }
  if (!enabled && slot->playback_enabled && state.OutputChannel() != nullptr) {
    state.OutputChannel()->ResetChannel(
        static_cast<std::uint32_t>(ResolveTalkerSlotIndex(talker_id)));
  }

  slot->playback_enabled = enabled;
  return true;
}

bool ComSatSoundIO_SetTalkerPlaybackMeteringEnabled(ComSatSoundIOState &state,
                                                    const std::uint32_t talker_id,
                                                    const bool enabled) {
  const std::size_t slot_index = ResolveTalkerSlotIndex(talker_id);
  if (slot_index >= state.slots.size()) {
    return false;
  }

  if (ComSatSoundIOSlot *slot = FindActiveTalkerSlot(state, talker_id); slot != nullptr) {
    slot->playback_metering_enabled = enabled;
  }

  return true;
}

bool ComSatSoundIO_NotifyTalkerFirstVoiceData(ComSatSoundIOState &state,
                                              const std::uint32_t talker_id) {
  ComSatSoundIOSlot *const slot = FindAllocatedTalkerSlot(state, talker_id);
  if (slot == nullptr) {
    return false;
  }

  if (slot->voice_data_received) {
    return false;
  }

  slot->voice_data_received = true;

  return true;
}

bool ComSatSoundIO_SetTalkerPlaybackVolume(ComSatSoundIOState &state, const std::uint32_t talker_id,
                                           const float volume) {
  if (volume < 0.0f || volume > 1.0f) {
    return false;
  }

  if (!state.initialized) {
    return false;
  }

  if (ComSatSoundIOSlot *slot = FindActiveTalkerSlot(state, talker_id); slot != nullptr) {
    slot->volume = volume;
  }

  return true;
}

bool ComSatSoundIO_SetTalkerSessionGain(ComSatSoundIOState &state, const std::uint32_t talker_id,
                                        const std::uint32_t session_id_low,
                                        const std::uint32_t session_id_high, const float gain) {
  ComSatSoundIOSlot *slot = FindActiveTalkerSlot(state, talker_id);
  if (slot == nullptr) {
    return false;
  }

  UpsertSessionBinding(*slot, ComposeComSatIdentifier(session_id_low, session_id_high))
      .session_gain = gain;
  return true;
}

bool ComSatSoundIO_SetTalkerSessionVolume(ComSatSoundIOState &state, const std::uint32_t talker_id,
                                          const std::uint32_t session_id_low,
                                          const std::uint32_t session_id_high, const float volume) {
  ComSatSoundIOSlot *slot = FindActiveTalkerSlot(state, talker_id);
  if (slot == nullptr) {
    return false;
  }

  UpsertSessionBinding(*slot, ComposeComSatIdentifier(session_id_low, session_id_high))
      .volume_scale = volume;
  return true;
}

bool ComSatSoundIO_SetTalkerSessionSelectionValue(ComSatSoundIOState &state,
                                                  const std::uint32_t talker_id,
                                                  const std::uint32_t session_id_low,
                                                  const std::uint32_t session_id_high,
                                                  const std::uint8_t selection_value) {
  ComSatSoundIOSlot *slot = FindActiveTalkerSlot(state, talker_id);
  if (slot == nullptr) {
    return false;
  }

  UpsertSessionBinding(*slot, ComposeComSatIdentifier(session_id_low, session_id_high))
      .selection_value = selection_value;
  return true;
}

void ComSatSoundIO_QueueSilentPlaybackFrames(ComSatSoundIOState &state,
                                             const std::uint32_t talker_id,
                                             const std::uint32_t session_index,
                                             const float block_run_count,
                                             const double frame_duration_seconds) {
  ComSatSoundIOSlot *const slot = FindActiveTalkerSlot(state, talker_id);
  if (slot == nullptr) {
    return;
  }

  slot->playback_pcm_scratch.fill(0);
  if (state.playback_observer != nullptr) {
    state.playback_observer->OnTalkerPlaybackSilencePrepared(
        talker_id, slot->playback_pcm_scratch.data(),
        static_cast<std::uint32_t>(slot->playback_pcm_scratch.size()));
  }

  float remaining_blocks = block_run_count;
  while (remaining_blocks >= 1.0f) {
    if (state.playback_observer != nullptr) {
      state.playback_observer->OnTalkerPlaybackBufferQueued(
          talker_id, session_index, slot->playback_pcm_scratch.data(),
          static_cast<std::uint32_t>(slot->playback_pcm_scratch.size()), frame_duration_seconds);
    }

    remaining_blocks -= 1.0f;
  }
}

std::size_t ComSatSoundIO_ProcessTalkerPlaybackFrames(ComSatSoundIOState &state,
                                                      const std::uint32_t talker_id,
                                                      const ComSatTalkerPlaybackFrame *frames,
                                                      const std::size_t frame_count) {
  ComSatSoundIOSlot *const slot = FindActiveTalkerSlot(state, talker_id);
  if (slot == nullptr || frames == nullptr) {
    return 0;
  }

  const std::size_t frames_to_process = std::min(frame_count, kComSatPlaybackBlocksPerUpdate);
  std::size_t processed_frame_count = 0;
  for (; processed_frame_count < frames_to_process; ++processed_frame_count) {
    const ComSatTalkerPlaybackFrame &frame = frames[processed_frame_count];
    if (frame.meter_pcm_data == nullptr ||
        frame.meter_pcm_byte_count < ComSatSoundIOSlot::kPlaybackPcmScratchBytes) {
      break;
    }

    std::memcpy(slot->playback_pcm_scratch.data(), frame.meter_pcm_data,
                ComSatSoundIOSlot::kPlaybackPcmScratchBytes);
    UpdatePlaybackDecoderResyncState(*slot);

    const bool processing_enabled = slot->playback_enabled || slot->playback_metering_enabled;
    if (processing_enabled && slot->playback_decoder_resync_pending) {
      slot->playback_decoder_resync_pending = false;
      slot->playback_decoder_resync_anchor =
          static_cast<std::uint16_t>(slot->playback_frame_sequence);
    }

    const bool frame_ready = frame.decoder_step_succeeded && frame.frame_ready;
    if (processing_enabled && !frame_ready) {
      slot->playback_decoder_resync_pending = true;
    }

    if (slot->playback_metering_enabled && state.playback_observer != nullptr) {
      state.playback_observer->OnTalkerPlaybackMeter(
          talker_id,
          MeasurePlaybackPeakToPeakLinear(slot->playback_pcm_scratch.data(),
                                          ComSatSoundIOSlot::kPlaybackPcmScratchBytes),
          frame_ready);
    }

    if (slot->playback_enabled && frame_ready) {
      if (state.OutputChannel() != nullptr) {
        const std::size_t slot_index = ResolveTalkerSlotIndex(talker_id);
        float effective_volume = state.master_volume * slot->volume;
        if (frame.session_index < slot->bound_sessions.size()) {
          const auto binding = slot->session_bindings.find(
              slot->bound_sessions[frame.session_index]);
          if (binding != slot->session_bindings.end()) {
            effective_volume *=
                binding->second.session_gain * binding->second.volume_scale;
          }
        }
        state.OutputChannel()->SetChannelVolume(
            static_cast<std::uint32_t>(slot_index),
            std::clamp(effective_volume, 0.0f, 1.0f));
        (void)state.OutputChannel()->WriteAudioData(
            static_cast<std::uint32_t>(slot_index),
            reinterpret_cast<const std::int16_t *>(
                slot->playback_pcm_scratch.data()),
            static_cast<std::uint32_t>(
                slot->playback_pcm_scratch.size() / sizeof(std::int16_t)),
            1.0);
      }
      if (state.playback_observer != nullptr && frame.queued_frame_data != nullptr &&
          frame.queued_frame_byte_count != 0u) {
        state.playback_observer->OnTalkerPlaybackBufferQueued(
            talker_id, frame.session_index, frame.queued_frame_data, frame.queued_frame_byte_count,
            kComSatPlaybackFrameDurationSeconds);
      }
      slot->playback_queue_active = true;
    } else if (slot->playback_queue_active) {
      if (state.OutputChannel() != nullptr) {
        state.OutputChannel()->ResetChannel(
            static_cast<std::uint32_t>(ResolveTalkerSlotIndex(talker_id)));
      }
      if (state.playback_observer != nullptr) {
        state.playback_observer->OnTalkerPlaybackDisabled(talker_id);
      }
      slot->playback_queue_active = false;
    }

    ++slot->playback_frame_sequence;
  }

  return processed_frame_count;
}

void ComSatSoundIO_ProcessPendingTalkerPlayback(ComSatSoundIOState &state,
                                                const ComSatPendingTalkerPlayback *pending_playback,
                                                const std::size_t pending_playback_count) {
  if (pending_playback == nullptr) {
    return;
  }

  for (std::size_t index = 0; index < pending_playback_count; ++index) {
    const ComSatPendingTalkerPlayback &pending_entry = pending_playback[index];
    if (!pending_entry.active) {
      continue;
    }

    (void)ComSatSoundIO_ProcessTalkerPlaybackFrames(
        state, pending_entry.talker_id, pending_entry.frames, pending_entry.frame_count);
  }
}

std::int32_t ComSatSoundIO_AdvancePlayback(ComSatSoundIOState &state,
                                           ComSatSoundIOPlaybackRuntime &runtime,
                                           const std::uint32_t current_tick_ms,
                                           ComSatSoundIOTalkerFrameAdvancer *const advancer) {
  const std::uint32_t elapsed_ticks = current_tick_ms - runtime.previous_tick_ms;
  const double elapsed_frames =
      static_cast<double>(elapsed_ticks) * 0.001 * static_cast<double>(runtime.playback_speed);

  const std::int32_t window = state.playback_update_window;
  const std::int32_t frames_to_generate = static_cast<std::int32_t>(elapsed_frames) + 1;

  std::int32_t pending =
      window - runtime.current_frame + static_cast<std::int32_t>(elapsed_frames);
  if (pending > window) {
    pending = window;
  }

  std::int32_t frame_index = frames_to_generate + window - pending;

  std::int32_t steps_executed = 0;
  while (pending > 0) {
    if (advancer != nullptr) {
      for (auto &slot : state.slots) {
        if (!slot.allocated || !slot.active) {
          continue;
        }
        advancer->StepTalkerFrame(state, slot.talker_id, frame_index);
      }
    }

    for (auto &[session_id, session_state] : state.sessions) {
      for (auto &member : session_state.detached_members) {
        ComSatSoundIO_AdvanceMemberRingBuffer(member);
      }
    }

    runtime.current_frame = frame_index;
    ++frame_index;
    --pending;
    ++steps_executed;
  }

  return steps_executed;
}

void ComSatSoundIO_AdvanceMemberRingBuffer(ComSatSoundIOSessionMember &member) {
  const auto ring_size = static_cast<std::int32_t>(member.playback_frames.size());
  if (ring_size == 0) {
    return;
  }

  const auto &history = member.playback_volume_history;
  const auto sample_count = static_cast<std::int32_t>(history.samples.size());
  if (sample_count == 0) {
    return;
  }

  const double average = history.sum / static_cast<double>(sample_count);
  if (average == 0.0) {
    return;
  }

  const double advance = 100.0 / average;
  const std::int32_t old_pos = static_cast<std::int32_t>(member.playback_block_cursor);

  const double new_cursor = member.playback_block_cursor + advance;
  member.playback_block_cursor = std::fmod(new_cursor, static_cast<double>(ring_size));

  const std::int32_t new_pos = static_cast<std::int32_t>(new_cursor) % ring_size;
  if (old_pos != new_pos) {
    std::int32_t pos = old_pos;
    do {
      member.playback_frames[static_cast<std::size_t>(pos)].ClearOccupiedFlag();
      pos = (pos + 1) % ring_size;
    } while (pos != new_pos);
  }
}

static bool IsPositionInPlaybackWindow(const std::int32_t pos, const std::int32_t cursor,
                                       const std::int32_t window,
                                       const std::int32_t ring_size) {
  const std::int32_t forward_space = ring_size - cursor;

  if (pos >= cursor) {
    if (forward_space < window) {
      return true;
    }
    return pos < cursor + window;
  }

  if (forward_space >= window) {
    return false;
  }
  return pos < cursor + window - ring_size;
}

void ComSatSoundIO_ShiftMemberRingBuffer(ComSatSoundIOSessionMember &member,
                                         const std::int32_t shift_amount,
                                         const std::int32_t playback_update_window) {
  const auto ring_size = static_cast<std::int32_t>(member.playback_frames.size());
  if (ring_size <= 0) {
    return;
  }

  const auto temp = member.playback_frames;

  for (auto &frame : member.playback_frames) {
    frame.ClearOccupiedFlag();
  }

  const std::int32_t cursor =
      static_cast<std::int32_t>(member.playback_block_cursor);

  for (std::int32_t i = 0; i < ring_size; ++i) {
    if (!temp[static_cast<std::size_t>(i)].occupied()) {
      continue;
    }

    std::int32_t new_pos = (shift_amount + i) % ring_size;
    if (new_pos < 0) {
      new_pos += ring_size;
    }

    if (IsPositionInPlaybackWindow(new_pos, cursor, playback_update_window,
                                   ring_size)) {
      continue;
    }

    member.playback_frames[static_cast<std::size_t>(new_pos)] =
        temp[static_cast<std::size_t>(i)];
  }
}

bool ComSatSoundIO_MutateTalkerBoundSessionSet(ComSatSoundIOState &state,
                                               const std::uint32_t talker_id,
                                               const std::uint32_t session_id_low,
                                               const std::uint32_t session_id_high,
                                               const ComSatTalkerSessionSetMutation mutation) {
  ComSatSoundIOSlot *slot = FindActiveTalkerSlot(state, talker_id);
  if (slot == nullptr) {
    return false;
  }

  return MutateSessionSet(slot->bound_sessions,
                          ComposeComSatIdentifier(session_id_low, session_id_high), mutation);
}

bool ComSatSoundIO_HasTalkerBoundSession(ComSatSoundIOState &state, const std::uint32_t talker_id,
                                         const std::uint32_t session_id_low,
                                         const std::uint32_t session_id_high) {
  const ComSatSoundIOSlot *slot = FindActiveTalkerSlot(state, talker_id);
  if (slot == nullptr) {
    return false;
  }

  return SlotContainsSession(*slot, ComposeComSatIdentifier(session_id_low, session_id_high));
}

bool ComSatSoundIO_MutateTalkerSecondarySessionSet(ComSatSoundIOState &state,
                                                   const std::uint32_t talker_id,
                                                   const std::uint32_t session_id_low,
                                                   const std::uint32_t session_id_high,
                                                   const ComSatTalkerSessionSetMutation mutation) {
  ComSatSoundIOSlot *slot = FindActiveTalkerSlot(state, talker_id);
  if (slot == nullptr) {
    return false;
  }

  return MutateSessionSet(slot->secondary_session_ids,
                          ComposeComSatIdentifier(session_id_low, session_id_high), mutation);
}

bool ComSatSoundIO_HasTalkerSecondarySession(ComSatSoundIOState &state,
                                             const std::uint32_t talker_id,
                                             const std::uint32_t session_id_low,
                                             const std::uint32_t session_id_high) {
  const ComSatSoundIOSlot *slot = FindActiveTalkerSlot(state, talker_id);
  if (slot == nullptr) {
    return false;
  }

  return SlotContainsSecondarySession(*slot,
                                      ComposeComSatIdentifier(session_id_low, session_id_high));
}

bool ComSatSoundIO_EncodePackedVoiceBatch(const ComSatVariableBitrateVoiceBatch &batch,
                                          std::uint8_t *buffer, const std::size_t buffer_size,
                                          std::size_t &bytes_written) {
  bytes_written = 0;
  if (buffer == nullptr || batch.frames.empty() || buffer_size < sizeof(batch.sequence)) {
    return false;
  }

  buffer[0] = static_cast<std::uint8_t>(batch.sequence & 0xFFu);
  buffer[1] = static_cast<std::uint8_t>(batch.sequence >> 8u);
  std::memset(buffer + sizeof(batch.sequence), 0, buffer_size - sizeof(batch.sequence));

  LittleEndianBitPacker packer(buffer + sizeof(batch.sequence),
                               buffer_size - sizeof(batch.sequence));
  for (const auto &frame : batch.frames) {
    if (frame.bit_count > frame.payload.size() * 8u) {
      bytes_written = 0;
      return false;
    }

    if (!packer.Append(frame.payload.data(), frame.bit_count)) {
      bytes_written = 0;
      return false;
    }
  }

  bytes_written = sizeof(batch.sequence) + packer.Finalize();
  return true;
}

bool ComSatSoundIO_EncodeVariableBitrateVoiceBatch(const ComSatVariableBitrateVoiceBatch &batch,
                                                   std::uint8_t *buffer,
                                                   const std::size_t buffer_size,
                                                   std::size_t &bytes_written) {
  bytes_written = 0;
  if (buffer == nullptr || batch.frames.empty() || batch.frames.size() > 0x7Fu) {
    return false;
  }

  const std::size_t header_size = 3u + batch.frames.size();
  if (buffer_size < header_size) {
    return false;
  }

  buffer[0] = static_cast<std::uint8_t>(batch.sequence & 0xFFu);
  buffer[1] = static_cast<std::uint8_t>(batch.sequence >> 8u);
  buffer[2] = static_cast<std::uint8_t>(batch.frames.size());
  if (batch.accumulated_duration_ms < kComSatVariableBitrateCompleteBatchMs) {
    buffer[2] = static_cast<std::uint8_t>(buffer[2] | 0x80u);
  }

  for (std::size_t i = 0; i < batch.frames.size(); ++i) {
    const auto bit_count = batch.frames[i].bit_count;
    if (bit_count > batch.frames[i].payload.size() * 8u) {
      bytes_written = 0;
      return false;
    }

    const int delta_bits =
        static_cast<int>(bit_count) - static_cast<int>(kComSatVariableBitrateBaselineBits);
    buffer[3u + i] = static_cast<std::uint8_t>(std::clamp(delta_bits, 0, 0xFF));
  }

  std::memset(buffer + header_size, 0, buffer_size - header_size);
  LittleEndianBitPacker packer(buffer + header_size, buffer_size - header_size);
  for (const auto &frame : batch.frames) {
    if (!packer.Append(frame.payload.data(), frame.bit_count)) {
      bytes_written = 0;
      return false;
    }
  }

  bytes_written = header_size + packer.Finalize();
  return true;
}

bool ComSatSoundIO_EncodeFixed960BitVoiceBatch(const ComSatVariableBitrateVoiceBatch &batch,
                                               std::uint8_t *buffer, const std::size_t buffer_size,
                                               std::size_t &bytes_written) {
  bytes_written = 0;
  if (buffer == nullptr || batch.frames.empty()) {
    return false;
  }

  const std::size_t trailing_partial_flag_bytes =
      batch.accumulated_duration_ms < kComSatVariableBitrateCompleteBatchMs ? 1u : 0u;
  const std::size_t required_size = sizeof(batch.sequence) +
                                    (batch.frames.size() * kComSatFixedWidthVoiceFrameBytes) +
                                    trailing_partial_flag_bytes;
  if (buffer_size < required_size) {
    return false;
  }

  buffer[0] = static_cast<std::uint8_t>(batch.sequence & 0xFFu);
  buffer[1] = static_cast<std::uint8_t>(batch.sequence >> 8u);

  std::size_t offset = sizeof(batch.sequence);
  for (const auto &frame : batch.frames) {
    std::memcpy(buffer + offset, frame.payload.data(), kComSatFixedWidthVoiceFrameBytes);
    offset += kComSatFixedWidthVoiceFrameBytes;
  }

  if (trailing_partial_flag_bytes != 0u) {
    buffer[offset++] = 0u;
  }

  bytes_written = offset;
  return true;
}

bool ComSatSoundIO_DecodePackedVoiceBatch(const std::uint8_t *buffer,
                                          const std::size_t buffer_size,
                                          ComSatVariableBitrateVoiceBatch &batch) {
  batch.sequence = 0;
  batch.accumulated_duration_ms = 0;
  batch.frames.clear();

  constexpr std::size_t kHeaderBytes = 2u;
  if (buffer == nullptr || buffer_size < kHeaderBytes) {
    return false;
  }

  batch.sequence =
      static_cast<std::uint16_t>(buffer[0] | (static_cast<std::uint16_t>(buffer[1]) << 8u));

  const std::size_t payload_bits = (buffer_size - kHeaderBytes) * 8u;
  if (payload_bits == 0u) {
    return true;
  }

  if (payload_bits > ComSatVariableBitrateVoiceFrame::kMaxPayloadBytes * 8u) {
    return false;
  }

  ComSatVariableBitrateVoiceFrame frame{};
  frame.bit_count = static_cast<std::uint16_t>(payload_bits);

  LittleEndianBitReader reader(buffer + kHeaderBytes, buffer_size - kHeaderBytes);
  if (!reader.Read(frame.payload.data(), payload_bits)) {
    return false;
  }

  batch.frames.push_back(frame);
  return true;
}

bool ComSatSoundIO_FlushPendingVoiceBatch(ComSatPendingVoiceBatch &pending_batch) {
  if (pending_batch.batch.frames.empty()) {
    return false;
  }

  if (pending_batch.sender == nullptr) {
    ClearPendingVoiceFrames(pending_batch);
    return true;
  }

  std::array<std::uint8_t, kComSatVoiceBatchPacketCapacityBytes> packet{};
  std::size_t bytes_written = 0;
  bool encoded = false;

  switch (pending_batch.encoding_mode) {
  case ComSatVoiceBatchEncodingMode::kBitPacked:
    encoded = ComSatSoundIO_EncodePackedVoiceBatch(pending_batch.batch, packet.data(),
                                                   packet.size(), bytes_written);
    break;
  case ComSatVoiceBatchEncodingMode::kVariableBitrate:
    encoded = ComSatSoundIO_EncodeVariableBitrateVoiceBatch(pending_batch.batch, packet.data(),
                                                            packet.size(), bytes_written);
    break;
  case ComSatVoiceBatchEncodingMode::kFixed960BitFrames:
    encoded = ComSatSoundIO_EncodeFixed960BitVoiceBatch(pending_batch.batch, packet.data(),
                                                        packet.size(), bytes_written);
    break;
  }

  if (!encoded) {
    return false;
  }

  pending_batch.sender->Send(pending_batch.destination_id_low, pending_batch.destination_id_high,
                             packet.data(), bytes_written);
  ClearPendingVoiceFrames(pending_batch);
  return true;
}

bool ComSatSoundIO_SetSocketQOS(const std::uintptr_t socket_handle) {
#ifdef _WIN32
  HMODULE library = LoadLibraryA("ws2_32.dll");
  if (!library) {
    return false;
  }

  using WSAIoctlFn = int(WSAAPI *)(SOCKET, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD,
                                   LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
  const auto qos_ioctl = reinterpret_cast<WSAIoctlFn>(GetProcAddress(library, "WSAIoctl"));
  if (!qos_ioctl) {
    FreeLibrary(library);
    return false;
  }

  DWORD bytes_returned = 0;
  DWORD zero = 0;
  qos_ioctl(NativeToSocketHandle(socket_handle), 0x98000004u, &zero, sizeof(zero), nullptr, 0,
            &bytes_returned, nullptr, nullptr);
  FreeLibrary(library);
  return true;
#else
  (void)socket_handle;
  return false;
#endif
}

ComSatDatagramEndpoint ComSatDatagramEndpoint::Ipv4Any(const std::uint16_t host_port) {
  ComSatDatagramEndpoint endpoint;
  ResetIpv4Endpoint(endpoint, htonl(INADDR_ANY), host_port);
  return endpoint;
}

ComSatDatagramEndpoint ComSatDatagramEndpoint::Ipv4Loopback(const std::uint16_t host_port) {
  ComSatDatagramEndpoint endpoint;
  ResetIpv4Endpoint(endpoint, htonl(INADDR_LOOPBACK), host_port);
  return endpoint;
}

std::uint16_t ComSatDatagramEndpoint::PortHostOrder() const {
  return ntohs(EndpointSockAddrIn(*this)->sin_port);
}

std::uint32_t ComSatDatagramEndpoint::AddressV4NetworkOrder() const {
  return EndpointSockAddrIn(*this)->sin_addr.s_addr;
}

ComSatDatagramSocket::ComSatDatagramSocket() {
  native_socket_handle_ = SocketHandleToNative(::socket(AF_INET, SOCK_DGRAM, 0));
  if (!IsOpen()) {
    return;
  }

  ComSatSoundIO_SetSocketQOS(native_socket_handle_);
  SetSocketNonBlocking(NativeToSocketHandle(native_socket_handle_));
  SetSocketReceiveBufferSize(NativeToSocketHandle(native_socket_handle_), 0x8000);
}

ComSatDatagramSocket::~ComSatDatagramSocket() {
  Close();
}

ComSatDatagramSocket::ComSatDatagramSocket(ComSatDatagramSocket &&other) noexcept
    : native_socket_handle_(other.native_socket_handle_) {
  other.native_socket_handle_ = kInvalidSocketHandle;
}

ComSatDatagramSocket &ComSatDatagramSocket::operator=(ComSatDatagramSocket &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  Close();
  native_socket_handle_ = other.native_socket_handle_;
  other.native_socket_handle_ = kInvalidSocketHandle;
  return *this;
}

bool ComSatDatagramSocket::Bind(const std::uint16_t host_port) {
  if (!IsOpen()) {
    return false;
  }

  const int reuse_address = 1;
  setsockopt(NativeToSocketHandle(native_socket_handle_), SOL_SOCKET, SO_REUSEADDR,
#ifdef _WIN32
             reinterpret_cast<const char *>(&reuse_address),
#else
             &reuse_address,
#endif
             sizeof(reuse_address));

  auto endpoint = ComSatDatagramEndpoint::Ipv4Any(host_port);
  return ::bind(NativeToSocketHandle(native_socket_handle_), EndpointSockAddr(endpoint),
                static_cast<socklen_t>(endpoint.length)) != -1;
}

int ComSatDatagramSocket::SendTo(const ComSatDatagramEndpoint &endpoint, const char *buffer,
                                 const int length) const {
  if (!IsOpen()) {
    return -1;
  }

  return static_cast<int>(::sendto(NativeToSocketHandle(native_socket_handle_), buffer, length, 0,
                                   EndpointSockAddr(endpoint),
                                   static_cast<socklen_t>(endpoint.length)));
}

bool ComSatDatagramSocket::ReceiveFrom(ComSatDatagramEndpoint &endpoint, char *buffer,
                                       std::size_t &in_out_length) const {
  if (!buffer) {
    return false;
  }

  std::memset(buffer, 0, in_out_length);
  if (!IsOpen()) {
    return false;
  }

  socklen_t from_length = static_cast<socklen_t>(std::min<std::size_t>(
      endpoint.length == 0 ? endpoint.storage.size() : endpoint.length, endpoint.storage.size()));
  const auto received =
      ::recvfrom(NativeToSocketHandle(native_socket_handle_), buffer,
                 static_cast<int>(std::min<std::size_t>(
                     in_out_length, static_cast<std::size_t>(std::numeric_limits<int>::max()))),
                 0, EndpointSockAddr(endpoint), &from_length);
  if (received <= 0) {
    return false;
  }

  endpoint.length = from_length;
  in_out_length = static_cast<std::size_t>(received);
  return true;
}

bool ComSatDatagramSocket::IsOpen() const noexcept {
  return native_socket_handle_ != kInvalidSocketHandle;
}

void ComSatDatagramSocket::Close() noexcept {
  if (!IsOpen()) {
    return;
  }

  CloseSocketHandle(NativeToSocketHandle(native_socket_handle_));
  native_socket_handle_ = kInvalidSocketHandle;
}

std::unique_ptr<ComSatDatagramSocket> ComSatSoundIO_CreateSocketWrapper() {
  return std::unique_ptr<ComSatDatagramSocket>(new (std::nothrow) ComSatDatagramSocket());
}

}
