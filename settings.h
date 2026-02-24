#pragma once

#include "clicker.h"

#include <array>
#include <string>

namespace blossom {

struct AppSettings final {
    std::array<ClickerConfig, 4> clickers = {};
    bool global_down_only = false;
};

AppSettings DefaultSettings();

bool LoadSettingsFile(const std::wstring& path, AppSettings& out_settings);
bool SaveSettingsFile(const std::wstring& path, const AppSettings& settings);

} // namespace blossom
