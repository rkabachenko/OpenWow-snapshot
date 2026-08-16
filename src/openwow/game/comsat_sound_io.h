
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

class ComSatSoundIOBridge;
class ComSatSoundOutputChannel;
struct ComSatSoundIOState;

class ComSatSoundIOPlaybackObserver {
public:
  virtual ~ComSatSoundIOPlaybackObserver() = default;

  virtual void OnTalkerPlaybackDisabled(std::uint32_t talker_id) = 0;
  virtual void OnTalkerPlaybackMeter(std::uint32_t talker_id, float linear_peak_to_peak,
                                     bool frame_ready) {
    (void)talker_id;
    (void)linear_peak_to_peak;
    (void)frame_ready;
  }
  virtual void OnTalkerPlaybackSilencePrepared(std::uint32_t talker_id,
                                               const std::uint8_t *pcm_data,
                                               std::uint32_t byte_count) {
    (void)talker_id;
    (void)pcm_data;
    (void)byte_count;
  }
  virtual void OnTalkerPlaybackBufferQueued(std::uint32_t talker_id, std::uint32_t session_index,
                                            const std::uint8_t *pcm_data, std::uint32_t byte_count,
                                            double frame_duration_seconds) {
    (void)talker_id;
    (void)session_index;
    (void)pcm_data;
    (void)byte_count;
    (void)frame_duration_seconds;
  }
};

class ComSatSoundIOUpdateStage {
public:
  virtual ~ComSatSoundIOUpdateStage() = default;

  virtual void Update(std::uint32_t tick_count_ms) = 0;
};

class ComSatSoundIOTalkerFrameAdvancer {
public:
  virtual ~ComSatSoundIOTalkerFrameAdvancer() = default;

  virtual void StepTalkerFrame(ComSatSoundIOState &state, std::uint32_t talker_id,
                               std::int32_t frame_index) = 0;
};

struct ComSatSoundIOPlaybackRuntime {
  std::uint32_t previous_tick_ms{0};
  float playback_speed{10.0f};
  std::int32_t current_frame{0};
};

class ComSatSoundIOFrameCoordinator {
public:
  ComSatSoundIOFrameCoordinator(ComSatSoundIOUpdateStage &receive_stage,
                                ComSatSoundIOUpdateStage &playback_stage) noexcept;

  void Update(std::uint32_t tick_count_ms);

private:
  ComSatSoundIOUpdateStage &receive_stage_;
  ComSatSoundIOUpdateStage &playback_stage_;
};

struct ComSatPendingTalkerPlayback;

class ComSatSoundIOPlaybackUpdateStage final : public ComSatSoundIOUpdateStage {
public:
  ComSatSoundIOPlaybackUpdateStage(ComSatSoundIOState &state,
                                   ComSatSoundIOPlaybackRuntime &runtime,
                                   ComSatSoundIOTalkerFrameAdvancer *advancer = nullptr) noexcept;

  void SetChainedStage(ComSatSoundIOUpdateStage *stage) noexcept;

  void SetPendingPlayback(const ComSatPendingTalkerPlayback *entries,
                          std::size_t count) noexcept;

  void Update(std::uint32_t tick_count_ms) override;

private:
  ComSatSoundIOState &state_;
  ComSatSoundIOPlaybackRuntime &runtime_;
  ComSatSoundIOTalkerFrameAdvancer *advancer_;
  ComSatSoundIOUpdateStage *chained_stage_{nullptr};
  const ComSatPendingTalkerPlayback *pending_playback_{nullptr};
  std::size_t pending_playback_count_{0};
};

struct ComSatSoundIOSlot {
  static constexpr std::size_t kPlaybackPcmScratchBytes = 1600u;

  struct SessionBinding {
    std::uint8_t member_status_flags{0u};
    std::uint8_t selection_value{0x80u};
    float session_gain{1.0f};
    float volume_scale{1.0f};
  };

  bool allocated{false};
  bool active{false};
  bool playback_enabled{false};
  bool playback_metering_enabled{false};
  bool playback_queue_active{false};
  bool playback_decoder_resync_pending{false};
  bool voice_batch_open{false};
  bool voice_data_received{false};
  float volume{1.0f};
  std::uint32_t talker_id{0};
  std::uint32_t priority{3};
  std::uint64_t member_id{0};
  std::uint64_t stream_source_id{0};
  std::uint64_t transport_session_id{0};
  std::uint16_t playback_decoder_resync_anchor{0};
  std::uint16_t pending_voice_sequence{0};
  std::uint32_t playback_frame_sequence{0};
  std::uint32_t pending_voice_frame_count{0};
  std::uint32_t pending_voice_duration_ms{0};
  std::string sound_label;
  std::array<std::uint8_t, kPlaybackPcmScratchBytes> playback_pcm_scratch{};
  std::vector<std::uint64_t> bound_sessions;
  std::vector<std::uint64_t> secondary_session_ids;
  std::unordered_map<std::uint64_t, SessionBinding> session_bindings;

  void Reset(float slot_volume);
};

struct ComSatSoundIOSessionMember {
  struct PlaybackFrame {
    static constexpr std::size_t kSerializedBytes = 0x90u;
    static constexpr std::size_t kOccupiedFlagOffset = 132u;

    std::array<std::uint8_t, kSerializedBytes> bytes{};

    [[nodiscard]] bool occupied() const noexcept { return bytes[kOccupiedFlagOffset] != 0u; }
    void ClearOccupiedFlag() noexcept { bytes[kOccupiedFlagOffset] = 0u; }
  };

  struct PlaybackVolumeHistory {
    std::vector<double> samples;
    double sum{0.0};
  };

  std::uint64_t member_id{0};
  std::uint8_t status_flags{0u};
  std::uint8_t talker_slot_index{0xFFu};
  std::uint8_t selection_value{0x80u};
  float volume_scale{1.0f};
  double playback_block_cursor{0.0};
  PlaybackVolumeHistory playback_volume_history;
  std::vector<PlaybackFrame> playback_frames;
};

struct ComSatSoundIOSessionState {
  std::uint16_t transport_routing_id{0};
  std::vector<std::uint64_t> transport_member_ids;
  std::unordered_map<std::uint64_t, std::uint8_t> member_selection_values;
  std::vector<ComSatSoundIOSessionMember> detached_members;
};

struct ComSatSoundIOState {
  ComSatSoundIOState();
  ~ComSatSoundIOState();
  ComSatSoundIOState(ComSatSoundIOState &&) noexcept;
  ComSatSoundIOState &operator=(ComSatSoundIOState &&) noexcept;
  ComSatSoundIOState(const ComSatSoundIOState &) = delete;
  ComSatSoundIOState &operator=(const ComSatSoundIOState &) = delete;

  bool initialized{false};
  bool capture_enabled{false};
  std::uint32_t active_slots{0};
  float master_volume{1.0f};
  std::int32_t playback_update_window{5};
  std::int32_t playback_update_window_shadow{5};
  std::vector<ComSatSoundIOSlot> slots;
  std::unordered_map<std::uint64_t, ComSatSoundIOSessionState> sessions;
  ComSatSoundIOPlaybackObserver *playback_observer{nullptr};
  [[nodiscard]] ComSatSoundOutputChannel *OutputChannel() const noexcept {
    return output_channel_;
  }

private:
  void ResetOwnedState(bool preserve_bridge, bool preserve_observer);

  std::unique_ptr<ComSatSoundIOBridge> bridge_;
  ComSatSoundOutputChannel *output_channel_{nullptr};

  friend void ComSatSoundIO_Initialize(ComSatSoundIOState &state, std::uint32_t max_players,
                                       float master_vol);
  friend void ComSatSoundIO_DestroySlot(ComSatSoundIOState &state, std::uint32_t slot_index);
  friend std::int32_t ComSatSoundIO_GetVoiceOutputDriverIndex(const ComSatSoundIOState &state);
  friend std::int32_t ComSatSoundIO_GetVoiceInputDriverIndex(const ComSatSoundIOState &state);
  friend bool ComSatSoundIO_SetVoiceOutputDriverIndex(ComSatSoundIOState &state,
                                                      std::int32_t index);
  friend bool ComSatSoundIO_SetVoiceInputDriverIndex(ComSatSoundIOState &state,
                                                     std::int32_t index);
  friend void ComSatSoundIO_SetOutputChannel(ComSatSoundIOState &state,
                                             ComSatSoundOutputChannel *channel) noexcept;
};

enum class ComSatTalkerSessionSetMutation : std::uint8_t {
  AddIfMissing = 0,
  Remove = 1,
  Toggle = 2,
};

struct ComSatVariableBitrateVoiceFrame {
  static constexpr std::size_t kMaxPayloadBytes = 128;

  std::uint16_t bit_count{0};
  std::array<std::uint8_t, kMaxPayloadBytes> payload{};
};

struct ComSatVariableBitrateVoiceBatch {
  std::uint16_t sequence{0};
  std::uint32_t accumulated_duration_ms{0};
  std::vector<ComSatVariableBitrateVoiceFrame> frames;
};

enum class ComSatVoiceBatchEncodingMode : std::uint8_t {
  kBitPacked = 0,
  kVariableBitrate = 1,
  kFixed960BitFrames = 2,
};

class ComSatVoiceBatchSender {
public:
  virtual ~ComSatVoiceBatchSender() = default;

  virtual void Send(std::uint32_t destination_id_low, std::uint32_t destination_id_high,
                    const std::uint8_t *buffer, std::size_t buffer_size) = 0;
};

struct ComSatPendingVoiceBatch {
  std::uint32_t destination_id_low{0};
  std::uint32_t destination_id_high{0};
  ComSatVoiceBatchSender *sender{nullptr};
  ComSatVoiceBatchEncodingMode encoding_mode{ComSatVoiceBatchEncodingMode::kBitPacked};
  ComSatVariableBitrateVoiceBatch batch;
};

struct ComSatTalkerPlaybackFrame {
  std::uint32_t session_index{0};
  const std::uint8_t *meter_pcm_data{nullptr};
  std::uint32_t meter_pcm_byte_count{0};
  const std::uint8_t *queued_frame_data{nullptr};
  std::uint32_t queued_frame_byte_count{0};
  bool decoder_step_succeeded{false};
  bool frame_ready{false};
};

struct ComSatPendingTalkerPlayback {
  bool active{false};
  std::uint32_t talker_id{0};
  const ComSatTalkerPlaybackFrame *frames{nullptr};
  std::size_t frame_count{0};
};

struct ComSatDatagramEndpoint {
  static constexpr std::uint32_t kIpv4SockaddrLength = 16;
  static constexpr std::size_t kStorageSize = 128;

  std::uint32_t length{kIpv4SockaddrLength};
  std::array<std::byte, kStorageSize> storage{};

  [[nodiscard]] static ComSatDatagramEndpoint Ipv4Any(std::uint16_t host_port);
  [[nodiscard]] static ComSatDatagramEndpoint Ipv4Loopback(std::uint16_t host_port);

  [[nodiscard]] std::uint16_t PortHostOrder() const;
  [[nodiscard]] std::uint32_t AddressV4NetworkOrder() const;
};

class ComSatDatagramSocket {
public:
  using NativeHandle = std::uintptr_t;

  ComSatDatagramSocket();
  ~ComSatDatagramSocket();

  ComSatDatagramSocket(const ComSatDatagramSocket &) = delete;
  ComSatDatagramSocket &operator=(const ComSatDatagramSocket &) = delete;
  ComSatDatagramSocket(ComSatDatagramSocket &&other) noexcept;
  ComSatDatagramSocket &operator=(ComSatDatagramSocket &&other) noexcept;

  [[nodiscard]] bool Bind(std::uint16_t host_port);
  [[nodiscard]] int SendTo(const ComSatDatagramEndpoint &endpoint, const char *buffer,
                           int length) const;
  [[nodiscard]] bool ReceiveFrom(ComSatDatagramEndpoint &endpoint, char *buffer,
                                 std::size_t &in_out_length) const;
  [[nodiscard]] NativeHandle native_handle() const noexcept {
    return native_socket_handle_;
  }
  [[nodiscard]] bool IsOpen() const noexcept;

private:
  static constexpr NativeHandle kInvalidSocketHandle = static_cast<NativeHandle>(-1);

  void Close() noexcept;

  NativeHandle native_socket_handle_{kInvalidSocketHandle};
};

void ComSatSoundIO_Initialize(ComSatSoundIOState &state, std::uint32_t max_players,
                              float master_vol);

[[nodiscard]] std::int32_t ComSatSoundIO_GetVoiceOutputDriverIndex(const ComSatSoundIOState &state);

[[nodiscard]] std::int32_t ComSatSoundIO_GetVoiceInputDriverIndex(const ComSatSoundIOState &state);

[[nodiscard]] bool ComSatSoundIO_SetVoiceOutputDriverIndex(ComSatSoundIOState &state,
                                                           std::int32_t index);

[[nodiscard]] bool ComSatSoundIO_SetVoiceInputDriverIndex(ComSatSoundIOState &state,
                                                          std::int32_t index);

void ComSatSoundIO_SetOutputChannel(ComSatSoundIOState &state,
                                    ComSatSoundOutputChannel *channel) noexcept;

void ComSatSoundIO_MeasurePcmLevel(const std::uint8_t *pcm_data, std::uint32_t byte_count,
                                   float &out_linear_peak_to_peak, float &out_db_level);

void ComSatSoundIO_DestroySlot(ComSatSoundIOState &state, std::uint32_t slot_index);

void ComSatSoundIO_SetPlaybackObserver(ComSatSoundIOState &state,
                                       ComSatSoundIOPlaybackObserver *observer) noexcept;

[[nodiscard]] std::int32_t ComSatSoundIO_AdjustPlaybackUpdateWindow(ComSatSoundIOState &state,
                                                                    std::int32_t delta);

void ComSatSoundIO_RegisterSession(ComSatSoundIOState &state, std::uint32_t session_id_low,
                                   std::uint32_t session_id_high);
void ComSatSoundIO_UnregisterSession(ComSatSoundIOState &state, std::uint32_t session_id_low,
                                     std::uint32_t session_id_high);

[[nodiscard]] bool ComSatSoundIO_SetSessionTransportRoutingId(
    ComSatSoundIOState &state, std::uint32_t session_id_low, std::uint32_t session_id_high,
    std::uint16_t transport_routing_id);

[[nodiscard]] bool ComSatSoundIO_BindSessionMemberToTalkerSlot(
    ComSatSoundIOState &state, std::uint32_t session_id_low, std::uint32_t session_id_high,
    std::uint32_t talker_id, std::uint32_t member_id_low, std::uint32_t member_id_high);

[[nodiscard]] bool ComSatSoundIO_SetTalkerTransportSession(
    ComSatSoundIOState &state, std::uint32_t talker_id, std::uint32_t session_id_low,
    std::uint32_t session_id_high);

[[nodiscard]] bool ComSatSoundIO_AddSessionMember(ComSatSoundIOState &state,
                                                  std::uint32_t session_id_low,
                                                  std::uint32_t session_id_high,
                                                  std::uint32_t member_id_low,
                                                  std::uint32_t member_id_high);
[[nodiscard]] bool ComSatSoundIO_SetBoundSessionMemberStatusFlags(
    ComSatSoundIOState &state, std::uint32_t session_id_low, std::uint32_t session_id_high,
    std::uint32_t member_id_low, std::uint32_t member_id_high, std::uint8_t status_flags);
[[nodiscard]] bool ComSatSoundIO_SetDetachedSessionMemberStatusFlags(
    ComSatSoundIOState &state, std::uint32_t session_id_low, std::uint32_t session_id_high,
    std::uint32_t member_id_low, std::uint32_t member_id_high, std::uint8_t status_flags);
[[nodiscard]] bool ComSatSoundIO_SetSessionMemberSelectionValue(
    ComSatSoundIOState &state, std::uint32_t session_id_low, std::uint32_t session_id_high,
    std::uint32_t member_id_low, std::uint32_t member_id_high, std::uint8_t selection_value);
[[nodiscard]] bool ComSatSoundIO_RemoveSessionMember(ComSatSoundIOState &state,
                                                     std::uint32_t session_id_low,
                                                     std::uint32_t session_id_high,
                                                     std::uint32_t member_id_low,
                                                     std::uint32_t member_id_high);
[[nodiscard]] int ComSatSoundIO_GetSessionMemberCount(ComSatSoundIOState &state,
                                                      std::uint32_t session_id_low,
                                                      std::uint32_t session_id_high);
[[nodiscard]] bool
ComSatSoundIO_GetSessionMemberByIndex(ComSatSoundIOState &state, std::uint32_t session_id_low,
                                      std::uint32_t session_id_high, std::uint32_t member_index,
                                      std::uint32_t &member_id_low, std::uint32_t &member_id_high,
                                      std::uint8_t &talker_slot_index);
[[nodiscard]] bool
ComSatSoundIO_CreateTalkerSlot(ComSatSoundIOState &state, std::uint32_t talker_id,
                               std::uint32_t member_id_low, std::uint32_t member_id_high,
                               float initial_volume, std::uint32_t stream_source_low,
                               std::uint32_t stream_source_high);
[[nodiscard]] bool ComSatSoundIO_DestroyTalkerSlot(ComSatSoundIOState &state,
                                                   std::uint32_t talker_id);
[[nodiscard]] bool ComSatSoundIO_SetTalkerPlaybackEnabled(ComSatSoundIOState &state,
                                                          std::uint32_t talker_id, bool enabled);
[[nodiscard]] bool ComSatSoundIO_SetTalkerPlaybackMeteringEnabled(ComSatSoundIOState &state,
                                                                  std::uint32_t talker_id,
                                                                  bool enabled);
[[nodiscard]] bool ComSatSoundIO_SetTalkerPlaybackVolume(ComSatSoundIOState &state,
                                                         std::uint32_t talker_id, float volume);
[[nodiscard]] bool ComSatSoundIO_SetTalkerSessionGain(ComSatSoundIOState &state,
                                                      std::uint32_t talker_id,
                                                      std::uint32_t session_id_low,
                                                      std::uint32_t session_id_high,
                                                      float gain);
[[nodiscard]] bool ComSatSoundIO_SetTalkerSessionVolume(ComSatSoundIOState &state,
                                                        std::uint32_t talker_id,
                                                        std::uint32_t session_id_low,
                                                        std::uint32_t session_id_high,
                                                        float volume);
[[nodiscard]] bool ComSatSoundIO_SetTalkerSessionSelectionValue(ComSatSoundIOState &state,
                                                                std::uint32_t talker_id,
                                                                std::uint32_t session_id_low,
                                                                std::uint32_t session_id_high,
                                                                std::uint8_t selection_value);

void ComSatSoundIO_QueueSilentPlaybackFrames(ComSatSoundIOState &state, std::uint32_t talker_id,
                                             std::uint32_t session_index, float block_run_count,
                                             double frame_duration_seconds);

[[nodiscard]] std::size_t
ComSatSoundIO_ProcessTalkerPlaybackFrames(ComSatSoundIOState &state, std::uint32_t talker_id,
                                          const ComSatTalkerPlaybackFrame *frames,
                                          std::size_t frame_count);

void ComSatSoundIO_ProcessPendingTalkerPlayback(ComSatSoundIOState &state,
                                                const ComSatPendingTalkerPlayback *pending_playback,
                                                std::size_t pending_playback_count);

std::int32_t ComSatSoundIO_AdvancePlayback(ComSatSoundIOState &state,
                                           ComSatSoundIOPlaybackRuntime &runtime,
                                           std::uint32_t current_tick_ms,
                                           ComSatSoundIOTalkerFrameAdvancer *advancer);

void ComSatSoundIO_AdvanceMemberRingBuffer(ComSatSoundIOSessionMember &member);

void ComSatSoundIO_ShiftMemberRingBuffer(ComSatSoundIOSessionMember &member,
                                         std::int32_t shift_amount,
                                         std::int32_t playback_update_window);
[[nodiscard]] bool ComSatSoundIO_MutateTalkerBoundSessionSet(
    ComSatSoundIOState &state, std::uint32_t talker_id, std::uint32_t session_id_low,
    std::uint32_t session_id_high, ComSatTalkerSessionSetMutation mutation);
[[nodiscard]] bool ComSatSoundIO_HasTalkerBoundSession(ComSatSoundIOState &state,
                                                       std::uint32_t talker_id,
                                                       std::uint32_t session_id_low,
                                                       std::uint32_t session_id_high);
[[nodiscard]] bool ComSatSoundIO_MutateTalkerSecondarySessionSet(
    ComSatSoundIOState &state, std::uint32_t talker_id, std::uint32_t session_id_low,
    std::uint32_t session_id_high, ComSatTalkerSessionSetMutation mutation);
[[nodiscard]] bool ComSatSoundIO_HasTalkerSecondarySession(ComSatSoundIOState &state,
                                                           std::uint32_t talker_id,
                                                           std::uint32_t session_id_low,
                                                           std::uint32_t session_id_high);

[[nodiscard]] bool ComSatSoundIO_NotifyTalkerFirstVoiceData(ComSatSoundIOState &state,
                                                            std::uint32_t talker_id);

[[nodiscard]] bool
ComSatSoundIO_EncodePackedVoiceBatch(const ComSatVariableBitrateVoiceBatch &batch,
                                     std::uint8_t *buffer, std::size_t buffer_size,
                                     std::size_t &bytes_written);

[[nodiscard]] bool
ComSatSoundIO_EncodeVariableBitrateVoiceBatch(const ComSatVariableBitrateVoiceBatch &batch,
                                              std::uint8_t *buffer, std::size_t buffer_size,
                                              std::size_t &bytes_written);

[[nodiscard]] bool
ComSatSoundIO_EncodeFixed960BitVoiceBatch(const ComSatVariableBitrateVoiceBatch &batch,
                                          std::uint8_t *buffer, std::size_t buffer_size,
                                          std::size_t &bytes_written);

[[nodiscard]] bool
ComSatSoundIO_DecodePackedVoiceBatch(const std::uint8_t *buffer, std::size_t buffer_size,
                                     ComSatVariableBitrateVoiceBatch &batch);

[[nodiscard]] bool ComSatSoundIO_FlushPendingVoiceBatch(ComSatPendingVoiceBatch &pending_batch);

bool ComSatSoundIO_SetSocketQOS(std::uintptr_t socket_handle);

[[nodiscard]] std::unique_ptr<ComSatDatagramSocket> ComSatSoundIO_CreateSocketWrapper();

}
