#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class RealmType : uint32_t {
    Normal = 0,
    PvP    = 1,
    RP     = 6,
    RPPvP  = 8,
};

enum class RealmPopulation : uint8_t {
    Low,
    Medium,
    High,
    Full,
    Recommended,
    New,
};

enum RealmFlags : uint32_t {
    RealmFlag_None         = 0x00,
    RealmFlag_Offline      = 0x02,
    RealmFlag_SpecifyBuild = 0x04,
    RealmFlag_Unknown1     = 0x08,
    RealmFlag_Unknown2     = 0x10,
};

struct RealmCharacterCount {
    uint32_t realm_id = 0;
    uint32_t num_chars = 0;
};

struct RealmEntry {
    uint32_t id = 0;
    RealmType type = RealmType::Normal;
    uint32_t flags = RealmFlag_None;
    std::string name;
    std::string address;
    uint16_t port = 8085;
    RealmPopulation population = RealmPopulation::Low;
    uint32_t character_count = 0;
    uint8_t timezone = 0;
    std::string build_info;
};

class RealmList {
 public:
    static RealmList& Get();

    void SetRealms(const std::vector<RealmEntry>& realms);
    [[nodiscard]] std::vector<RealmEntry> GetRealms() const;
    [[nodiscard]] std::optional<RealmEntry> GetRealm(uint32_t id) const;
    [[nodiscard]] std::optional<RealmEntry> GetRealmByName(const std::string& name) const;
    [[nodiscard]] uint32_t GetRealmCount() const;

    void SetSelectedRealm(uint32_t id);
    [[nodiscard]] std::optional<RealmEntry> GetSelectedRealm() const;
    [[nodiscard]] uint32_t GetSelectedRealmId() const;

    [[nodiscard]] std::string GetLastSelectedRealmName() const;
    void SetLastSelectedRealmName(const std::string& name);

    [[nodiscard]] bool IsRealmOnline(uint32_t id) const;
    [[nodiscard]] std::optional<RealmEntry> GetRecommendedRealm() const;

    void SortByName();
    void SortByType();
    void SortByPopulation();

    [[nodiscard]] uint32_t GetCharacterCount(uint32_t realm_id) const;
    void SetCharacterCounts(const std::vector<RealmCharacterCount>& counts);

    void ForEach(const std::function<void(const RealmEntry&)>& callback) const;

    void Reset();

 private:
    RealmList() = default;

    std::vector<RealmEntry> realms_;
    uint32_t selected_realm_id_ = 0;
    std::string last_selected_realm_name_;
    mutable std::mutex mutex_;
};

}
