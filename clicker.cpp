#include "clicker.h"

#include <immintrin.h>

#include <algorithm>

namespace blossom {
namespace {

constexpr uint16_t kMouseLeft = VK_LBUTTON;
constexpr uint16_t kMouseRight = VK_RBUTTON;
constexpr uint16_t kMouseMiddle = VK_MBUTTON;
std::atomic<uint32_t> g_worker_id_seed{0u};

int64_t ReadQpcNow() noexcept {
    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    return static_cast<int64_t>(q.QuadPart);
}

int64_t ReadQpcFrequency() noexcept {
    LARGE_INTEGER q{};
    QueryPerformanceFrequency(&q);
    return std::max<int64_t>(1, static_cast<int64_t>(q.QuadPart));
}

int64_t MsToTicks(int64_t ms, int64_t freq) noexcept {
    if (ms <= 0) {
        return 0;
    }
    return (ms * freq) / 1000;
}

int64_t ComputeIntervalTicks(uint32_t cps, int64_t freq) noexcept {
    const uint32_t clamped = std::clamp<uint32_t>(cps, 1u, 4000u);
    const int64_t safe_freq = std::max<int64_t>(1, freq);
    // QPC frequency is ticks/second, so interval ticks = (ticks/second) / (clicks/second).
    return std::max<int64_t>(1, safe_freq / static_cast<int64_t>(clamped));
}

bool IsMouseVirtualKey(uint16_t vk) noexcept {
    return vk == kMouseLeft || vk == kMouseRight || vk == kMouseMiddle;
}

DWORD MouseDownFlag(uint16_t vk) noexcept {
    switch (vk) {
    case kMouseRight: return MOUSEEVENTF_RIGHTDOWN;
    case kMouseMiddle: return MOUSEEVENTF_MIDDLEDOWN;
    default: return MOUSEEVENTF_LEFTDOWN;
    }
}

DWORD MouseUpFlag(uint16_t vk) noexcept {
    switch (vk) {
    case kMouseRight: return MOUSEEVENTF_RIGHTUP;
    case kMouseMiddle: return MOUSEEVENTF_MIDDLEUP;
    default: return MOUSEEVENTF_LEFTUP;
    }
}

bool IsExtendedVirtualKey(uint16_t vk) noexcept {
    switch (vk) {
    case VK_RMENU:
    case VK_RCONTROL:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_DIVIDE:
    case VK_NUMLOCK:
    case VK_SNAPSHOT:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    default:
        return false;
    }
}

void WaitUntilQpc(int64_t target_qpc, int64_t freq, const std::atomic<bool>& running) {
    while (running.load(std::memory_order_relaxed)) {
        const int64_t now = ReadQpcNow();
        const int64_t remaining_ticks = target_qpc - now;
        if (remaining_ticks <= 0) {
            return;
        }

        const int64_t remaining_us = (remaining_ticks * 1000000) / freq;
        if (remaining_us > 2500) {
            Sleep(static_cast<DWORD>((remaining_us / 1000) - 1));
        } else if (remaining_us > 350) {
            Sleep(0);
        } else {
            _mm_pause();
        }
    }
}

INPUT MakeKeyboardInput(uint16_t vk, bool key_up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
    if (scan != 0) {
        in.ki.wScan = static_cast<WORD>(scan & 0xFFu);
        in.ki.dwFlags = KEYEVENTF_SCANCODE;
        if ((scan & 0xFF00u) == 0xE000u || IsExtendedVirtualKey(vk)) {
            in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
    } else {
        in.ki.wVk = vk;
    }
    if (key_up) {
        in.ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    return in;
}

} // namespace

AdvancedClicker::AdvancedClicker() {
    const uint32_t worker_id = g_worker_id_seed.fetch_add(1u, std::memory_order_relaxed);
    qpc_frequency_ = ReadQpcFrequency();
    worker_ = std::thread(&AdvancedClicker::WorkerLoop, this);
    SetThreadPriority(worker_.native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);

    // Spread workers across cores instead of pinning all clickers to one core.
    const DWORD cpu_count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (cpu_count > 0 && cpu_count <= 64) {
        const DWORD core = worker_id % cpu_count;
        const DWORD_PTR mask = static_cast<DWORD_PTR>(1ull << core);
        SetThreadAffinityMask(worker_.native_handle(), mask);
    }
}

AdvancedClicker::~AdvancedClicker() {
    running_.store(false, std::memory_order_release);
    active_.store(false, std::memory_order_release);
    if (worker_.joinable()) {
        worker_.join();
    }
}

void AdvancedClicker::SetConfig(const ClickerConfig& config) {
    std::lock_guard<std::mutex> lock(config_lock_);
    config_ = config;
}

ClickerConfig AdvancedClicker::GetConfig() const {
    std::lock_guard<std::mutex> lock(config_lock_);
    return config_;
}

void AdvancedClicker::SetActive(bool active) {
    active_.store(active, std::memory_order_release);
}

bool AdvancedClicker::IsActive() const {
    return active_.load(std::memory_order_acquire);
}

uint64_t AdvancedClicker::GetTotalClicks() const {
    return total_clicks_.load(std::memory_order_relaxed);
}

void AdvancedClicker::ResetCount() {
    total_clicks_.store(0, std::memory_order_relaxed);
}

void AdvancedClicker::ReleaseHeld(uint16_t vk, bool& held_down, uint16_t& held_vk) {
    if (!held_down || vk == 0) {
        held_vk = 0;
        return;
    }

    if (IsMouseVirtualKey(vk)) {
        INPUT up{};
        up.type = INPUT_MOUSE;
        up.mi.dwFlags = MouseUpFlag(vk);
        SendInput(1, &up, sizeof(INPUT));
    } else {
        INPUT up = MakeKeyboardInput(vk, true);
        SendInput(1, &up, sizeof(INPUT));
    }
    held_down = false;
    held_vk = 0;
}

bool AdvancedClicker::EmitAction(uint16_t vk, bool down_only, uint32_t key_press_ms, bool& held_down, uint16_t& held_vk) {
    if (vk == 0) {
        return false;
    }

    if (IsMouseVirtualKey(vk)) {
        if (held_down) {
            ReleaseHeld(held_vk, held_down, held_vk);
        }
        INPUT inputs[2]{};
        inputs[0].type = INPUT_MOUSE;
        inputs[0].mi.dwFlags = MouseDownFlag(vk);
        inputs[1].type = INPUT_MOUSE;
        inputs[1].mi.dwFlags = MouseUpFlag(vk);
        SendInput(2, inputs, sizeof(INPUT));
        return true;
    }

    if (down_only) {
        if (held_down && held_vk == vk) {
            return false;
        }
        ReleaseHeld(held_vk, held_down, held_vk);

        INPUT down = MakeKeyboardInput(vk, false);
        SendInput(1, &down, sizeof(INPUT));
        held_down = true;
        held_vk = vk;
        return true;
    }

    if (held_down) {
        ReleaseHeld(held_vk, held_down, held_vk);
    }

    INPUT inputs[2]{};
    inputs[0] = MakeKeyboardInput(vk, false);
    inputs[1] = MakeKeyboardInput(vk, true);

    if (key_press_ms == 0) {
        // Normal mode: emit press+release in one syscall for minimum latency.
        SendInput(2, inputs, sizeof(INPUT));
        return true;
    }

    // Hold mode: keep key down for key_press_ms before releasing.
    SendInput(1, &inputs[0], sizeof(INPUT));
    WaitUntilQpc(ReadQpcNow() + MsToTicks(static_cast<int64_t>(std::max<uint32_t>(1u, key_press_ms)), qpc_frequency_), qpc_frequency_, running_);
    SendInput(1, &inputs[1], sizeof(INPUT));
    return true;
}

void AdvancedClicker::WorkerLoop() {
    int64_t next_tick = 0;
    int64_t scheduled_interval_ticks = 0;
    bool offset_applied = false;
    bool held_down = false;
    uint16_t held_vk = 0;

    while (running_.load(std::memory_order_acquire)) {
        if (!active_.load(std::memory_order_acquire)) {
            ReleaseHeld(held_vk, held_down, held_vk);
            next_tick = 0;
            scheduled_interval_ticks = 0;
            offset_applied = false;
            Sleep(1);
            continue;
        }

        const ClickerConfig cfg = GetConfig();
        if (!cfg.enabled || cfg.output_vk == 0 || cfg.cps == 0) {
            ReleaseHeld(held_vk, held_down, held_vk);
            next_tick = 0;
            scheduled_interval_ticks = 0;
            offset_applied = false;
            Sleep(1);
            continue;
        }

        const int64_t interval_ticks = ComputeIntervalTicks(cfg.cps, qpc_frequency_);
        const int64_t min_delay_ticks = MsToTicks(static_cast<int64_t>(cfg.delay_ms), qpc_frequency_);
        const int64_t actual_interval_ticks = std::max<int64_t>(interval_ticks, min_delay_ticks);
        if (!offset_applied) {
            next_tick = ReadQpcNow() + MsToTicks(static_cast<int64_t>(cfg.offset_ms), qpc_frequency_);
            offset_applied = true;
            scheduled_interval_ticks = actual_interval_ticks;
        } else if (scheduled_interval_ticks != actual_interval_ticks) {
            // Apply rate changes immediately without re-applying startup offset.
            next_tick = ReadQpcNow() + actual_interval_ticks;
            scheduled_interval_ticks = actual_interval_ticks;
        }
        if (next_tick <= 0) {
            next_tick = ReadQpcNow();
        }

        WaitUntilQpc(next_tick, qpc_frequency_, running_);
        if (!running_.load(std::memory_order_acquire) || !active_.load(std::memory_order_acquire)) {
            continue;
        }

        uint32_t emitted = 0;
        const uint32_t key_press_ms = cfg.key_press_ms;
        if (running_.load(std::memory_order_relaxed) && active_.load(std::memory_order_relaxed) &&
            EmitAction(cfg.output_vk, cfg.down_only, key_press_ms, held_down, held_vk)) {
            emitted = 1;
        }
        if (emitted > 0) {
            total_clicks_.fetch_add(emitted, std::memory_order_relaxed);
        }

        // Keep scheduling on this clicker's own timeline to avoid drift and resync.
        next_tick += actual_interval_ticks;
        const int64_t now = ReadQpcNow();
        if (next_tick <= now) {
            const int64_t behind_ticks = now - next_tick;
            const int64_t missed_intervals = std::max<int64_t>(1, (behind_ticks / actual_interval_ticks) + 1);
            next_tick += missed_intervals * actual_interval_ticks;
        }
        if (next_tick <= now) {
            next_tick = now + 1;
        }
    }

    ReleaseHeld(held_vk, held_down, held_vk);
}

} // namespace blossom
