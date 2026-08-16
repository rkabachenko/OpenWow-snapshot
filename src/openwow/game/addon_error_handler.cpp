
#include "openwow/game/addon_error_handler.h"

#include <algorithm>
#include <chrono>

namespace openwow::game {

static double Now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

int AddonErrorHandler::FindDedupIndex(const DedupKey& key) const {
    for (int i = 0; i < static_cast<int>(errors_.size()); ++i) {
        if (errors_[i].message == key.message &&
            errors_[i].file == key.file &&
            errors_[i].line == key.line) {
            return i;
        }
    }
    return -1;
}

uint32_t AddonErrorHandler::CountAddonErrors(const std::string& name) const {
    uint32_t total = 0;
    for (const auto& e : errors_) {
        if (e.addonName == name) {
            total += e.count;
        }
    }
    return total;
}

void AddonErrorHandler::ReportError(const std::string& addonName,
                                     const std::string& message,
                                     const std::string& stackTrace,
                                     const std::string& file,
                                     uint32_t line,
                                     AddonErrorSeverity severity) {
    const double now = Now();

    DedupKey key{message, file, line};
    int idx = FindDedupIndex(key);
    if (idx >= 0) {
        errors_[idx].count++;
        errors_[idx].timestamp = now;

        if (static_cast<uint8_t>(severity) >
            static_cast<uint8_t>(errors_[idx].severity)) {
            errors_[idx].severity = severity;
        }

        errors_[idx].stackTrace = stackTrace;

        if (!IsAddonDisabledDueToErrors(addonName) &&
            CountAddonErrors(addonName) >= autoDisableThreshold_) {
            disabledAddons_.push_back(addonName);
        }
        return;
    }

    if (errors_.size() >= maxErrors_) {

        errors_.pop_back();
    }

    AddonErrorRecord rec;
    rec.addonName  = addonName;
    rec.message    = message;
    rec.stackTrace = stackTrace;
    rec.timestamp  = now;
    rec.severity   = severity;
    rec.count      = 1;
    rec.firstSeen  = now;
    rec.file       = file;
    rec.line       = line;

    errors_.insert(errors_.begin(), std::move(rec));

    if (!IsAddonDisabledDueToErrors(addonName) &&
        CountAddonErrors(addonName) >= autoDisableThreshold_) {
        disabledAddons_.push_back(addonName);
    }
}

std::vector<AddonErrorRecord> AddonErrorHandler::GetErrors() const {
    return errors_;
}

uint32_t AddonErrorHandler::GetErrorCount() const {
    return static_cast<uint32_t>(errors_.size());
}

uint32_t AddonErrorHandler::GetTotalOccurrences() const {
    uint32_t total = 0;
    for (const auto& e : errors_) {
        total += e.count;
    }
    return total;
}

std::vector<AddonErrorRecord> AddonErrorHandler::GetErrorsForAddon(
    const std::string& name) const {
    std::vector<AddonErrorRecord> result;
    for (const auto& e : errors_) {
        if (e.addonName == name) {
            result.push_back(e);
        }
    }
    return result;
}

void AddonErrorHandler::ClearErrors() {
    errors_.clear();
}

void AddonErrorHandler::ClearErrorsForAddon(const std::string& name) {
    errors_.erase(
        std::remove_if(errors_.begin(), errors_.end(),
                        [&name](const AddonErrorRecord& e) {
                            return e.addonName == name;
                        }),
        errors_.end());
}

void AddonErrorHandler::SetMaxErrors(uint32_t max) {
    maxErrors_ = max;
    while (errors_.size() > maxErrors_) {
        errors_.pop_back();
    }
}

bool AddonErrorHandler::IsAddonDisabledDueToErrors(const std::string& name) const {
    return std::find(disabledAddons_.begin(), disabledAddons_.end(), name) !=
           disabledAddons_.end();
}

std::vector<std::string> AddonErrorHandler::GetDisabledAddons() const {
    return disabledAddons_;
}

void AddonErrorHandler::ReenableAddon(const std::string& name) {
    disabledAddons_.erase(
        std::remove(disabledAddons_.begin(), disabledAddons_.end(), name),
        disabledAddons_.end());
}

void AddonErrorHandler::SetAutoDisableThreshold(uint32_t count) {
    autoDisableThreshold_ = count;
}

}
