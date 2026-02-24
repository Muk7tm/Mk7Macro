#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <commctrl.h>
#include <mmsystem.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

#include "clicker.h"
#include "keybinds.h"
#include "monitor.h"
#include "settings.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")

namespace blossom {
namespace {

constexpr wchar_t kWindowClassName[] = L"MK7MacroWindowClass";
constexpr wchar_t kWindowTitle[] = L"Mk7Macro";
constexpr int kClickerCount = 4;

constexpr UINT_PTR kTimerPoll = 1;
constexpr UINT_PTR kTimerUi = 2;
constexpr int kPollMs = 1;
constexpr int kUiRefreshMs = 120;

constexpr int kIdCardStartBase = 1000;
constexpr int kIdCardSettingsBase = 1100;
constexpr int kIdCardDelayBase = 1200;
constexpr int kIdCardKeyPressBase = 1300;
constexpr int kIdGlobalDownOnly = 2000;
constexpr int kIdStartAll = 2001;
constexpr int kIdStopAll = 2002;
constexpr int kIdSave = 2003;
constexpr int kIdDefaults = 2004;
constexpr int kIdApply = 2005;
constexpr int kIdRecord = 2006;
constexpr int kIdEditCps = 2100;
constexpr int kIdEditDelay = 2101;
constexpr int kIdEditOffset = 2102;
constexpr int kIdEditOutput = 2103;
constexpr int kIdEditHotkey = 2104;
constexpr int kIdComboMode = 2105;
constexpr int kIdCheckEnabled = 2106;
constexpr int kIdEditKeyPressMs = 2107;
constexpr int kIdModePopupHold = 2201;
constexpr int kIdModePopupToggle = 2202;

constexpr COLORREF kColorBg = RGB(10, 12, 18);
constexpr COLORREF kColorPanel = RGB(21, 30, 35);
constexpr COLORREF kColorCard = RGB(17, 30, 30);
constexpr COLORREF kColorInput = RGB(29, 34, 40);
constexpr COLORREF kColorText = RGB(236, 241, 248);
constexpr COLORREF kColorTextMuted = RGB(158, 168, 186);
constexpr COLORREF kColorAccentCyan = RGB(0, 217, 255);
constexpr COLORREF kColorAccentGreen = RGB(0, 255, 136);
constexpr COLORREF kColorButtonSecondary = RGB(42, 53, 64);
constexpr COLORREF kColorButtonDanger = RGB(255, 68, 68);
constexpr COLORREF kColorBorder = RGB(42, 53, 64);
constexpr COLORREF kColorPanelBorder = RGB(39, 49, 62);
constexpr COLORREF kColorCardBorder = RGB(32, 57, 57);
constexpr COLORREF kColorShadow = RGB(6, 9, 14);

enum class ButtonVisualStyle : uint8_t {
    Primary,
    Secondary,
    Danger,
    DynamicStartStop,
    InputDropdown,
    Checkbox
};

struct ButtonVisualEntry final {
    HWND handle = nullptr;
    ButtonVisualStyle style = ButtonVisualStyle::Secondary;
};

HMENU MenuId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

int ClampInt(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

int ClampChannel(int v) {
    return std::max(0, std::min(255, v));
}

COLORREF OffsetColor(COLORREF color, int delta) {
    return RGB(
        ClampChannel(static_cast<int>(GetRValue(color)) + delta),
        ClampChannel(static_cast<int>(GetGValue(color)) + delta),
        ClampChannel(static_cast<int>(GetBValue(color)) + delta));
}

COLORREF BlendColor(COLORREF a, COLORREF b, uint8_t mix_a) {
    const int mix_b = 255 - static_cast<int>(mix_a);
    return RGB(
        (GetRValue(a) * mix_a + GetRValue(b) * mix_b) / 255,
        (GetGValue(a) * mix_a + GetGValue(b) * mix_b) / 255,
        (GetBValue(a) * mix_a + GetBValue(b) * mix_b) / 255);
}

int ReadEditInt(HWND wnd, int id, int fallback, int lo, int hi) {
    BOOL ok = FALSE;
    const UINT value = GetDlgItemInt(wnd, id, &ok, FALSE);
    if (!ok) {
        return fallback;
    }
    return ClampInt(static_cast<int>(value), lo, hi);
}

std::wstring ModuleSiblingPath(const wchar_t* file_name) {
    wchar_t module_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, module_path, static_cast<DWORD>(std::size(module_path)));
    std::wstring out(module_path);
    const size_t slash = out.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        out.erase(slash + 1);
    }
    out += file_name;
    return out;
}

std::wstring CompactPathForFooter(const std::wstring& path, size_t max_chars) {
    if (path.size() <= max_chars) {
        return path;
    }

    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        const std::wstring tail = path.substr(slash + 1);
        if (tail.size() + 4 < max_chars) {
            const size_t head_keep = max_chars - tail.size() - 4;
            return path.substr(0, head_keep) + L"...\\" + tail;
        }
    }

    const size_t left_keep = (max_chars - 3) / 2;
    const size_t right_keep = max_chars - 3 - left_keep;
    return path.substr(0, left_keep) + L"..." + path.substr(path.size() - right_keep);
}

struct CardControls final {
    HWND title = nullptr;
    HWND detail = nullptr;
    HWND bind = nullptr;
    HWND lbl_delay = nullptr;
    HWND ed_delay = nullptr;
    HWND lbl_key_press = nullptr;
    HWND ed_key_press = nullptr;
    HWND start = nullptr;
    HWND settings = nullptr;
};

class App final {
public:
    bool Init(HINSTANCE instance);
    int Run();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp);

    int Scale(int px) const;
    void CreateFonts();
    void DestroyFonts();
    void ApplyFonts();
    void ApplyEditMargins(HWND edit);
    void RegisterButton(HWND button, ButtonVisualStyle style);
    bool IsManagedButton(HWND button) const;
    ButtonVisualStyle ResolveButtonStyle(HWND button) const;
    HBRUSH ResolveSurfaceBrush(HWND control, COLORREF& back_color) const;
    void UpdateHoveredButton();
    void DrawRoundedRect(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF border) const;
    void DrawSurfaceCard(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius) const;
    void DrawInputFrame(HDC dc, HWND control) const;
    void DrawCustomButton(const DRAWITEMSTRUCT& dis) const;
    void DrawStatText(HDC dc, size_t index, COLORREF value_color) const;

    void BuildUi();
    void LayoutUi();
    void PaintUi(HDC dc);
    void RefreshUi();
    void RefreshCard(size_t index);
    void RefreshEditor();
    void ApplyCardEditor(size_t index);
    void ApplyEditor();
    void PollHotkeys();
    void ApplyGlobalDownOnly(bool enabled);
    void LoadOrDefaultSettings();
    void ApplySettingsToEngine();
    bool SaveSettingsNow();
    void ResetDefaults();
    bool HandleRecordKey(uint16_t vk);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    int dpi_ = USER_DEFAULT_SCREEN_DPI;
    HICON icon_large_ = nullptr;
    HICON icon_small_ = nullptr;
    bool owns_icon_large_ = false;
    bool owns_icon_small_ = false;

    std::array<AdvancedClicker, kClickerCount> clickers_{};
    ClickerMonitor monitor_{};
    KeyBindManager keybinds_{};
    AppSettings settings_{};
    std::wstring settings_path_{};

    size_t selected_clicker_ = 0;
    std::array<bool, kClickerCount> manual_on_ = {};
    std::array<bool, kClickerCount> toggle_latched_ = {};
    std::array<bool, kClickerCount> previous_down_ = {};
    bool recording_hotkey_ = false;
    int mode_selection_ = 0;

    HFONT font_title_ = nullptr;
    HFONT font_section_ = nullptr;
    HFONT font_body_ = nullptr;
    HFONT font_label_ = nullptr;
    HFONT font_small_ = nullptr;
    HFONT font_value_ = nullptr;
    HFONT font_hint_ = nullptr;
    HBRUSH brush_bg_ = nullptr;
    HBRUSH brush_panel_ = nullptr;
    HBRUSH brush_card_ = nullptr;
    HBRUSH brush_input_ = nullptr;

    std::array<ButtonVisualEntry, kClickerCount * 2 + 10> managed_buttons_ = {};
    size_t managed_button_count_ = 0;
    HWND hovered_button_ = nullptr;
    bool tracking_mouse_leave_ = false;

    RECT header_divider_rect_ = {};
    RECT stats_rects_[3] = {};
    RECT card_rects_[kClickerCount] = {};
    RECT down_rect_ = {};
    RECT editor_rect_ = {};
    RECT editor_timing_group_ = {};
    RECT editor_bind_group_ = {};
    RECT editor_actions_group_ = {};
    RECT footer_rect_ = {};

    std::array<std::wstring, 3> stat_labels_ = {L"ACTIVE", L"TOTAL", L"STATUS"};
    std::array<std::wstring, 3> stat_values_ = {};
    std::array<std::wstring, 3> stat_hints_ = {};
    std::wstring footer_text_{};

    HWND st_header_title_ = nullptr;
    HWND st_header_subtitle_ = nullptr;
    HWND st_active_ = nullptr;
    HWND st_total_ = nullptr;
    HWND st_status_ = nullptr;
    HWND st_section_auto_ = nullptr;

    std::array<CardControls, kClickerCount> cards_ = {};
    HWND chk_down_only_ = nullptr;
    HWND st_down_hint_ = nullptr;

    HWND st_editor_title_ = nullptr;
    HWND st_lbl_cps_ = nullptr;
    HWND st_lbl_delay_ = nullptr;
    HWND st_lbl_offset_ = nullptr;
    HWND st_lbl_key_press_ = nullptr;
    HWND st_lbl_output_ = nullptr;
    HWND st_lbl_hotkey_ = nullptr;
    HWND st_lbl_mode_ = nullptr;

    HWND ed_cps_ = nullptr;
    HWND ed_delay_ = nullptr;
    HWND ed_offset_ = nullptr;
    HWND ed_key_press_ = nullptr;
    HWND ed_output_ = nullptr;
    HWND ed_hotkey_ = nullptr;
    HWND cb_mode_ = nullptr;
    HWND chk_enabled_ = nullptr;

    HWND btn_record_ = nullptr;
    HWND btn_apply_ = nullptr;
    HWND btn_start_all_ = nullptr;
    HWND btn_stop_all_ = nullptr;
    HWND btn_save_ = nullptr;
    HWND btn_defaults_ = nullptr;

    HWND st_footer_ = nullptr;
};

bool App::Init(HINSTANCE instance) {
    instance_ = instance;
    settings_path_ = ModuleSiblingPath(L"mk7macro_settings.bin");

    // Improve scheduler precision and process responsiveness for macro timing.
    timeBeginPeriod(1);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &WindowProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    const std::wstring icon_path = ModuleSiblingPath(L"blossom.ico");
    icon_large_ = reinterpret_cast<HICON>(LoadImageW(nullptr, icon_path.c_str(), IMAGE_ICON,
                                                     GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE));
    icon_small_ = reinterpret_cast<HICON>(LoadImageW(nullptr, icon_path.c_str(), IMAGE_ICON,
                                                     GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE));
    owns_icon_large_ = icon_large_ != nullptr;
    owns_icon_small_ = icon_small_ != nullptr;
    if (!icon_large_) {
        icon_large_ = LoadIconW(nullptr, IDI_APPLICATION);
    }
    if (!icon_small_) {
        icon_small_ = LoadIconW(nullptr, IDI_APPLICATION);
    }
    wc.hIcon = icon_large_;
    wc.hIconSm = icon_small_;
    if (!RegisterClassExW(&wc)) {
        return false;
    }

    hwnd_ = CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1360,
        920,
        nullptr,
        nullptr,
        instance_,
        this);
    if (!hwnd_) {
        return false;
    }
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon_large_));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon_small_));

    dpi_ = GetDpiForWindow(hwnd_);
    CreateFonts();

    brush_bg_ = CreateSolidBrush(kColorBg);
    brush_panel_ = CreateSolidBrush(kColorPanel);
    brush_card_ = CreateSolidBrush(kColorCard);
    brush_input_ = CreateSolidBrush(kColorInput);

    BuildUi();
    LayoutUi();
    LoadOrDefaultSettings();
    ApplySettingsToEngine();
    monitor_.ResetSession();
    RefreshEditor();
    RefreshUi();
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);

    SetTimer(hwnd_, kTimerPoll, kPollMs, nullptr);
    SetTimer(hwnd_, kTimerUi, kUiRefreshMs, nullptr);
    return true;
}

int App::Run() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

int App::Scale(int px) const {
    return MulDiv(px, dpi_, 96);
}

void App::CreateFonts() {
    DestroyFonts();

    font_title_ = CreateFontW(-Scale(42), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    font_section_ = CreateFontW(-Scale(20), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    font_body_ = CreateFontW(-Scale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    font_label_ = CreateFontW(-Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    font_small_ = CreateFontW(-Scale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    font_value_ = CreateFontW(-Scale(32), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    font_hint_ = CreateFontW(-Scale(13), 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

void App::DestroyFonts() {
    if (font_title_) {
        DeleteObject(font_title_);
        font_title_ = nullptr;
    }
    if (font_section_) {
        DeleteObject(font_section_);
        font_section_ = nullptr;
    }
    if (font_body_) {
        DeleteObject(font_body_);
        font_body_ = nullptr;
    }
    if (font_label_) {
        DeleteObject(font_label_);
        font_label_ = nullptr;
    }
    if (font_small_) {
        DeleteObject(font_small_);
        font_small_ = nullptr;
    }
    if (font_value_) {
        DeleteObject(font_value_);
        font_value_ = nullptr;
    }
    if (font_hint_) {
        DeleteObject(font_hint_);
        font_hint_ = nullptr;
    }
}

void App::ApplyFonts() {
    auto apply_font = [this](HWND control, HFONT font) {
        if (control && font) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    };

    apply_font(st_header_title_, font_title_);
    apply_font(st_header_subtitle_, font_body_);
    apply_font(st_section_auto_, font_section_);
    apply_font(st_editor_title_, font_section_);
    apply_font(st_down_hint_, font_hint_);
    apply_font(chk_down_only_, font_body_);
    apply_font(chk_enabled_, font_body_);

    apply_font(st_lbl_cps_, font_label_);
    apply_font(st_lbl_delay_, font_label_);
    apply_font(st_lbl_offset_, font_label_);
    apply_font(st_lbl_key_press_, font_label_);
    apply_font(st_lbl_output_, font_label_);
    apply_font(st_lbl_hotkey_, font_label_);
    apply_font(st_lbl_mode_, font_label_);

    apply_font(ed_cps_, font_body_);
    apply_font(ed_delay_, font_body_);
    apply_font(ed_offset_, font_body_);
    apply_font(ed_key_press_, font_body_);
    apply_font(ed_output_, font_body_);
    apply_font(ed_hotkey_, font_body_);
    apply_font(cb_mode_, font_body_);

    apply_font(btn_record_, font_body_);
    apply_font(btn_apply_, font_body_);
    apply_font(btn_start_all_, font_body_);
    apply_font(btn_stop_all_, font_body_);
    apply_font(btn_save_, font_body_);
    apply_font(btn_defaults_, font_body_);

    for (size_t i = 0; i < kClickerCount; ++i) {
        apply_font(cards_[i].title, font_section_);
        apply_font(cards_[i].detail, font_small_);
        apply_font(cards_[i].bind, font_small_);
        apply_font(cards_[i].lbl_delay, font_label_);
        apply_font(cards_[i].ed_delay, font_body_);
        apply_font(cards_[i].lbl_key_press, font_label_);
        apply_font(cards_[i].ed_key_press, font_body_);
        apply_font(cards_[i].start, font_body_);
        apply_font(cards_[i].settings, font_body_);
    }
}

void App::ApplyEditMargins(HWND edit) {
    if (!edit) {
        return;
    }
    const int margin = Scale(6);
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(margin, margin));
}

void App::RegisterButton(HWND button, ButtonVisualStyle style) {
    if (!button || managed_button_count_ >= managed_buttons_.size()) {
        return;
    }
    managed_buttons_[managed_button_count_++] = {button, style};
}

bool App::IsManagedButton(HWND button) const {
    for (size_t i = 0; i < managed_button_count_; ++i) {
        if (managed_buttons_[i].handle == button) {
            return true;
        }
    }
    return false;
}

ButtonVisualStyle App::ResolveButtonStyle(HWND button) const {
    for (size_t i = 0; i < managed_button_count_; ++i) {
        if (managed_buttons_[i].handle == button) {
            return managed_buttons_[i].style;
        }
    }
    return ButtonVisualStyle::Secondary;
}

HBRUSH App::ResolveSurfaceBrush(HWND control, COLORREF& back_color) const {
    RECT wr{};
    if (!control || !GetWindowRect(control, &wr)) {
        back_color = kColorBg;
        return brush_bg_;
    }

    MapWindowPoints(nullptr, hwnd_, reinterpret_cast<LPPOINT>(&wr), 2);
    POINT center = {(wr.left + wr.right) / 2, (wr.top + wr.bottom) / 2};

    for (size_t i = 0; i < kClickerCount; ++i) {
        if (PtInRect(&card_rects_[i], center)) {
            back_color = kColorCard;
            return brush_card_;
        }
    }

    for (const RECT& stat : stats_rects_) {
        if (PtInRect(&stat, center)) {
            back_color = kColorPanel;
            return brush_panel_;
        }
    }

    if (PtInRect(&down_rect_, center) || PtInRect(&editor_rect_, center) || PtInRect(&footer_rect_, center)) {
        back_color = kColorPanel;
        return brush_panel_;
    }

    back_color = kColorBg;
    return brush_bg_;
}

void App::UpdateHoveredButton() {
    POINT pt{};
    if (!GetCursorPos(&pt)) {
        return;
    }

    HWND hovered = WindowFromPoint(pt);
    if (hovered && GetAncestor(hovered, GA_ROOT) != hwnd_) {
        hovered = nullptr;
    }
    if (!IsManagedButton(hovered)) {
        hovered = nullptr;
    }

    if (hovered == hovered_button_) {
        return;
    }

    if (hovered_button_) {
        InvalidateRect(hovered_button_, nullptr, FALSE);
    }
    hovered_button_ = hovered;
    if (hovered_button_) {
        InvalidateRect(hovered_button_, nullptr, FALSE);
    }
}

void App::DrawRoundedRect(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF border) const {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius * 2, radius * 2);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void App::DrawSurfaceCard(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius) const {
    RECT shadow = rect;
    const int shadow_offset = Scale(2);
    OffsetRect(&shadow, 0, shadow_offset);
    DrawRoundedRect(dc, shadow, radius, BlendColor(kColorShadow, kColorBg, 225), BlendColor(kColorShadow, kColorBg, 225));
    DrawRoundedRect(dc, rect, radius, fill, border);
}

void App::DrawInputFrame(HDC dc, HWND control) const {
    if (!control || !IsWindowVisible(control)) {
        return;
    }
    RECT rect{};
    GetWindowRect(control, &rect);
    MapWindowPoints(nullptr, hwnd_, reinterpret_cast<LPPOINT>(&rect), 2);
    InflateRect(&rect, Scale(1), Scale(1));
    DrawRoundedRect(dc, rect, Scale(4), kColorInput, kColorBorder);
}

void App::DrawCustomButton(const DRAWITEMSTRUCT& dis) const {
    if (dis.CtlType != ODT_BUTTON || !IsManagedButton(dis.hwndItem)) {
        return;
    }

    ButtonVisualStyle style = ResolveButtonStyle(dis.hwndItem);
    if (style == ButtonVisualStyle::DynamicStartStop) {
        wchar_t text[32] = {};
        GetWindowTextW(dis.hwndItem, text, static_cast<int>(std::size(text)));
        style = (wcsncmp(text, L"Stop", 4) == 0) ? ButtonVisualStyle::Danger : ButtonVisualStyle::Primary;
    }

    const bool pressed = (dis.itemState & ODS_SELECTED) != 0;
    const bool disabled = (dis.itemState & ODS_DISABLED) != 0;
    const bool hovered = dis.hwndItem == hovered_button_;

    wchar_t text[64] = {};
    GetWindowTextW(dis.hwndItem, text, static_cast<int>(std::size(text)));
    if (text[0] == L'\0') {
        for (size_t i = 0; i < kClickerCount; ++i) {
            if (dis.hwndItem == cards_[i].start) {
                wcscpy_s(text, L"Start");
                break;
            }
            if (dis.hwndItem == cards_[i].settings) {
                wcscpy_s(text, L"Settings");
                break;
            }
        }
    }

    if (style == ButtonVisualStyle::Checkbox) {
        COLORREF back_color = kColorBg;
        HBRUSH surface = ResolveSurfaceBrush(dis.hwndItem, back_color);
        FillRect(dis.hDC, &dis.rcItem, surface);

        RECT box = dis.rcItem;
        box.left += Scale(2);
        box.top += Scale(2);
        const int box_size = std::max(Scale(16), static_cast<int>(dis.rcItem.bottom - dis.rcItem.top) - Scale(4));
        box.right = box.left + box_size;
        box.bottom = box.top + box_size;

        bool checked = false;
        if (dis.hwndItem == chk_down_only_) {
            checked = settings_.global_down_only;
        } else if (dis.hwndItem == chk_enabled_) {
            checked = settings_.clickers[selected_clicker_].enabled;
        } else {
            checked = SendMessageW(dis.hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED;
        }

        COLORREF box_fill = checked ? kColorAccentCyan : kColorInput;
        COLORREF box_border = checked ? OffsetColor(kColorAccentCyan, -18) : (hovered ? OffsetColor(kColorBorder, 24) : kColorBorder);
        if (pressed) {
            box_fill = checked ? OffsetColor(box_fill, -12) : OffsetColor(kColorInput, -10);
        }
        DrawRoundedRect(dis.hDC, box, Scale(3), box_fill, box_border);

        if (checked) {
            const int mark_thickness = std::max(2, Scale(2));
            const COLORREF mark_color = RGB(245, 252, 255);
            HPEN pen = CreatePen(PS_SOLID, mark_thickness, mark_color);
            HGDIOBJ old_pen = SelectObject(dis.hDC, pen);
            MoveToEx(dis.hDC, box.left + Scale(3), box.top + ((box.bottom - box.top) / 2), nullptr);
            LineTo(dis.hDC, box.left + ((box.right - box.left) / 2) - Scale(1), box.bottom - Scale(4));
            LineTo(dis.hDC, box.right - Scale(3), box.top + Scale(4));
            SelectObject(dis.hDC, old_pen);
            DeleteObject(pen);
        }

        HFONT old_font = reinterpret_cast<HFONT>(SelectObject(dis.hDC, font_body_));
        SetBkMode(dis.hDC, TRANSPARENT);
        SetTextColor(dis.hDC, disabled ? kColorTextMuted : kColorText);
        RECT text_rect = dis.rcItem;
        text_rect.left = box.right + Scale(8);
        DrawTextW(dis.hDC, text, -1, &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dis.hDC, old_font);
    } else {
        COLORREF base = kColorButtonSecondary;
        COLORREF text_color = kColorText;
        bool draw_shadow = true;
        int radius = Scale(7);
        if (style == ButtonVisualStyle::Primary) {
            base = kColorAccentCyan;
            text_color = RGB(7, 16, 22);
        } else if (style == ButtonVisualStyle::Secondary) {
            base = kColorButtonSecondary;
            text_color = kColorText;
        } else if (style == ButtonVisualStyle::Danger) {
            base = kColorButtonDanger;
            text_color = RGB(25, 6, 6);
        } else if (style == ButtonVisualStyle::InputDropdown) {
            base = kColorInput;
            text_color = kColorText;
            draw_shadow = false;
            radius = Scale(4);
        }

        COLORREF fill = base;
        COLORREF border = (style == ButtonVisualStyle::InputDropdown) ? kColorBorder : OffsetColor(base, -16);
        if (disabled) {
            fill = BlendColor(base, kColorPanel, 120);
            border = BlendColor(border, kColorPanel, 120);
            text_color = BlendColor(text_color, kColorTextMuted, 90);
        } else if (pressed) {
            fill = OffsetColor(base, (style == ButtonVisualStyle::InputDropdown) ? -8 : -30);
            border = OffsetColor(fill, (style == ButtonVisualStyle::InputDropdown) ? -6 : -14);
        } else if (hovered) {
            fill = OffsetColor(base, (style == ButtonVisualStyle::InputDropdown) ? 8 : 18);
            border = OffsetColor(fill, (style == ButtonVisualStyle::InputDropdown) ? -4 : -10);
        }

        if (draw_shadow) {
            RECT shadow = dis.rcItem;
            OffsetRect(&shadow, 0, Scale(1));
            DrawRoundedRect(dis.hDC, shadow, radius, BlendColor(kColorShadow, kColorBg, 205), BlendColor(kColorShadow, kColorBg, 205));
        }
        DrawRoundedRect(dis.hDC, dis.rcItem, radius, fill, border);

        SetBkMode(dis.hDC, TRANSPARENT);
        SetTextColor(dis.hDC, text_color);
        HFONT old_font = reinterpret_cast<HFONT>(SelectObject(dis.hDC, font_body_));
        RECT text_rect = dis.rcItem;
        UINT format = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
        if (style == ButtonVisualStyle::InputDropdown) {
            text_rect.left += Scale(10);
            text_rect.right -= Scale(20);
            format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
        }
        DrawTextW(dis.hDC, text, -1, &text_rect, format);

        if (style == ButtonVisualStyle::InputDropdown) {
            const int cx = dis.rcItem.right - Scale(12);
            const int cy = (dis.rcItem.top + dis.rcItem.bottom) / 2;
            POINT pts[3] = {
                {cx - Scale(4), cy - Scale(2)},
                {cx + Scale(4), cy - Scale(2)},
                {cx, cy + Scale(3)}
            };
            HBRUSH arrow_brush = CreateSolidBrush(disabled ? kColorTextMuted : kColorText);
            HPEN arrow_pen = CreatePen(PS_SOLID, 1, disabled ? kColorTextMuted : kColorText);
            HGDIOBJ old_brush = SelectObject(dis.hDC, arrow_brush);
            HGDIOBJ old_pen = SelectObject(dis.hDC, arrow_pen);
            Polygon(dis.hDC, pts, 3);
            SelectObject(dis.hDC, old_pen);
            SelectObject(dis.hDC, old_brush);
            DeleteObject(arrow_pen);
            DeleteObject(arrow_brush);
        }
        SelectObject(dis.hDC, old_font);
    }

    if ((dis.itemState & ODS_FOCUS) != 0) {
        RECT focus = dis.rcItem;
        InflateRect(&focus, -Scale(2), -Scale(2));
        const COLORREF focus_color = BlendColor(kColorAccentCyan, kColorText, 188);
        HPEN pen = CreatePen(PS_SOLID, std::max(1, Scale(2)), focus_color);
        HGDIOBJ old_pen = SelectObject(dis.hDC, pen);
        HGDIOBJ old_brush = SelectObject(dis.hDC, GetStockObject(NULL_BRUSH));
        const int radius = (style == ButtonVisualStyle::Checkbox || style == ButtonVisualStyle::InputDropdown) ? Scale(4) : Scale(6);
        RoundRect(dis.hDC, focus.left, focus.top, focus.right, focus.bottom, radius * 2, radius * 2);
        SelectObject(dis.hDC, old_brush);
        SelectObject(dis.hDC, old_pen);
        DeleteObject(pen);
    }
}

void App::DrawStatText(HDC dc, size_t index, COLORREF value_color) const {
    if (index >= 3) {
        return;
    }

    const RECT& panel = stats_rects_[index];
    const int inset = Scale(14);
    RECT label_rect = {panel.left + inset, panel.top + inset, panel.right - inset, panel.top + inset + Scale(20)};
    RECT value_rect = {panel.left + inset, label_rect.bottom + Scale(2), panel.right - inset, label_rect.bottom + Scale(44)};
    RECT hint_rect = {panel.left + inset, panel.bottom - Scale(28), panel.right - inset, panel.bottom - Scale(8)};

    SetBkMode(dc, TRANSPARENT);
    HFONT old_font = reinterpret_cast<HFONT>(SelectObject(dc, font_label_));
    SetTextColor(dc, kColorTextMuted);
    DrawTextW(dc, stat_labels_[index].c_str(), -1, &label_rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    SelectObject(dc, font_value_);
    SetTextColor(dc, value_color);
    DrawTextW(dc, stat_values_[index].c_str(), -1, &value_rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    SelectObject(dc, font_small_);
    SetTextColor(dc, kColorText);
    DrawTextW(dc, stat_hints_[index].c_str(), -1, &hint_rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, old_font);
}

void App::BuildUi() {
    managed_button_count_ = 0;
    hovered_button_ = nullptr;

    st_header_title_ = CreateWindowExW(0, L"STATIC", L"Mk7Macro", WS_CHILD | WS_VISIBLE, 0, 0, 220, 42, hwnd_, nullptr, instance_, nullptr);
    st_header_subtitle_ = CreateWindowExW(0, L"STATIC", L"Auto clickers tuned for low-latency output", WS_CHILD | WS_VISIBLE, 0, 0, 520, 22, hwnd_, nullptr, instance_, nullptr);

    // Keep hidden legacy stat/footer statics as data holders while rendering text manually.
    st_active_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 200, 70, hwnd_, nullptr, instance_, nullptr);
    st_total_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 200, 70, hwnd_, nullptr, instance_, nullptr);
    st_status_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 200, 70, hwnd_, nullptr, instance_, nullptr);
    st_section_auto_ = CreateWindowExW(0, L"STATIC", L"Auto Clickers", WS_CHILD | WS_VISIBLE, 0, 0, 220, 24, hwnd_, nullptr, instance_, nullptr);

    for (size_t i = 0; i < kClickerCount; ++i) {
        cards_[i].title = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 220, 24, hwnd_, nullptr, instance_, nullptr);
        cards_[i].detail = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 220, 20, hwnd_, nullptr, instance_, nullptr);
        cards_[i].bind = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 220, 20, hwnd_, nullptr, instance_, nullptr);
        cards_[i].lbl_delay = CreateWindowExW(0, L"STATIC", L"Delay (ms)", WS_CHILD | WS_VISIBLE, 0, 0, 76, 18, hwnd_, nullptr, instance_, nullptr);
        cards_[i].ed_delay = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                                             0, 0, 88, 24, hwnd_, MenuId(kIdCardDelayBase + static_cast<int>(i)), instance_, nullptr);
        cards_[i].lbl_key_press = CreateWindowExW(0, L"STATIC", L"Key Press (ms)", WS_CHILD | WS_VISIBLE, 0, 0, 92, 18, hwnd_, nullptr, instance_, nullptr);
        cards_[i].ed_key_press = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                                                 0, 0, 88, 24, hwnd_, MenuId(kIdCardKeyPressBase + static_cast<int>(i)), instance_, nullptr);
        cards_[i].start = CreateWindowExW(0, L"BUTTON", L"Start", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                          0, 0, 96, 30, hwnd_, MenuId(kIdCardStartBase + static_cast<int>(i)), instance_, nullptr);
        cards_[i].settings = CreateWindowExW(0, L"BUTTON", L"Settings", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                             0, 0, 96, 30, hwnd_, MenuId(kIdCardSettingsBase + static_cast<int>(i)), instance_, nullptr);
        RegisterButton(cards_[i].start, ButtonVisualStyle::DynamicStartStop);
        RegisterButton(cards_[i].settings, ButtonVisualStyle::Secondary);
        ApplyEditMargins(cards_[i].ed_delay);
        ApplyEditMargins(cards_[i].ed_key_press);
    }

    chk_down_only_ = CreateWindowExW(0, L"BUTTON", L"Down Click Only", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                     0, 0, 180, 24, hwnd_, MenuId(kIdGlobalDownOnly), instance_, nullptr);
    st_down_hint_ = CreateWindowExW(0, L"STATIC", L"Press only (no release) instead of normal clicks", WS_CHILD | WS_VISIBLE,
                                    0, 0, 420, 20, hwnd_, nullptr, instance_, nullptr);

    st_editor_title_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 220, 24, hwnd_, nullptr, instance_, nullptr);
    st_lbl_cps_ = CreateWindowExW(0, L"STATIC", L"CPS", WS_CHILD | WS_VISIBLE, 0, 0, 45, 20, hwnd_, nullptr, instance_, nullptr);
    st_lbl_delay_ = CreateWindowExW(0, L"STATIC", L"Delay", WS_CHILD | WS_VISIBLE, 0, 0, 50, 20, hwnd_, nullptr, instance_, nullptr);
    st_lbl_offset_ = CreateWindowExW(0, L"STATIC", L"Offset", WS_CHILD | WS_VISIBLE, 0, 0, 50, 20, hwnd_, nullptr, instance_, nullptr);
    st_lbl_key_press_ = CreateWindowExW(0, L"STATIC", L"Key Press (ms)", WS_CHILD | WS_VISIBLE, 0, 0, 96, 20, hwnd_, nullptr, instance_, nullptr);
    st_lbl_output_ = CreateWindowExW(0, L"STATIC", L"Output", WS_CHILD | WS_VISIBLE, 0, 0, 56, 20, hwnd_, nullptr, instance_, nullptr);
    st_lbl_hotkey_ = CreateWindowExW(0, L"STATIC", L"Hotkey", WS_CHILD | WS_VISIBLE, 0, 0, 56, 20, hwnd_, nullptr, instance_, nullptr);
    st_lbl_mode_ = CreateWindowExW(0, L"STATIC", L"Mode", WS_CHILD | WS_VISIBLE, 0, 0, 50, 20, hwnd_, nullptr, instance_, nullptr);

    ed_cps_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                              0, 0, 90, 26, hwnd_, MenuId(kIdEditCps), instance_, nullptr);
    ed_delay_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                                0, 0, 90, 26, hwnd_, MenuId(kIdEditDelay), instance_, nullptr);
    ed_offset_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                                 0, 0, 90, 26, hwnd_, MenuId(kIdEditOffset), instance_, nullptr);
    ed_key_press_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                                    0, 0, 90, 26, hwnd_, MenuId(kIdEditKeyPressMs), instance_, nullptr);
    ed_output_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                 0, 0, 110, 26, hwnd_, MenuId(kIdEditOutput), instance_, nullptr);
    ed_hotkey_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                 0, 0, 110, 26, hwnd_, MenuId(kIdEditHotkey), instance_, nullptr);
    ApplyEditMargins(ed_cps_);
    ApplyEditMargins(ed_delay_);
    ApplyEditMargins(ed_offset_);
    ApplyEditMargins(ed_key_press_);
    ApplyEditMargins(ed_output_);
    ApplyEditMargins(ed_hotkey_);

    cb_mode_ = CreateWindowExW(0, L"BUTTON", L"Hold", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                               0, 0, 110, 32, hwnd_, MenuId(kIdComboMode), instance_, nullptr);

    chk_enabled_ = CreateWindowExW(0, L"BUTTON", L"Enabled", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                   0, 0, 100, 24, hwnd_, MenuId(kIdCheckEnabled), instance_, nullptr);

    btn_record_ = CreateWindowExW(0, L"BUTTON", L"Record", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                  0, 0, 90, 30, hwnd_, MenuId(kIdRecord), instance_, nullptr);
    btn_apply_ = CreateWindowExW(0, L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                 0, 0, 90, 30, hwnd_, MenuId(kIdApply), instance_, nullptr);
    btn_start_all_ = CreateWindowExW(0, L"BUTTON", L"Start All", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                     0, 0, 100, 32, hwnd_, MenuId(kIdStartAll), instance_, nullptr);
    btn_stop_all_ = CreateWindowExW(0, L"BUTTON", L"Stop All", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                    0, 0, 100, 32, hwnd_, MenuId(kIdStopAll), instance_, nullptr);
    btn_save_ = CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                0, 0, 90, 32, hwnd_, MenuId(kIdSave), instance_, nullptr);
    btn_defaults_ = CreateWindowExW(0, L"BUTTON", L"Defaults", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                    0, 0, 90, 32, hwnd_, MenuId(kIdDefaults), instance_, nullptr);
    RegisterButton(btn_record_, ButtonVisualStyle::Secondary);
    RegisterButton(btn_apply_, ButtonVisualStyle::Primary);
    RegisterButton(btn_start_all_, ButtonVisualStyle::Primary);
    RegisterButton(btn_stop_all_, ButtonVisualStyle::Danger);
    RegisterButton(btn_save_, ButtonVisualStyle::Secondary);
    RegisterButton(btn_defaults_, ButtonVisualStyle::Danger);
    RegisterButton(cb_mode_, ButtonVisualStyle::InputDropdown);
    RegisterButton(chk_down_only_, ButtonVisualStyle::Checkbox);
    RegisterButton(chk_enabled_, ButtonVisualStyle::Checkbox);

    st_footer_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 300, 48, hwnd_, nullptr, instance_, nullptr);

    ApplyFonts();
}

void App::LayoutUi() {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = static_cast<int>(client.right - client.left);
    const int height = static_cast<int>(client.bottom - client.top);

    const int pad = Scale(28);
    const int section_gap = Scale(16);
    const int gap = Scale(12);
    const int panel_pad = Scale(14);
    const int label_h = Scale(18);
    const int input_h = Scale(32);
    const int button_h = Scale(34);
    const int input_frame = Scale(1);

    auto place_input = [input_frame](HWND control, int x, int y, int w, int h) {
        const int inner_w = std::max(1, w - input_frame * 2);
        const int inner_h = std::max(1, h - input_frame * 2);
        MoveWindow(control, x + input_frame, y + input_frame, inner_w, inner_h, TRUE);
    };

    int y = Scale(20);
    const int header_title_h = Scale(56);
    const int subtitle_h = Scale(24);
    MoveWindow(st_header_title_, pad, y, Scale(400), header_title_h, TRUE);
    y += header_title_h + Scale(8);
    MoveWindow(st_header_subtitle_, pad, y, width - (pad * 2), subtitle_h, TRUE);
    y += subtitle_h + Scale(12);
    header_divider_rect_ = {pad, y, width - pad, y + Scale(1)};
    y += section_gap;

    const int stat_h = Scale(102);
    const int stat_w = (width - (pad * 2) - (gap * 2)) / 3;
    for (int i = 0; i < 3; ++i) {
        const int x = pad + i * (stat_w + gap);
        stats_rects_[i] = {x, y, x + stat_w, y + stat_h};
    }
    y += stat_h + section_gap;

    MoveWindow(st_section_auto_, pad, y, Scale(240), Scale(28), TRUE);
    y += Scale(34);

    const int card_h = Scale(210);
    const int card_w = (width - (pad * 2) - (gap * 3)) / 4;
    for (size_t i = 0; i < kClickerCount; ++i) {
        const int x = pad + static_cast<int>(i) * (card_w + gap);
        card_rects_[i] = {x, y, x + card_w, y + card_h};

        const int content_x = x + panel_pad;
        const int content_w = card_w - (panel_pad * 2);
        const int field_w = (content_w - gap) / 2;
        const int key_field_x = content_x + field_w + gap;

        MoveWindow(cards_[i].title, content_x, y + panel_pad, content_w, Scale(26), TRUE);
        MoveWindow(cards_[i].detail, content_x, y + panel_pad + Scale(30), content_w, Scale(18), TRUE);
        MoveWindow(cards_[i].bind, content_x, y + panel_pad + Scale(50), content_w, Scale(18), TRUE);

        const int field_label_y = y + panel_pad + Scale(74);
        const int field_input_y = field_label_y + label_h + Scale(4);
        MoveWindow(cards_[i].lbl_delay, content_x, field_label_y, field_w, label_h, TRUE);
        place_input(cards_[i].ed_delay, content_x, field_input_y, field_w, input_h);
        MoveWindow(cards_[i].lbl_key_press, key_field_x, field_label_y, field_w, label_h, TRUE);
        place_input(cards_[i].ed_key_press, key_field_x, field_input_y, field_w, input_h);

        const int button_w = (content_w - gap) / 2;
        const int button_y = y + card_h - panel_pad - button_h;
        MoveWindow(cards_[i].start, content_x, button_y, button_w, button_h, TRUE);
        MoveWindow(cards_[i].settings, content_x + button_w + gap, button_y, button_w, button_h, TRUE);
    }
    y += card_h + section_gap;

    down_rect_ = {pad, y, width - pad, y + Scale(62)};
    MoveWindow(chk_down_only_, down_rect_.left + panel_pad, down_rect_.top + Scale(10), Scale(240), Scale(24), TRUE);
    MoveWindow(st_down_hint_, down_rect_.left + panel_pad, down_rect_.top + Scale(34), down_rect_.right - down_rect_.left - (panel_pad * 2), Scale(20), TRUE);
    y = down_rect_.bottom + section_gap;

    editor_rect_ = {pad, y, width - pad, y + Scale(196)};
    const int editor_inner_left = editor_rect_.left + panel_pad;
    const int editor_inner_top = editor_rect_.top + panel_pad;
    const int editor_inner_width = editor_rect_.right - editor_rect_.left - (panel_pad * 2);
    MoveWindow(st_editor_title_, editor_inner_left, editor_inner_top, Scale(320), Scale(26), TRUE);

    const int content_top = editor_inner_top + Scale(36);
    int actions_w = Scale(320);
    actions_w = std::min(actions_w, editor_inner_width - Scale(420));
    actions_w = std::max(actions_w, Scale(260));
    const int form_w = editor_inner_width - actions_w - gap;
    const int form_left = editor_inner_left;
    const int actions_left = form_left + form_w + gap;
    const int col_w = (form_w - (gap * 3)) / 4;

    auto col_x = [form_left, col_w, gap](int index) {
        return form_left + index * (col_w + gap);
    };

    const int row1_label_y = content_top;
    const int row1_input_y = row1_label_y + label_h + Scale(4);
    const int row2_label_y = row1_input_y + input_h + Scale(10);
    const int row2_input_y = row2_label_y + label_h + Scale(4);

    MoveWindow(st_lbl_cps_, col_x(0), row1_label_y, col_w, label_h, TRUE);
    MoveWindow(st_lbl_delay_, col_x(1), row1_label_y, col_w, label_h, TRUE);
    MoveWindow(st_lbl_offset_, col_x(2), row1_label_y, col_w, label_h, TRUE);
    MoveWindow(st_lbl_key_press_, col_x(3), row1_label_y, col_w, label_h, TRUE);

    place_input(ed_cps_, col_x(0), row1_input_y, col_w, input_h);
    place_input(ed_delay_, col_x(1), row1_input_y, col_w, input_h);
    place_input(ed_offset_, col_x(2), row1_input_y, col_w, input_h);
    place_input(ed_key_press_, col_x(3), row1_input_y, col_w, input_h);

    MoveWindow(st_lbl_output_, col_x(0), row2_label_y, col_w, label_h, TRUE);
    MoveWindow(st_lbl_hotkey_, col_x(1), row2_label_y, col_w, label_h, TRUE);
    MoveWindow(st_lbl_mode_, col_x(2), row2_label_y, col_w, label_h, TRUE);

    place_input(ed_output_, col_x(0), row2_input_y, col_w, input_h);
    place_input(ed_hotkey_, col_x(1), row2_input_y, col_w, input_h);
    MoveWindow(cb_mode_, col_x(2), row2_input_y, col_w, input_h, TRUE);
    MoveWindow(chk_enabled_, col_x(3), row2_input_y + Scale(5), col_w, Scale(24), TRUE);

    const int action_button_w = (actions_w - gap) / 2;
    const int action_row0 = content_top;
    const int action_row1 = action_row0 + button_h + gap;
    const int action_row2 = action_row1 + button_h + gap;
    MoveWindow(btn_record_, actions_left, action_row0, action_button_w, button_h, TRUE);
    MoveWindow(btn_apply_, actions_left + action_button_w + gap, action_row0, action_button_w, button_h, TRUE);
    MoveWindow(btn_start_all_, actions_left, action_row1, action_button_w, button_h, TRUE);
    MoveWindow(btn_stop_all_, actions_left + action_button_w + gap, action_row1, action_button_w, button_h, TRUE);
    MoveWindow(btn_save_, actions_left, action_row2, action_button_w, button_h, TRUE);
    MoveWindow(btn_defaults_, actions_left + action_button_w + gap, action_row2, action_button_w, button_h, TRUE);

    editor_timing_group_ = {form_left - Scale(8), row1_label_y - Scale(8), form_left + form_w + Scale(8), row1_input_y + input_h + Scale(10)};
    editor_bind_group_ = {form_left - Scale(8), row2_label_y - Scale(8), form_left + form_w + Scale(8), row2_input_y + input_h + Scale(14)};
    editor_actions_group_ = {actions_left - Scale(8), action_row0 - Scale(8), actions_left + actions_w + Scale(8), action_row2 + button_h + Scale(10)};

    int footer_top = editor_rect_.bottom + section_gap;
    const int footer_h = Scale(70);
    if (footer_top + footer_h > height - pad) {
        footer_top = std::max(static_cast<int>(editor_rect_.bottom) + Scale(8), height - pad - footer_h);
    }
    footer_rect_ = {pad, footer_top, width - pad, footer_top + footer_h};
    MoveWindow(st_footer_, footer_rect_.left + panel_pad, footer_rect_.top + Scale(10),
               footer_rect_.right - footer_rect_.left - (panel_pad * 2), Scale(48), TRUE);
}

void App::PaintUi(HDC dc) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    FillRect(dc, &client, brush_bg_);

    // Rounded section shells with subtle shadow for modern grouping.
    for (const RECT& r : stats_rects_) {
        DrawSurfaceCard(dc, r, kColorPanel, kColorPanelBorder, Scale(10));
    }
    for (size_t i = 0; i < kClickerCount; ++i) {
        const bool selected = i == selected_clicker_;
        const COLORREF border = selected ? BlendColor(kColorAccentCyan, kColorCardBorder, 132) : kColorCardBorder;
        DrawSurfaceCard(dc, card_rects_[i], kColorCard, border, Scale(10));
        if (selected) {
            RECT ring = card_rects_[i];
            InflateRect(&ring, -Scale(2), -Scale(2));
            DrawRoundedRect(dc, ring, Scale(9), kColorCard, BlendColor(kColorAccentCyan, kColorText, 180));
        }
    }
    DrawSurfaceCard(dc, down_rect_, kColorPanel, kColorPanelBorder, Scale(10));
    DrawSurfaceCard(dc, editor_rect_, kColorPanel, kColorPanelBorder, Scale(10));
    DrawSurfaceCard(dc, footer_rect_, kColorPanel, kColorPanelBorder, Scale(10));

    // Internal editor grouping so timing/bindings/actions are visually distinct.
    DrawRoundedRect(dc, editor_timing_group_, Scale(6), BlendColor(kColorPanel, kColorBg, 220), kColorBorder);
    DrawRoundedRect(dc, editor_bind_group_, Scale(6), BlendColor(kColorPanel, kColorBg, 220), kColorBorder);
    DrawRoundedRect(dc, editor_actions_group_, Scale(6), BlendColor(kColorPanel, kColorBg, 220), kColorBorder);

    // Header divider and section hint lines.
    SetDCBrushColor(dc, kColorBorder);
    FillRect(dc, &header_divider_rect_, reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

    // Draw custom input frames to replace dated 3D client-edge look.
    for (size_t i = 0; i < kClickerCount; ++i) {
        DrawInputFrame(dc, cards_[i].ed_delay);
        DrawInputFrame(dc, cards_[i].ed_key_press);
    }
    DrawInputFrame(dc, ed_cps_);
    DrawInputFrame(dc, ed_delay_);
    DrawInputFrame(dc, ed_offset_);
    DrawInputFrame(dc, ed_key_press_);
    DrawInputFrame(dc, ed_output_);
    DrawInputFrame(dc, ed_hotkey_);

    // Transparent text rendering with explicit vertical alignment prevents overlap artifacts.
    SetBkMode(dc, TRANSPARENT);
    DrawStatText(dc, 0, kColorAccentCyan);
    DrawStatText(dc, 1, kColorAccentGreen);
    DrawStatText(dc, 2, (stat_values_[2] == L"Running") ? kColorAccentGreen : kColorTextMuted);

    HFONT old = reinterpret_cast<HFONT>(SelectObject(dc, font_small_));
    SetTextColor(dc, kColorAccentCyan);
    RECT timing_title = {editor_timing_group_.left + Scale(10), editor_timing_group_.top + Scale(4), editor_timing_group_.right - Scale(8), editor_timing_group_.top + Scale(22)};
    DrawTextW(dc, L"Timing", -1, &timing_title, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    RECT bind_title = {editor_bind_group_.left + Scale(10), editor_bind_group_.top + Scale(4), editor_bind_group_.right - Scale(8), editor_bind_group_.top + Scale(22)};
    DrawTextW(dc, L"Bindings", -1, &bind_title, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    RECT actions_title = {editor_actions_group_.left + Scale(10), editor_actions_group_.top + Scale(4), editor_actions_group_.right - Scale(8), editor_actions_group_.top + Scale(22)};
    DrawTextW(dc, L"Actions", -1, &actions_title, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    SetTextColor(dc, BlendColor(kColorText, kColorTextMuted, 182));
    SelectObject(dc, font_hint_);
    RECT macro_hint = {down_rect_.right - Scale(430), down_rect_.top + Scale(9), down_rect_.right - Scale(14), down_rect_.top + Scale(30)};
    DrawTextW(dc, L"Toggle off for normal press/release clicks", -1, &macro_hint, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    RECT footer_text = {footer_rect_.left + Scale(14), footer_rect_.top + Scale(10), footer_rect_.right - Scale(14), footer_rect_.bottom - Scale(10)};
    DrawTextW(dc, footer_text_.c_str(), -1, &footer_text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, old);

}

void App::LoadOrDefaultSettings() {
    AppSettings loaded{};
    if (LoadSettingsFile(settings_path_, loaded)) {
        settings_ = loaded;
    } else {
        settings_ = DefaultSettings();
    }

    SendMessageW(chk_down_only_, BM_SETCHECK, settings_.global_down_only ? BST_CHECKED : BST_UNCHECKED, 0);
}

void App::ApplySettingsToEngine() {
    for (size_t i = 0; i < kClickerCount; ++i) {
        ClickerConfig cfg = settings_.clickers[i];
        if (settings_.global_down_only) {
            cfg.down_only = true;
        }
        settings_.clickers[i] = cfg;
        clickers_[i].SetConfig(cfg);
        keybinds_.SetKeyBinding(static_cast<int>(i), cfg.hotkey_vk);
    }
}

bool App::SaveSettingsNow() {
    return SaveSettingsFile(settings_path_, settings_);
}

void App::ResetDefaults() {
    settings_ = DefaultSettings();
    selected_clicker_ = 0;
    manual_on_.fill(false);
    toggle_latched_.fill(false);
    previous_down_.fill(false);
    recording_hotkey_ = false;
    ApplySettingsToEngine();
    SendMessageW(chk_down_only_, BM_SETCHECK, BST_UNCHECKED, 0);
    monitor_.ResetSession();
    SaveSettingsNow();
}

void App::ApplyGlobalDownOnly(bool enabled) {
    settings_.global_down_only = enabled;
    for (size_t i = 0; i < kClickerCount; ++i) {
        settings_.clickers[i].down_only = enabled;
        clickers_[i].SetConfig(settings_.clickers[i]);
    }
}

void App::RefreshCard(size_t index) {
    const ClickerConfig& cfg = settings_.clickers[index];

    wchar_t title[64] = {};
    std::swprintf(title, std::size(title), L"Clicker %u", static_cast<unsigned>(index + 1));
    SetWindowTextW(cards_[index].title, title);

    wchar_t detail[128] = {};
    std::swprintf(detail, std::size(detail), L"%u CPS   %ums offset", static_cast<unsigned>(cfg.cps), static_cast<unsigned>(cfg.offset_ms));
    SetWindowTextW(cards_[index].detail, detail);

    const HWND focused = GetFocus();
    if (focused != cards_[index].ed_delay) {
        SetDlgItemInt(hwnd_, kIdCardDelayBase + static_cast<int>(index), cfg.delay_ms, FALSE);
    }
    if (focused != cards_[index].ed_key_press) {
        SetDlgItemInt(hwnd_, kIdCardKeyPressBase + static_cast<int>(index), cfg.key_press_ms, FALSE);
    }

    const std::wstring mode = cfg.mode == ActivationMode::Toggle ? L"Toggle" : L"Hold";
    const std::wstring bind = KeyBindManager::VkToText(cfg.hotkey_vk) + L" | " + mode + L" | " + KeyBindManager::VkToText(cfg.output_vk);
    SetWindowTextW(cards_[index].bind, bind.c_str());
    SetWindowTextW(cards_[index].start, clickers_[index].IsActive() ? L"Stop" : L"Start");
}

void App::RefreshEditor() {
    ClickerConfig cfg = settings_.clickers[selected_clicker_];
    if (settings_.global_down_only) {
        cfg.down_only = true;
    }

    wchar_t cap[64] = {};
    std::swprintf(cap, std::size(cap), L"Clicker %u Settings", static_cast<unsigned>(selected_clicker_ + 1));
    SetWindowTextW(st_editor_title_, cap);

    SetDlgItemInt(hwnd_, kIdEditCps, cfg.cps, FALSE);
    SetDlgItemInt(hwnd_, kIdEditDelay, cfg.delay_ms, FALSE);
    SetDlgItemInt(hwnd_, kIdEditOffset, cfg.offset_ms, FALSE);
    SetDlgItemInt(hwnd_, kIdEditKeyPressMs, cfg.key_press_ms, FALSE);
    SetWindowTextW(ed_output_, KeyBindManager::VkToText(cfg.output_vk).c_str());
    SetWindowTextW(ed_hotkey_, KeyBindManager::VkToText(cfg.hotkey_vk).c_str());
    mode_selection_ = (cfg.mode == ActivationMode::Toggle) ? 1 : 0;
    SetWindowTextW(cb_mode_, mode_selection_ == 1 ? L"Toggle" : L"Hold");
    SendMessageW(chk_enabled_, BM_SETCHECK, cfg.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(btn_record_, recording_hotkey_ ? L"Press key..." : L"Record");
}

void App::ApplyCardEditor(size_t index) {
    ClickerConfig cfg = settings_.clickers[index];
    cfg.delay_ms = static_cast<uint32_t>(ReadEditInt(hwnd_, kIdCardDelayBase + static_cast<int>(index), static_cast<int>(cfg.delay_ms), 0, 1000));
    cfg.key_press_ms = static_cast<uint32_t>(ReadEditInt(hwnd_, kIdCardKeyPressBase + static_cast<int>(index), static_cast<int>(cfg.key_press_ms), 1, 10));
    cfg.down_only = settings_.global_down_only;

    settings_.clickers[index] = cfg;
    clickers_[index].SetConfig(cfg);
}

void App::ApplyEditor() {
    ClickerConfig cfg = settings_.clickers[selected_clicker_];
    cfg.cps = static_cast<uint32_t>(ReadEditInt(hwnd_, kIdEditCps, static_cast<int>(cfg.cps), 1, 4000));
    cfg.delay_ms = static_cast<uint32_t>(ReadEditInt(hwnd_, kIdEditDelay, static_cast<int>(cfg.delay_ms), 0, 1000));
    cfg.offset_ms = static_cast<uint32_t>(ReadEditInt(hwnd_, kIdEditOffset, static_cast<int>(cfg.offset_ms), 0, 5000));
    cfg.key_press_ms = static_cast<uint32_t>(ReadEditInt(hwnd_, kIdEditKeyPressMs, static_cast<int>(cfg.key_press_ms), 1, 10));

    wchar_t text[64] = {};
    uint16_t vk = 0;
    GetWindowTextW(ed_output_, text, static_cast<int>(std::size(text)));
    if (KeyBindManager::TextToVk(text, vk)) {
        cfg.output_vk = vk;
    }
    GetWindowTextW(ed_hotkey_, text, static_cast<int>(std::size(text)));
    if (KeyBindManager::TextToVk(text, vk)) {
        cfg.hotkey_vk = vk;
    }

    cfg.mode = (mode_selection_ == 1) ? ActivationMode::Toggle : ActivationMode::Hold;
    // Enabled is custom owner-drawn; keep state from settings model instead of BM_GETCHECK.
    cfg.enabled = settings_.clickers[selected_clicker_].enabled;
    cfg.down_only = settings_.global_down_only;

    settings_.clickers[selected_clicker_] = cfg;
    clickers_[selected_clicker_].SetConfig(cfg);
    keybinds_.SetKeyBinding(static_cast<int>(selected_clicker_), cfg.hotkey_vk);
}

void App::PollHotkeys() {
    for (size_t i = 0; i < kClickerCount; ++i) {
        const ClickerConfig& cfg = settings_.clickers[i];

        if (!cfg.enabled || cfg.hotkey_vk == 0) {
            const bool active = manual_on_[i] && cfg.enabled;
            clickers_[i].SetActive(active);
            previous_down_[i] = false;
            continue;
        }

        const bool down = keybinds_.IsDown(cfg.hotkey_vk);
        if (cfg.mode == ActivationMode::Toggle && down && !previous_down_[i]) {
            toggle_latched_[i] = !toggle_latched_[i];
        }

        bool active = manual_on_[i] || toggle_latched_[i] || (cfg.mode == ActivationMode::Hold && down);
        if (!cfg.enabled) {
            active = false;
        }
        clickers_[i].SetActive(active);
        previous_down_[i] = down;
    }
}

void App::RefreshUi() {
    std::array<uint64_t, kClickerCount> click_counts{};
    uint32_t active_count = 0;
    for (size_t i = 0; i < kClickerCount; ++i) {
        click_counts[i] = clickers_[i].GetTotalClicks();
        if (clickers_[i].IsActive()) {
            ++active_count;
        }
        RefreshCard(i);
    }

    monitor_.Update(click_counts, active_count);
    const uint64_t total = monitor_.GetTotalClicks();
    const double live = monitor_.GetLiveCps();
    const double average = monitor_.GetAverageCps();
    const double session = monitor_.GetSessionSeconds();

    wchar_t value[64] = {};
    std::swprintf(value, std::size(value), L"%u", static_cast<unsigned>(active_count));
    stat_values_[0] = value;
    stat_hints_[0] = active_count > 0 ? L"Running" : L"Idle";

    std::swprintf(value, std::size(value), L"%u", static_cast<unsigned>(kClickerCount));
    stat_values_[1] = value;
    stat_hints_[1] = L"Available";

    stat_values_[2] = active_count > 0 ? L"Running" : L"Idle";
    stat_hints_[2] = recording_hotkey_ ? L"Recording hotkey..." : L"Ready";

    SetWindowTextW(st_active_, stat_values_[0].c_str());
    SetWindowTextW(st_total_, stat_values_[1].c_str());
    SetWindowTextW(st_status_, stat_values_[2].c_str());

    const std::wstring settings_display = CompactPathForFooter(settings_path_, 54);
    wchar_t footer[512] = {};
    std::swprintf(footer, std::size(footer), L"Total Clicks: %llu   |   Live CPS: %.0f   |   Avg CPS: %.0f   |   Session: %.1fs   |   Settings: %s",
                  static_cast<unsigned long long>(total), live, average, session, settings_display.c_str());
    footer_text_ = footer;
    SetWindowTextW(st_footer_, footer_text_.c_str());

    UpdateHoveredButton();
    for (const RECT& stat : stats_rects_) {
        InvalidateRect(hwnd_, &stat, FALSE);
    }
    InvalidateRect(hwnd_, &footer_rect_, FALSE);
}

bool App::HandleRecordKey(uint16_t vk) {
    if (!recording_hotkey_) {
        return false;
    }
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) {
        return true;
    }
    settings_.clickers[selected_clicker_].hotkey_vk = vk;
    keybinds_.SetKeyBinding(static_cast<int>(selected_clicker_), vk);
    clickers_[selected_clicker_].SetConfig(settings_.clickers[selected_clicker_]);
    recording_hotkey_ = false;
    SaveSettingsNow();
    RefreshEditor();
    RefreshUi();
    return true;
}

LRESULT CALLBACK App::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* app = nullptr;
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        app = reinterpret_cast<App*>(create->lpCreateParams);
        app->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!app) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return app->OnMessage(msg, wp, lp);
}

LRESULT App::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        LayoutUi();
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;

    case WM_DPICHANGED: {
        dpi_ = HIWORD(wp);
        const RECT* suggested = reinterpret_cast<RECT*>(lp);
        if (suggested) {
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        CreateFonts();
        ApplyFonts();
        for (size_t i = 0; i < kClickerCount; ++i) {
            ApplyEditMargins(cards_[i].ed_delay);
            ApplyEditMargins(cards_[i].ed_key_press);
        }
        ApplyEditMargins(ed_cps_);
        ApplyEditMargins(ed_delay_);
        ApplyEditMargins(ed_offset_);
        ApplyEditMargins(ed_key_press_);
        ApplyEditMargins(ed_output_);
        ApplyEditMargins(ed_hotkey_);
        LayoutUi();
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd_, &ps);
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;

        HDC mem_dc = CreateCompatibleDC(dc);
        HBITMAP bmp = (mem_dc && width > 0 && height > 0) ? CreateCompatibleBitmap(dc, width, height) : nullptr;
        if (mem_dc && bmp) {
            HGDIOBJ old_bmp = SelectObject(mem_dc, bmp);
            PaintUi(mem_dc);
            BitBlt(dc, 0, 0, width, height, mem_dc, 0, 0, SRCCOPY);
            SelectObject(mem_dc, old_bmp);
            DeleteObject(bmp);
            DeleteDC(mem_dc);
        } else {
            if (bmp) {
                DeleteObject(bmp);
            }
            if (mem_dc) {
                DeleteDC(mem_dc);
            }
            PaintUi(dc);
        }
        EndPaint(hwnd_, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (dis) {
            if (dis->CtlType == ODT_MENU && (dis->itemID == kIdModePopupHold || dis->itemID == kIdModePopupToggle)) {
                const bool selected = (dis->itemState & ODS_SELECTED) != 0;
                const bool checked = (dis->itemID == kIdModePopupHold) ? (mode_selection_ == 0) : (mode_selection_ == 1);
                const COLORREF fill = selected ? BlendColor(kColorInput, kColorAccentCyan, 90) : kColorInput;

                HBRUSH fill_brush = CreateSolidBrush(fill);
                FillRect(dis->hDC, &dis->rcItem, fill_brush);
                DeleteObject(fill_brush);

                if (checked) {
                    RECT stripe = dis->rcItem;
                    stripe.right = stripe.left + Scale(4);
                    HBRUSH stripe_brush = CreateSolidBrush(kColorAccentCyan);
                    FillRect(dis->hDC, &stripe, stripe_brush);
                    DeleteObject(stripe_brush);
                }

                const wchar_t* label = (dis->itemID == kIdModePopupHold) ? L"Hold" : L"Toggle";
                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, checked ? kColorAccentCyan : kColorText);
                HFONT old_font = reinterpret_cast<HFONT>(SelectObject(dis->hDC, font_body_));
                RECT text_rect = dis->rcItem;
                text_rect.left += Scale(12);
                text_rect.right -= Scale(8);
                DrawTextW(dis->hDC, label, -1, &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(dis->hDC, old_font);
                return TRUE;
            }
            if (dis->CtlType == ODT_BUTTON && IsManagedButton(dis->hwndItem)) {
                DrawCustomButton(*dis);
                return TRUE;
            }
        }
        break;
    }

    case WM_MEASUREITEM: {
        auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if (mi && mi->CtlType == ODT_MENU && (mi->itemID == kIdModePopupHold || mi->itemID == kIdModePopupToggle)) {
            mi->itemWidth = static_cast<UINT>(Scale(120));
            mi->itemHeight = static_cast<UINT>(Scale(26));
            return TRUE;
        }
        break;
    }

    case WM_MOUSEMOVE:
        if (!tracking_mouse_leave_) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd_;
            if (TrackMouseEvent(&tme)) {
                tracking_mouse_leave_ = true;
            }
        }
        UpdateHoveredButton();
        return 0;

    case WM_MOUSELEAVE:
        tracking_mouse_leave_ = false;
        if (hovered_button_) {
            HWND old = hovered_button_;
            hovered_button_ = nullptr;
            InvalidateRect(old, nullptr, FALSE);
        }
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wp);
        const HWND control = reinterpret_cast<HWND>(lp);
        COLORREF back_color = kColorBg;
        HBRUSH surface = ResolveSurfaceBrush(control, back_color);

        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, back_color);
        if (control == st_header_title_) {
            SetTextColor(dc, kColorAccentCyan);
        } else if (control == st_section_auto_ || control == st_editor_title_) {
            SetTextColor(dc, kColorAccentGreen);
        } else {
            bool muted = control == st_header_subtitle_ || control == st_down_hint_ || control == st_lbl_cps_ || control == st_lbl_delay_ ||
                         control == st_lbl_offset_ || control == st_lbl_key_press_ || control == st_lbl_output_ || control == st_lbl_hotkey_ ||
                         control == st_lbl_mode_;
            for (size_t i = 0; i < kClickerCount && !muted; ++i) {
                muted = control == cards_[i].detail || control == cards_[i].bind || control == cards_[i].lbl_delay || control == cards_[i].lbl_key_press;
            }
            if (muted) {
                SetTextColor(dc, kColorTextMuted);
            } else {
                SetTextColor(dc, kColorText);
            }
        }
        return reinterpret_cast<INT_PTR>(surface);
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, kColorText);
        SetBkColor(dc, kColorInput);
        SetBkMode(dc, OPAQUE);
        return reinterpret_cast<INT_PTR>(brush_input_);
    }

    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, kColorText);
        SetBkColor(dc, kColorInput);
        SetBkMode(dc, OPAQUE);
        return reinterpret_cast<INT_PTR>(brush_input_);
    }

    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wp);
        const HWND control = reinterpret_cast<HWND>(lp);
        if (IsManagedButton(control)) {
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<INT_PTR>(GetStockObject(NULL_BRUSH));
        }

        COLORREF back_color = kColorBg;
        HBRUSH surface = ResolveSurfaceBrush(control, back_color);
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, back_color);
        SetTextColor(dc, kColorText);
        return reinterpret_cast<INT_PTR>(surface);
    }

    case WM_TIMER:
        if (wp == kTimerPoll) {
            PollHotkeys();
            return 0;
        }
        if (wp == kTimerUi) {
            RefreshUi();
            return 0;
        }
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (recording_hotkey_) {
            HandleRecordKey(static_cast<uint16_t>(wp));
            return 0;
        }
        break;

    case WM_XBUTTONDOWN:
        if (recording_hotkey_) {
            const WORD xb = HIWORD(wp);
            HandleRecordKey(xb == XBUTTON2 ? VK_XBUTTON2 : VK_XBUTTON1);
            return 0;
        }
        break;

    case WM_COMMAND: {
        const int id = LOWORD(wp);
        const int code = HIWORD(wp);

        if (id >= kIdCardStartBase && id < kIdCardStartBase + kClickerCount && code == BN_CLICKED) {
            const size_t idx = static_cast<size_t>(id - kIdCardStartBase);
            manual_on_[idx] = !manual_on_[idx];
            if (!manual_on_[idx]) {
                toggle_latched_[idx] = false;
            }
            clickers_[idx].SetActive(manual_on_[idx] || toggle_latched_[idx]);
            RefreshUi();
            return 0;
        }

        if (id >= kIdCardSettingsBase && id < kIdCardSettingsBase + kClickerCount && code == BN_CLICKED) {
            const size_t previous = selected_clicker_;
            selected_clicker_ = static_cast<size_t>(id - kIdCardSettingsBase);
            recording_hotkey_ = false;
            RefreshEditor();
            if (previous != selected_clicker_) {
                InvalidateRect(hwnd_, &card_rects_[previous], FALSE);
                InvalidateRect(hwnd_, &card_rects_[selected_clicker_], FALSE);
            }
            return 0;
        }

        if ((id >= kIdCardDelayBase && id < kIdCardDelayBase + kClickerCount && code == EN_KILLFOCUS) ||
            (id >= kIdCardKeyPressBase && id < kIdCardKeyPressBase + kClickerCount && code == EN_KILLFOCUS)) {
            const size_t idx = (id >= kIdCardKeyPressBase)
                                   ? static_cast<size_t>(id - kIdCardKeyPressBase)
                                   : static_cast<size_t>(id - kIdCardDelayBase);
            ApplyCardEditor(idx);
            if (selected_clicker_ == idx) {
                RefreshEditor();
            }
            SaveSettingsNow();
            RefreshUi();
            return 0;
        }

        if (id == kIdGlobalDownOnly && code == BN_CLICKED) {
            const bool enabled = !settings_.global_down_only;
            ApplyGlobalDownOnly(enabled);
            SaveSettingsNow();
            RefreshEditor();
            RefreshUi();
            return 0;
        }

        if (id == kIdComboMode && code == BN_CLICKED) {
            RECT rc{};
            GetWindowRect(cb_mode_, &rc);
            HMENU menu = CreatePopupMenu();
            if (menu) {
                AppendMenuW(menu, MF_OWNERDRAW, kIdModePopupHold, nullptr);
                AppendMenuW(menu, MF_OWNERDRAW, kIdModePopupToggle, nullptr);
                SetForegroundWindow(hwnd_);
                const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                                                rc.left, rc.bottom, 0, hwnd_, nullptr);
                DestroyMenu(menu);
                if (cmd == kIdModePopupHold || cmd == kIdModePopupToggle) {
                    mode_selection_ = (cmd == kIdModePopupToggle) ? 1 : 0;
                    SetWindowTextW(cb_mode_, mode_selection_ == 1 ? L"Toggle" : L"Hold");
                    ApplyEditor();
                    RefreshUi();
                }
            }
            return 0;
        }

        if (id == kIdCheckEnabled && code == BN_CLICKED) {
            settings_.clickers[selected_clicker_].enabled = !settings_.clickers[selected_clicker_].enabled;
            clickers_[selected_clicker_].SetConfig(settings_.clickers[selected_clicker_]);
            RefreshEditor();
            RefreshUi();
            return 0;
        }

        if (id == kIdRecord && code == BN_CLICKED) {
            recording_hotkey_ = true;
            SetFocus(hwnd_);
            RefreshEditor();
            return 0;
        }

        if (id == kIdApply && code == BN_CLICKED) {
            ApplyEditor();
            SaveSettingsNow();
            RefreshUi();
            return 0;
        }

        if (id == kIdStartAll && code == BN_CLICKED) {
            for (size_t i = 0; i < kClickerCount; ++i) {
                manual_on_[i] = true;
            }
            RefreshUi();
            return 0;
        }

        if (id == kIdStopAll && code == BN_CLICKED) {
            for (size_t i = 0; i < kClickerCount; ++i) {
                manual_on_[i] = false;
                toggle_latched_[i] = false;
                clickers_[i].SetActive(false);
            }
            RefreshUi();
            return 0;
        }

        if (id == kIdSave && code == BN_CLICKED) {
            ApplyEditor();
            SaveSettingsNow();
            RefreshUi();
            return 0;
        }

        if (id == kIdDefaults && code == BN_CLICKED) {
            ResetDefaults();
            RefreshEditor();
            RefreshUi();
            return 0;
        }

        if ((id == kIdEditCps || id == kIdEditDelay || id == kIdEditOffset || id == kIdEditKeyPressMs || id == kIdEditOutput || id == kIdEditHotkey) &&
            (code == EN_KILLFOCUS || code == CBN_SELCHANGE || code == BN_CLICKED)) {
            ApplyEditor();
            RefreshUi();
            return 0;
        }
        break;
    }

    case WM_GETMINMAXINFO: {
        auto* m = reinterpret_cast<MINMAXINFO*>(lp);
        m->ptMinTrackSize.x = Scale(1260);
        m->ptMinTrackSize.y = Scale(900);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd_, kTimerPoll);
        KillTimer(hwnd_, kTimerUi);
        for (auto& clicker : clickers_) {
            clicker.SetActive(false);
        }
        SaveSettingsNow();
        timeEndPeriod(1);

        DestroyFonts();
        if (brush_bg_) DeleteObject(brush_bg_);
        if (brush_panel_) DeleteObject(brush_panel_);
        if (brush_card_) DeleteObject(brush_card_);
        if (brush_input_) DeleteObject(brush_input_);
        if (owns_icon_large_ && icon_large_) {
            DestroyIcon(icon_large_);
            icon_large_ = nullptr;
            owns_icon_large_ = false;
        }
        if (owns_icon_small_ && icon_small_) {
            DestroyIcon(icon_small_);
            icon_small_ = nullptr;
            owns_icon_small_ = false;
        }

        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd_, msg, wp, lp);
}

} // namespace
} // namespace blossom

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    blossom::App app;
    if (!app.Init(instance)) {
        MessageBoxW(nullptr, L"Failed to start Mk7Macro.", L"Mk7Macro", MB_OK | MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
