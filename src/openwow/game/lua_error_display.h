#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class LuaErrorDisplayMode : uint8_t {
    ShowAll  = 0,
    ShowFirst = 1,
    HideAll  = 2,
};

struct LuaErrorDisplayEntry {
    std::string  message;
    std::string  fullStack;
    double       timestamp{0.0};
    std::string  addonName;
    bool         dismissed{false};
};

class LuaErrorDisplay {
 public:
    LuaErrorDisplay() = default;

    void PushError(const std::string& message,
                   const std::string& stack,
                   const std::string& addonName);

    [[nodiscard]] std::optional<LuaErrorDisplayEntry> GetCurrentError() const;

    void DismissCurrent();
    void DismissAll();

    [[nodiscard]] uint32_t GetQueueSize() const;

    [[nodiscard]] LuaErrorDisplayMode GetMode() const;
    void SetMode(LuaErrorDisplayMode mode);

    [[nodiscard]] bool HasVisibleError() const;

    [[nodiscard]] std::vector<LuaErrorDisplayEntry> GetAllErrors() const;
    void ClearAll();

    void NavigateNext();
    void NavigatePrev();
    [[nodiscard]] uint32_t GetCurrentIndex() const;

 private:
    static constexpr uint32_t kMaxErrors = 200;

    std::vector<LuaErrorDisplayEntry> errors_;
    LuaErrorDisplayMode               mode_{LuaErrorDisplayMode::ShowAll};
    uint32_t                          currentIdx_{0};

    [[nodiscard]] int FindNextUndismissed(int from, int direction) const;
};

}
