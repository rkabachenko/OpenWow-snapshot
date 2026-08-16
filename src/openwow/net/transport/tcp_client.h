#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace openwow::net {

class TcpClient {
 public:
  TcpClient();
  ~TcpClient();

  TcpClient(const TcpClient&) = delete;
  TcpClient& operator=(const TcpClient&) = delete;
  TcpClient(TcpClient&&) = delete;
  TcpClient& operator=(TcpClient&&) = delete;

  bool Connect(const std::string& host,
               std::uint16_t port,
               std::uint32_t timeout_ms = 5000,
               const std::function<bool()>& should_cancel = {});
  bool Write(const std::vector<std::uint8_t>& bytes,
             std::uint32_t timeout_ms = 5000,
             const std::function<bool()>& should_cancel = {});

  std::vector<std::uint8_t> ReadSome(std::size_t max_bytes, std::uint32_t timeout_ms = 5000,
                                     const std::function<bool()> &should_cancel = {});

  std::vector<std::uint8_t> ReadSomeUntilCancelled(std::size_t max_bytes,
                                                   const std::function<bool()> &should_cancel);
  std::vector<std::uint8_t> ReadExact(std::size_t bytes, std::uint32_t timeout_ms = 5000,
                                      const std::function<bool()> &should_cancel = {});
  std::vector<std::uint8_t> ReadExactUntilCancelled(std::size_t bytes,
                                                    const std::function<bool()> &should_cancel);
  void Disconnect();
  [[nodiscard]] bool IsConnected() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}
