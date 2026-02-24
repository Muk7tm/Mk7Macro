#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace blossom {

class ClickerMonitor final {
public:
    ClickerMonitor();

    void ResetSession();
    void Update(const std::array<uint64_t, 4>& click_counts, uint32_t active_clickers);

    uint64_t GetTotalClicks() const;
    double GetLiveCps() const;
    double GetAverageCps() const;
    double GetSessionSeconds() const;
    uint32_t GetActiveClickers() const;
    std::array<uint64_t, 4> GetPerClicker() const;

private:
    struct CpsSample final {
        uint64_t tick = 0;
        uint64_t clicks = 0;
    };

    mutable std::mutex lock_{};
    std::array<uint64_t, 4> per_clicker_{};
    uint64_t total_clicks_ = 0;
    uint32_t active_clickers_ = 0;
    uint64_t start_tick_ = 0;
    uint64_t qpc_frequency_ = 1;
    double live_cps_ = 0.0;
    std::array<CpsSample, 32> cps_samples_{};
    size_t cps_tail_ = 0;
    size_t cps_count_ = 0;
};

} // namespace blossom
