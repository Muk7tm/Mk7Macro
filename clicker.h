#pragma once

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace blossom {

enum class ActivationMode : uint8_t {
    Hold = 0,
    Toggle = 1,
};

struct ClickerConfig final {
    bool enabled = true;
    bool down_only = false;
    uint16_t output_vk = VK_LBUTTON;
    uint16_t hotkey_vk = 0;
    ActivationMode mode = ActivationMode::Hold;
    uint32_t cps = 100;
    uint32_t delay_ms = 10;
    uint32_t offset_ms = 0;
    uint32_t key_press_ms = 1;
};

class AdvancedClicker final {
public:
    AdvancedClicker();
    ~AdvancedClicker();

    AdvancedClicker(const AdvancedClicker&) = delete;
    AdvancedClicker& operator=(const AdvancedClicker&) = delete;

    void SetConfig(const ClickerConfig& config);
    ClickerConfig GetConfig() const;

    void SetActive(bool active);
    bool IsActive() const;

    uint64_t GetTotalClicks() const;
    void ResetCount();

private:
    void WorkerLoop();
    bool EmitAction(uint16_t vk, bool down_only, uint32_t key_press_ms, bool& held_down, uint16_t& held_vk);
    void ReleaseHeld(uint16_t vk, bool& held_down, uint16_t& held_vk);

    std::atomic<bool> running_{true};
    std::atomic<bool> active_{false};
    std::thread worker_{};

    mutable std::mutex config_lock_{};
    ClickerConfig config_{};

    std::atomic<uint64_t> total_clicks_{0};
    int64_t qpc_frequency_ = 1;
};

} // namespace blossom
