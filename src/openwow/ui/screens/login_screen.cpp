#include "openwow/ui/screens/login_screen.h"

namespace openwow::ui::screens {

void LoginScreen::SetRememberPassword(bool enabled) {
  remember_password_ = enabled;
}

bool LoginScreen::remember_password() const {
  return remember_password_;
}

void LoginScreen::SetUsername(const std::string& username) {
  username_ = username;
}

void LoginScreen::SetPassword(const std::string& password) {
  password_ = password;
}

const std::string& LoginScreen::username() const {
  return username_;
}

const std::string& LoginScreen::password() const {
  return password_;
}

openwow::net::wotlk::AuthResult LoginScreen::AttemptLogin(const std::string& host,
                                                          std::uint16_t port,
                                                          std::uint32_t timeout_ms) const {
  openwow::net::wotlk::AuthProtocol auth;
  return auth.Login(host, port, username_, password_, timeout_ms);
}

void LoginScreen::SecureClearPassword() {
  if (!password_.empty()) {

    volatile char* p = const_cast<volatile char*>(password_.data());
    std::memset(const_cast<char*>(password_.data()), 0, password_.size());
    (void)p;
    password_.clear();
  }
}

}
