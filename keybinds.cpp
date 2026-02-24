#include "keybinds.h"

#include <algorithm>
#include <cwctype>

namespace blossom {
namespace {

std::wstring UpperTrim(std::wstring s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](wchar_t c) { return c == L' ' || c == L'\t'; }), s.end());
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
    return s;
}

bool ParseUInt(const std::wstring& text, uint32_t& out) {
    if (text.empty()) {
        return false;
    }
    wchar_t* end = nullptr;
    const unsigned long v = wcstoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != L'\0') {
        return false;
    }
    out = static_cast<uint32_t>(v);
    return true;
}

} // namespace

void KeyBindManager::SetKeyBinding(int clicker_id, uint16_t vk_code) {
    bindings_[clicker_id] = vk_code;
}

uint16_t KeyBindManager::GetKeyBinding(int clicker_id) const {
    const auto it = bindings_.find(clicker_id);
    if (it == bindings_.end()) {
        return 0;
    }
    return it->second;
}

bool KeyBindManager::IsDown(uint16_t vk_code) const {
    if (vk_code == 0) {
        return false;
    }
    return (GetAsyncKeyState(static_cast<int>(vk_code)) & 0x8000) != 0;
}

std::wstring KeyBindManager::VkToText(uint16_t vk_code) {
    if (vk_code >= 'A' && vk_code <= 'Z') return std::wstring(1, static_cast<wchar_t>(vk_code));
    if (vk_code >= '0' && vk_code <= '9') return std::wstring(1, static_cast<wchar_t>(vk_code));
    if (vk_code >= VK_F1 && vk_code <= VK_F24) return L"F" + std::to_wstring(vk_code - VK_F1 + 1);

    switch (vk_code) {
    case VK_LBUTTON: return L"LMB";
    case VK_RBUTTON: return L"RMB";
    case VK_MBUTTON: return L"MMB";
    case VK_XBUTTON1: return L"XBUTTON1";
    case VK_XBUTTON2: return L"XBUTTON2";
    case VK_SPACE: return L"SPACE";
    case VK_RETURN: return L"ENTER";
    case VK_TAB: return L"TAB";
    case VK_ESCAPE: return L"ESC";
    default: return L"VK_" + std::to_wstring(vk_code);
    }
}

bool KeyBindManager::TextToVk(const std::wstring& text, uint16_t& out_vk_code) {
    const std::wstring token = UpperTrim(text);
    if (token.empty()) {
        return false;
    }

    if (token.size() == 1) {
        const wchar_t c = token[0];
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) {
            out_vk_code = static_cast<uint16_t>(c);
            return true;
        }
    }

    if (token[0] == L'F' && token.size() <= 3) {
        uint32_t fn = 0;
        if (ParseUInt(token.substr(1), fn) && fn >= 1 && fn <= 24) {
            out_vk_code = static_cast<uint16_t>(VK_F1 + fn - 1);
            return true;
        }
    }

    if (token == L"LMB") { out_vk_code = VK_LBUTTON; return true; }
    if (token == L"RMB") { out_vk_code = VK_RBUTTON; return true; }
    if (token == L"MMB") { out_vk_code = VK_MBUTTON; return true; }
    if (token == L"XBUTTON1") { out_vk_code = VK_XBUTTON1; return true; }
    if (token == L"XBUTTON2") { out_vk_code = VK_XBUTTON2; return true; }
    if (token == L"SPACE") { out_vk_code = VK_SPACE; return true; }
    if (token == L"ENTER") { out_vk_code = VK_RETURN; return true; }
    if (token == L"TAB") { out_vk_code = VK_TAB; return true; }
    if (token == L"ESC") { out_vk_code = VK_ESCAPE; return true; }

    return false;
}

} // namespace blossom
