#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

enum class AddonErrorSeverity : uint8_t {
    Warning = 0,
    Error   = 1,
    Fatal   = 2,
};

struct AddonErrorRecord {
    std::string          addonName;
    std::string          message;
    std::string          stackTrace;
    double               timestamp{0.0};
    AddonErrorSeverity   severity{AddonErrorSeverity::Error};
    uint32_t             count{1};
    double               firstSeen{0.0};
    std::string          file;
    uint32_t             line{0};
};

class AddonErrorHandler {
 public:
    AddonErrorHandler() = default;

    void ReportError(const std::string& addonName,
                     const std::string& message,
                     const std::string& stackTrace,
                     const std::string& file,
                     uint32_t line,
                     AddonErrorSeverity severity = AddonErrorSeverity::Error);

    [[nodiscard]] std::vector<AddonErrorRecord> GetErrors() const;

    [[nodiscard]] uint32_t GetErrorCount() const;

    [[nodiscard]] uint32_t GetTotalOccurrences() const;

    [[nodiscard]] std::vector<AddonErrorRecord> GetErrorsForAddon(
        const std::string& name) const;

    void ClearErrors();
    void ClearErrorsForAddon(const std::string& name);

    void SetMaxErrors(uint32_t max);

    [[nodiscard]] bool IsAddonDisabledDueToErrors(const std::string& name) const;
    [[nodiscard]] std::vector<std::string> GetDisabledAddons() const;
    void ReenableAddon(const std::string& name);
    void SetAutoDisableThreshold(uint32_t count);

 private:
    struct DedupKey {
        std::string message;
        std::string file;
        uint32_t    line{0};

        bool operator==(const DedupKey& o) const {
            return message == o.message && file == o.file && line == o.line;
        }
    };

    std::vector<AddonErrorRecord>  errors_;
    std::vector<std::string>       disabledAddons_;
    uint32_t                       maxErrors_{100};
    uint32_t                       autoDisableThreshold_{50};

    [[nodiscard]] int FindDedupIndex(const DedupKey& key) const;
    [[nodiscard]] uint32_t CountAddonErrors(const std::string& name) const;
};

}
