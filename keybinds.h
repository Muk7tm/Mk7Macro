#pragma once

#include <Windows.h>

#include <cstdint>
#include <map>
#include <string>

namespace blossom {

class KeyBindManager final {
public:
    void SetKeyBinding(int clicker_id, uint16_t vk_code);
    uint16_t GetKeyBinding(int clicker_id) const;

    bool IsDown(uint16_t vk_code) const;

    static std::wstring VkToText(uint16_t vk_code);
    static bool TextToVk(const std::wstring& text, uint16_t& out_vk_code);

private:
    std::map<int, uint16_t> bindings_{};
};

} // namespace blossom
