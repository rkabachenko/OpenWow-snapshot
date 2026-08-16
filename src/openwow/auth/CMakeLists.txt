openwow_add_component(openwow_auth
  srp6.cpp
)
target_link_libraries(openwow_auth PRIVATE OpenSSL::Crypto)
