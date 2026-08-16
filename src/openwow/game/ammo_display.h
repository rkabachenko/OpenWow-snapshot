#pragma once

#include <cstdint>
#include <string>

namespace openwow::game {

enum class AmmoType : std::uint8_t {
    None   = 0,
    Arrow  = 1,
    Bullet = 2,
};

inline constexpr std::uint32_t kAmmoLowThreshold = 200;

class AmmoDisplay {
public:
    void SetAmmo(std::uint32_t itemId, std::string name,
                 std::uint32_t iconId, AmmoType type);
    void ClearAmmo();

    [[nodiscard]] std::uint32_t    GetAmmoItemId() const;
    [[nodiscard]] const std::string& GetAmmoName() const;
    [[nodiscard]] AmmoType         GetAmmoType() const;
    [[nodiscard]] std::uint32_t    GetAmmoIconId() const;

    void SetAmmoCount(std::uint32_t count);
    [[nodiscard]] std::uint32_t GetAmmoCount() const;

    [[nodiscard]] bool IsAmmoEquipped() const;

    [[nodiscard]] float GetDPS() const;
    void SetDPS(float dps);

    [[nodiscard]] bool IsLow() const;
    [[nodiscard]] std::uint32_t GetLowThreshold() const;

    [[nodiscard]] bool NeedsAmmo() const;
    void SetHasRangedWeapon(bool has);
    [[nodiscard]] bool HasRangedWeapon() const;

    void Reset();

private:
    std::uint32_t itemId_ = 0;
    std::string   name_;
    std::uint32_t iconId_ = 0;
    AmmoType      type_   = AmmoType::None;
    std::uint32_t count_  = 0;
    float         dps_    = 0.0f;
    bool          hasRangedWeapon_ = false;
};

}
