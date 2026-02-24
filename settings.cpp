#include "settings.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace blossom {
namespace {

constexpr uint32_t kMagic = 0x4D53374Du; // "M7SM"
constexpr uint32_t kVersion = 2u;

constexpr uint32_t kFlagEnabled = 1u << 0;
constexpr uint32_t kFlagDownOnly = 1u << 1;
constexpr uint32_t kFlagToggle = 1u << 2;

struct StoredClickerV1 final {
    uint16_t output_vk = 0;
    uint16_t hotkey_vk = 0;
    uint32_t cps = 0;
    uint32_t delay_ms = 0;
    uint32_t offset_ms = 0;
    uint32_t flags = 0;
};

struct StoredClicker final {
    uint16_t output_vk = 0;
    uint16_t hotkey_vk = 0;
    uint32_t cps = 0;
    uint32_t delay_ms = 0;
    uint32_t offset_ms = 0;
    uint32_t key_press_ms = 1;
    uint32_t flags = 0;
};

struct StoredSettingsV1 final {
    uint32_t magic = kMagic;
    uint32_t version = 1u;
    uint32_t global_flags = 0;
    std::array<StoredClickerV1, 4> clickers = {};
};

struct StoredSettings final {
    uint32_t magic = kMagic;
    uint32_t version = kVersion;
    uint32_t global_flags = 0;
    std::array<StoredClicker, 4> clickers = {};
};

ClickerConfig Normalize(const ClickerConfig& cfg) {
    ClickerConfig out = cfg;
    out.cps = std::clamp<uint32_t>(out.cps, 1u, 4000u);
    out.delay_ms = std::clamp<uint32_t>(out.delay_ms, 0u, 1000u);
    out.offset_ms = std::clamp<uint32_t>(out.offset_ms, 0u, 5000u);
    out.key_press_ms = std::clamp<uint32_t>(out.key_press_ms, 1u, 10u);
    if (out.output_vk == 0) {
        out.output_vk = VK_LBUTTON;
    }
    return out;
}

} // namespace

AppSettings DefaultSettings() {
    AppSettings settings{};
    settings.global_down_only = false;

    settings.clickers[0] = ClickerConfig{true, false, static_cast<uint16_t>('F'), static_cast<uint16_t>('X'), ActivationMode::Toggle, 167u, 6u, 0u, 1u};
    settings.clickers[1] = ClickerConfig{true, false, static_cast<uint16_t>(VK_LBUTTON), static_cast<uint16_t>(VK_XBUTTON1), ActivationMode::Toggle, 100u, 10u, 0u, 1u};
    settings.clickers[2] = ClickerConfig{false, false, static_cast<uint16_t>(VK_SPACE), static_cast<uint16_t>(VK_SPACE), ActivationMode::Hold, 300u, 10u, 0u, 1u};
    settings.clickers[3] = ClickerConfig{false, false, static_cast<uint16_t>('Q'), static_cast<uint16_t>('Q'), ActivationMode::Hold, 50u, 10u, 0u, 1u};

    for (auto& c : settings.clickers) {
        c = Normalize(c);
    }
    return settings;
}

bool LoadSettingsFile(const std::wstring& path, AppSettings& out_settings) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER file_size{};
    if (!GetFileSizeEx(file, &file_size)) {
        CloseHandle(file);
        return false;
    }

    AppSettings loaded{};
    const bool is_v2 = file_size.QuadPart == static_cast<LONGLONG>(sizeof(StoredSettings));
    const bool is_v1 = file_size.QuadPart == static_cast<LONGLONG>(sizeof(StoredSettingsV1));
    if (!is_v2 && !is_v1) {
        CloseHandle(file);
        return false;
    }

    DWORD read_bytes = 0;
    if (is_v2) {
        StoredSettings stored{};
        const BOOL ok = ReadFile(file, &stored, static_cast<DWORD>(sizeof(stored)), &read_bytes, nullptr);
        CloseHandle(file);
        if (!ok || read_bytes != sizeof(stored)) {
            return false;
        }
        if (stored.magic != kMagic || stored.version != kVersion) {
            return false;
        }

        loaded.global_down_only = (stored.global_flags & kFlagDownOnly) != 0;
        for (size_t i = 0; i < loaded.clickers.size(); ++i) {
            ClickerConfig cfg{};
            cfg.output_vk = stored.clickers[i].output_vk;
            cfg.hotkey_vk = stored.clickers[i].hotkey_vk;
            cfg.cps = stored.clickers[i].cps;
            cfg.delay_ms = stored.clickers[i].delay_ms;
            cfg.offset_ms = stored.clickers[i].offset_ms;
            cfg.key_press_ms = stored.clickers[i].key_press_ms;
            cfg.enabled = (stored.clickers[i].flags & kFlagEnabled) != 0;
            cfg.down_only = (stored.clickers[i].flags & kFlagDownOnly) != 0;
            cfg.mode = (stored.clickers[i].flags & kFlagToggle) ? ActivationMode::Toggle : ActivationMode::Hold;
            loaded.clickers[i] = Normalize(cfg);
        }
    } else {
        StoredSettingsV1 stored{};
        const BOOL ok = ReadFile(file, &stored, static_cast<DWORD>(sizeof(stored)), &read_bytes, nullptr);
        CloseHandle(file);
        if (!ok || read_bytes != sizeof(stored)) {
            return false;
        }
        if (stored.magic != kMagic || stored.version != 1u) {
            return false;
        }

        loaded.global_down_only = (stored.global_flags & kFlagDownOnly) != 0;
        for (size_t i = 0; i < loaded.clickers.size(); ++i) {
            ClickerConfig cfg{};
            cfg.output_vk = stored.clickers[i].output_vk;
            cfg.hotkey_vk = stored.clickers[i].hotkey_vk;
            cfg.cps = stored.clickers[i].cps;
            cfg.delay_ms = stored.clickers[i].delay_ms;
            cfg.offset_ms = stored.clickers[i].offset_ms;
            cfg.key_press_ms = 1u;
            cfg.enabled = (stored.clickers[i].flags & kFlagEnabled) != 0;
            cfg.down_only = (stored.clickers[i].flags & kFlagDownOnly) != 0;
            cfg.mode = (stored.clickers[i].flags & kFlagToggle) ? ActivationMode::Toggle : ActivationMode::Hold;
            loaded.clickers[i] = Normalize(cfg);
        }
    }

    out_settings = loaded;
    return true;
}

bool SaveSettingsFile(const std::wstring& path, const AppSettings& settings) {
    StoredSettings stored{};
    stored.magic = kMagic;
    stored.version = kVersion;
    stored.global_flags = settings.global_down_only ? kFlagDownOnly : 0u;

    for (size_t i = 0; i < settings.clickers.size(); ++i) {
        const ClickerConfig cfg = Normalize(settings.clickers[i]);
        stored.clickers[i].output_vk = cfg.output_vk;
        stored.clickers[i].hotkey_vk = cfg.hotkey_vk;
        stored.clickers[i].cps = cfg.cps;
        stored.clickers[i].delay_ms = cfg.delay_ms;
        stored.clickers[i].offset_ms = cfg.offset_ms;
        stored.clickers[i].key_press_ms = cfg.key_press_ms;
        stored.clickers[i].flags =
            (cfg.enabled ? kFlagEnabled : 0u) |
            (cfg.down_only ? kFlagDownOnly : 0u) |
            (cfg.mode == ActivationMode::Toggle ? kFlagToggle : 0u);
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written_bytes = 0;
    const BOOL ok = WriteFile(file, &stored, static_cast<DWORD>(sizeof(stored)), &written_bytes, nullptr);
    CloseHandle(file);
    return ok && written_bytes == sizeof(stored);
}

} // namespace blossom
