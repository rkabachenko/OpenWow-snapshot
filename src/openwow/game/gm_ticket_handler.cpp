
#include "openwow/game/gm_ticket_handler.h"

#include "openwow/game/packet_reader.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/net/serialization/cdatastore_ops.h"

namespace openwow::game {

namespace {

net::CDataStore MakeGMResponsePacketStore(const std::uint8_t* data,
                                          const std::size_t len) {
  const auto wire_size = static_cast<std::uint32_t>(len);
  return net::CDataStore{
      .data = const_cast<std::uint8_t*>(data),
      .window_base = 0,
      .window_size = wire_size,
      .write_pos = wire_size,
      .read_pos = 0,
  };
}

template <std::size_t BufferSize>
std::string ReadGMResponseString(net::CDataStore& store) {
  std::array<char, BufferSize> buffer{};
  net::CDataStore_GetString(store, buffer.data(),
                            static_cast<std::uint32_t>(buffer.size()));
  return buffer.data();
}

}

void GMTicketHandler::SetActiveTicketId(const std::uint32_t ticket_id) {
  active_ticket_id_ = ticket_id;
}

void GMTicketHandler::SetActiveResponse(const std::uint32_t response_id,
                                        const std::uint32_t ticket_id) {
  active_response_id_ = response_id;
  active_ticket_id_ = ticket_id;
}

void GMTicketHandler::ClearActiveTicketState() {
  active_ticket_id_ = 0;
  active_response_id_ = 0;
}

void GMTicketHandler::ClearActiveResponse() {
  active_response_id_ = 0;
}

bool GMTicketHandler::HandleGMTicketSystemStatus(const std::uint8_t* data,
                                                  std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(system_status_)) return false;
  return true;
}

bool GMTicketHandler::HandleGMTicketGetTicket(const std::uint8_t* data,
                                               std::size_t len) {
  PacketReader r(data, len);
  GMTicketData ticket{};
  if (!r.ReadU32(ticket.status)) return false;

  if (ticket.status == 6) {
    if (!r.ReadU32(ticket.id)) return false;
    if (!r.ReadCString(ticket.text, kGMTicketTextMaxBytesIncludingNul)) return false;
    if (!r.ReadU8(ticket.category)) return false;
    if (!r.ReadFloat(ticket.time_since_updated)) return false;
    if (!r.ReadFloat(ticket.time_oldest)) return false;
    if (!r.ReadFloat(ticket.time_since_updated2)) return false;
    if (!r.ReadU8(ticket.escalated)) return false;
    if (!r.ReadU8(ticket.viewed)) return false;
  }

  last_ticket_ = std::move(ticket);
  return true;
}

bool GMTicketHandler::HandleGMTicketCreate(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(ticket_create_result_)) return false;
  return true;
}

bool GMTicketHandler::HandleGMTicketStatusUpdate(const std::uint8_t* data,
                                                  std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(ticket_status_update_)) return false;
  return true;
}

bool GMTicketHandler::HandleGMResponseReceived(const std::uint8_t* data,
                                                std::size_t len) {
  auto store = MakeGMResponsePacketStore(data, len);
  GMResponse resp{};

  resp.response_id = active_response_id_;
  resp.ticket_id = active_ticket_id_;
  net::CDataStore_GetUInt32(store, &resp.response_id);
  net::CDataStore_GetUInt32(store, &resp.ticket_id);
  resp.description =
      ReadGMResponseString<kGMResponseDescriptionMaxBytesIncludingNul>(store);
  for (auto& response_line : resp.response) {
    response_line =
        ReadGMResponseString<kGMResponseLineMaxBytesIncludingNul>(store);
  }
  last_gm_response_ = std::move(resp);
  return true;
}

bool GMTicketHandler::HandleGMResponseStatusUpdate(const std::uint8_t* data,
                                                    std::size_t len) {

  gm_response_active_ = 0;
  auto store = MakeGMResponsePacketStore(data, len);
  net::CDataStore_GetUInt8(store, &gm_response_active_);
  return true;
}

void GMTicketHandler::Clear() {
  system_status_ = 0;
  last_ticket_.reset();
  ticket_create_result_ = 0;
  ticket_status_update_ = 0;
  last_gm_response_.reset();
  gm_response_active_ = 0;
  gm_response_create_ticket_ = 0;
  gm_response_db_error_ = false;
  ticket_delete_result_ = 0;
  ticket_update_text_result_ = 0;
  active_ticket_id_ = 0;
  active_response_id_ = 0;
}

bool GMTicketHandler::HandleGMResponseCreateTicket(const std::uint8_t* data,
                                                   std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(gm_response_create_ticket_)) return false;
  return true;
}

bool GMTicketHandler::HandleGMResponseDbError() {
  gm_response_db_error_ = true;
  return true;
}

bool GMTicketHandler::HandleGMTicketDeleteTicket(const std::uint8_t* data,
                                                 std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(ticket_delete_result_)) return false;
  return true;
}

bool GMTicketHandler::HandleGMTicketUpdateText(const std::uint8_t* data,
                                               std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(ticket_update_text_result_)) return false;
  return true;
}

}
