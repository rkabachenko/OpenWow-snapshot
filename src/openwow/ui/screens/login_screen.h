#pragma once

#include "openwow/net/wotlk/protocol/auth_protocol.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace openwow::ui::screens {

class LoginScreen {
 public:
  ~LoginScreen() { SecureClearPassword(); }

  void SetRememberPassword(bool enabled);
  bool remember_password() const;
  void SetUsername(const std::string& username);
  void SetPassword(const std::string& password);
  const std::string& username() const;
  const std::string& password() const;
  openwow::net::wotlk::AuthResult AttemptLogin(const std::string& host,
                                               std::uint16_t port,
                                               std::uint32_t timeout_ms = 5000) const;

  void SecureClearPassword();

 private:
  bool remember_password_{false};
  std::string username_;
  std::string password_;
};

}
