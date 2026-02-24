#include "monitor.h"

#include <Windows.h>

#include <algorithm>

namespace blossom {
namespace {

uint64_t ReadQpcNow() noexcept {
    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    return static_cast<uint64_t>(q.QuadPart);
}

uint64_t ReadQpcFrequency() noexcept {
    LARGE_INTEGER q{};
    QueryPerformanceFrequency(&q);
    return std::max<uint64_t>(1ull, static_cast<uint64_t>(q.QuadPart));
}

} // namespace

ClickerMonitor::ClickerMonitor() {
    qpc_frequency_ = ReadQpcFrequency();
    start_tick_ = ReadQpcNow();
}

void ClickerMonitor::ResetSession() {
    std::lock_guard<std::mutex> lock(lock_);
    per_clicker_ = {};
    total_clicks_ = 0;
    active_clickers_ = 0;
    start_tick_ = ReadQpcNow();
    live_cps_ = 0.0;
    cps_tail_ = 0;
    cps_count_ = 0;
}

void ClickerMonitor::Update(const std::array<uint64_t, 4>& click_counts, uint32_t active_clickers) {
    std::lock_guard<std::mutex> lock(lock_);
    per_clicker_ = click_counts;
    total_clicks_ = click_counts[0] + click_counts[1] + click_counts[2] + click_counts[3];
    active_clickers_ = active_clickers;

    const uint64_t now_tick = ReadQpcNow();
    const size_t capacity = cps_samples_.size();
    size_t write_index = (cps_tail_ + cps_count_) % capacity;
    if (cps_count_ == capacity) {
        cps_tail_ = (cps_tail_ + 1) % capacity;
        --cps_count_;
        write_index = (cps_tail_ + cps_count_) % capacity;
    }
    cps_samples_[write_index] = {now_tick, total_clicks_};
    ++cps_count_;

    const uint64_t window_ticks = qpc_frequency_;
    while (cps_count_ > 2) {
        const CpsSample& oldest = cps_samples_[cps_tail_];
        if (now_tick - oldest.tick <= window_ticks) {
            break;
        }
        cps_tail_ = (cps_tail_ + 1) % capacity;
        --cps_count_;
    }

    if (cps_count_ < 2) {
        live_cps_ = 0.0;
        return;
    }

    const CpsSample& oldest = cps_samples_[cps_tail_];
    const size_t newest_index = (cps_tail_ + cps_count_ - 1) % capacity;
    const CpsSample& newest = cps_samples_[newest_index];
    const uint64_t elapsed_ticks = newest.tick - oldest.tick;
    const uint64_t emitted = (newest.clicks >= oldest.clicks) ? (newest.clicks - oldest.clicks) : 0;
    if (elapsed_ticks == 0 || emitted == 0) {
        live_cps_ = 0.0;
        return;
    }

    const double elapsed_sec = static_cast<double>(elapsed_ticks) / static_cast<double>(qpc_frequency_);
    live_cps_ = (elapsed_sec > 0.0) ? (static_cast<double>(emitted) / elapsed_sec) : 0.0;
}

uint64_t ClickerMonitor::GetTotalClicks() const {
    std::lock_guard<std::mutex> lock(lock_);
    return total_clicks_;
}

double ClickerMonitor::GetLiveCps() const {
    std::lock_guard<std::mutex> lock(lock_);
    return live_cps_;
}

double ClickerMonitor::GetAverageCps() const {
    std::lock_guard<std::mutex> lock(lock_);
    const uint64_t elapsed_ticks = ReadQpcNow() - start_tick_;
    const double elapsed_sec = static_cast<double>(elapsed_ticks) / static_cast<double>(qpc_frequency_);
    if (elapsed_sec <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(total_clicks_) / elapsed_sec;
}

double ClickerMonitor::GetSessionSeconds() const {
    std::lock_guard<std::mutex> lock(lock_);
    const uint64_t elapsed_ticks = ReadQpcNow() - start_tick_;
    return static_cast<double>(elapsed_ticks) / static_cast<double>(qpc_frequency_);
}

uint32_t ClickerMonitor::GetActiveClickers() const {
    std::lock_guard<std::mutex> lock(lock_);
    return active_clickers_;
}

std::array<uint64_t, 4> ClickerMonitor::GetPerClicker() const {
    std::lock_guard<std::mutex> lock(lock_);
    return per_clicker_;
}

} // namespace blossom
