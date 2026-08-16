
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace openwow::game {

inline constexpr std::size_t kGMResponseDescriptionMaxBytesIncludingNul = 2000u;
inline constexpr std::size_t kGMResponseLineMaxBytesIncludingNul = 4000u;
inline constexpr std::size_t kGMResponseLineCount = 4u;

inline constexpr std::size_t kGMTicketTextMaxBytesIncludingNul = 2000u;

struct GMTicketData {
  std::uint32_t status = 0;
  std::uint32_t id = 0;
  std::string text;
  std::uint8_t category = 0;
  float time_since_updated = 0.0f;
  float time_oldest = 0.0f;
  float time_since_updated2 = 0.0f;
  std::uint8_t escalated = 0;
  std::uint8_t viewed = 0;
};

struct GMResponse {
  std::uint32_t response_id = 0;
  std::uint32_t ticket_id = 0;
  std::string description;
  std::array<std::string, kGMResponseLineCount> response;
};

class GMTicketHandler {
 public:
  bool HandleGMTicketSystemStatus(const std::uint8_t* data, std::size_t len);
  bool HandleGMTicketGetTicket(const std::uint8_t* data, std::size_t len);
  bool HandleGMTicketCreate(const std::uint8_t* data, std::size_t len);
  bool HandleGMTicketStatusUpdate(const std::uint8_t* data, std::size_t len);
  bool HandleGMResponseReceived(const std::uint8_t* data, std::size_t len);
  bool HandleGMResponseStatusUpdate(const std::uint8_t* data, std::size_t len);

  bool HandleGMResponseCreateTicket(const std::uint8_t* data, std::size_t len);
  bool HandleGMResponseDbError();
  bool HandleGMTicketDeleteTicket(const std::uint8_t* data, std::size_t len);
  bool HandleGMTicketUpdateText(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] std::uint32_t system_status() const { return system_status_; }
  [[nodiscard]] const std::optional<GMTicketData>& last_ticket() const {
    return last_ticket_;
  }
  [[nodiscard]] std::uint32_t ticket_create_result() const {
    return ticket_create_result_;
  }
  [[nodiscard]] std::uint32_t ticket_status_update() const {
    return ticket_status_update_;
  }
  [[nodiscard]] const std::optional<GMResponse>& last_gm_response() const {
    return last_gm_response_;
  }
  [[nodiscard]] std::uint8_t gm_response_active() const {
    return gm_response_active_;
  }

  [[nodiscard]] std::uint32_t gm_response_create_ticket() const {
    return gm_response_create_ticket_;
  }
  [[nodiscard]] bool gm_response_db_error() const {
    return gm_response_db_error_;
  }
  [[nodiscard]] std::uint32_t ticket_delete_result() const {
    return ticket_delete_result_;
  }
  [[nodiscard]] std::uint32_t ticket_update_text_result() const {
    return ticket_update_text_result_;
  }
  [[nodiscard]] std::uint32_t active_ticket_id() const {
    return active_ticket_id_;
  }
  [[nodiscard]] std::uint32_t active_response_id() const {
    return active_response_id_;
  }

  void SetActiveTicketId(std::uint32_t ticket_id);
  void SetActiveResponse(std::uint32_t response_id, std::uint32_t ticket_id);
  void ClearActiveTicketState();
  void ClearActiveResponse();

  void Clear();

 private:
  std::uint32_t system_status_ = 0;
  std::optional<GMTicketData> last_ticket_;
  std::uint32_t ticket_create_result_ = 0;
  std::uint32_t ticket_status_update_ = 0;
  std::optional<GMResponse> last_gm_response_;
  std::uint8_t gm_response_active_ = 0;

  std::uint32_t gm_response_create_ticket_ = 0;
  bool gm_response_db_error_ = false;
  std::uint32_t ticket_delete_result_ = 0;
  std::uint32_t ticket_update_text_result_ = 0;
  std::uint32_t active_ticket_id_ = 0;
  std::uint32_t active_response_id_ = 0;
};

}
