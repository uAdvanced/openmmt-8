#include "panel_ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine.h"
#include "lcd_font.h"

namespace mmt8 {
namespace {

ImU32 rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

const uint8_t *glyph(uint8_t code, const mmt_emu::PanelView &v) {
    static const uint8_t blank[8] = {};
    static const uint8_t full[8] = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
    if (code < 0x10) return &v.cgram[(code & 7u) * 8u];
    if (code >= 0x20 && code < 0x80) return kLcdFontA00[code - 0x20];
    if (code == 0xFF) return full;
    return blank;
}

void draw_icon(ImDrawList *dl, uint8_t id, ImVec2 c, float r, ImU32 col) {
    auto tri_right = [&](float x) {
        dl->AddTriangleFilled(ImVec2(x - r * 0.8f, c.y - r), ImVec2(x + r * 0.8f, c.y), ImVec2(x - r * 0.8f, c.y + r), col);
    };
    auto tri_left = [&](float x) {
        dl->AddTriangleFilled(ImVec2(x + r * 0.8f, c.y - r), ImVec2(x - r * 0.8f, c.y), ImVec2(x + r * 0.8f, c.y + r), col);
    };
    switch (id) {
        case 0: tri_left(c.x - r * 0.75f); tri_left(c.x + r * 0.75f); break;
        case 1: tri_right(c.x - r * 0.75f); tri_right(c.x + r * 0.75f); break;
        case 4: tri_right(c.x + r * 0.15f); break;
        case 5: dl->AddRectFilled(ImVec2(c.x - r * 0.8f, c.y - r * 0.8f), ImVec2(c.x + r * 0.8f, c.y + r * 0.8f), col); break;
        case 7: dl->AddCircleFilled(c, r * 0.9f, col, 24); break;
        case 22:
            dl->AddTriangleFilled(ImVec2(c.x - r, c.y + r * 0.6f), ImVec2(c.x + r, c.y + r * 0.6f), ImVec2(c.x, c.y - r * 0.6f), col);
            break;
        case 23:
            dl->AddTriangleFilled(ImVec2(c.x - r, c.y - r * 0.6f), ImVec2(c.x + r, c.y - r * 0.6f), ImVec2(c.x, c.y + r * 0.6f), col);
            break;
        default: break;
    }
}

}  // namespace

PanelUi::PanelUi(Engine &engine, GuiHost &host) : engine_(engine), host_(host) {}

void PanelUi::apply_style() const {
    ImGuiStyle &s = ImGui::GetStyle();
    s.WindowPadding = ImVec2(0, 0);
    s.WindowBorderSize = 0;
    s.FrameRounding = 4;
    s.PopupRounding = 6;
    s.PopupBorderSize = 1;
    s.GrabRounding = 4;
    s.ItemSpacing = ImVec2(8, 6);
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.135f, 0.145f, 0.98f);
    s.Colors[ImGuiCol_Border] = ImVec4(1, 1, 1, 0.12f);
    s.Colors[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.9f, 1);
    s.Colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.55f, 0.53f, 1);
    s.Colors[ImGuiCol_FrameBg] = ImVec4(0.09f, 0.09f, 0.1f, 1);
    s.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.12f, 0.13f, 1);
    s.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.14f, 0.14f, 0.15f, 1);
    s.Colors[ImGuiCol_SliderGrab] = ImVec4(0.85f, 0.84f, 0.8f, 1);
    s.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1, 1, 1, 1);
    s.Colors[ImGuiCol_Button] = ImVec4(0.09f, 0.09f, 0.1f, 1);
    s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.16f, 0.16f, 0.17f, 1);
    s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.2f, 0.2f, 0.21f, 1);
    s.Colors[ImGuiCol_Header] = ImVec4(0.25f, 0.25f, 0.27f, 1);
    s.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.3f, 0.3f, 0.32f, 1);
    s.Colors[ImGuiCol_HeaderActive] = ImVec4(0.35f, 0.35f, 0.37f, 1);
    s.Colors[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.12f);
}

void PanelUi::set_font(ImFont *font) { font_ = font; }

void PanelUi::set_status(const std::string &error) {
    status_ = error;
    if (ImGui::GetCurrentContext()) status_time_ = ImGui::GetTime();
}

ImVec2 PanelUi::P(float x, float y) const { return ImVec2(org_.x + x * s_, org_.y + y * s_); }

void PanelUi::text_centered(ImDrawList *dl, float size, ImVec2 center, ImU32 col, const char *t) {
    const ImVec2 sz = font_->CalcTextSizeA(size, FLT_MAX, 0, t);
    dl->AddText(font_, size, ImVec2(std::round(center.x - sz.x * 0.5f), std::round(center.y - sz.y * 0.5f)), col, t);
}

void PanelUi::draw_led(ImDrawList *dl, ImVec2 c, bool on) {
    const float r = 4.2f * s_;
    if (on) {
        dl->AddCircleFilled(c, r * 3.2f, rgb(255, 40, 20, 28), 24);
        dl->AddCircleFilled(c, r * 2.0f, rgb(255, 50, 25, 60), 24);
        dl->AddCircleFilled(c, r, rgb(255, 64, 40), 20);
        dl->AddCircleFilled(ImVec2(c.x - r * 0.3f, c.y - r * 0.35f), r * 0.4f, rgb(255, 200, 180, 200), 12);
    } else {
        dl->AddCircleFilled(c, r, rgb(70, 18, 16), 20);
        dl->AddCircle(c, r, rgb(0, 0, 0, 120), 20, 1.0f * s_);
    }
}

void PanelUi::draw_chassis(ImDrawList *dl, ImVec2 size) {
    dl->AddRectFilled(ImVec2(0, 0), size, rgb(14, 14, 15));
    const ImVec2 a = P(0, 0), b = P(kPanelW, kPanelH);
    dl->AddRectFilledMultiColor(a, b, rgb(52, 53, 57), rgb(52, 53, 57), rgb(30, 31, 34), rgb(30, 31, 34));
    dl->AddRect(a, b, rgb(255, 255, 255, 18), 0, 0, 1.0f);
    dl->AddLine(P(0, 48), P(kPanelW, 48), rgb(0, 0, 0, 90), 1.5f * s_);
    dl->AddLine(P(0, 49.5f), P(kPanelW, 49.5f), rgb(255, 255, 255, 14), 1.0f * s_);
    dl->AddText(font_, 22 * s_, P(32, 12), rgb(236, 236, 232), "OpenMMT-8");
    const float w = font_->CalcTextSizeA(22 * s_, FLT_MAX, 0, "OpenMMT-8").x;
    dl->AddText(font_, 11 * s_, ImVec2(P(32, 12).x + w + 12 * s_, P(0, 19).y), rgb(170, 170, 166),
                "MIDI SEQUENCER");
    dl->AddText(font_, 10 * s_, P(32, 36), rgb(150, 150, 146), "Not affiliated with Alesis.");
    dl->AddText(font_, 10 * s_, P(layout::KX, 244), rgb(150, 150, 146), "KEYPAD");
}

void PanelUi::draw_lcd(ImDrawList *dl, const mmt_emu::PanelView &v, double t) {
    const ImVec2 b0 = P(32, 62), b1 = P(384, 178);
    dl->AddRectFilled(b0, b1, rgb(8, 8, 9), 6 * s_);
    dl->AddRect(b0, b1, rgb(255, 255, 255, 22), 6 * s_, 0, 1.0f);
    const ImVec2 g0 = P(42, 72), g1 = P(374, 168);
    const bool lit = v.rom_loaded;
    const ImU32 bg_top = lit ? rgb(168, 206, 58) : rgb(60, 72, 30);
    const ImU32 bg_bot = lit ? rgb(132, 176, 36) : rgb(48, 58, 24);
    dl->AddRectFilledMultiColor(g0, g1, bg_top, bg_top, bg_bot, bg_bot);
    const float gw = g1.x - g0.x, gh = g1.y - g0.y;
    const float p = std::max(1.0f, std::floor(std::min((gw - 16 * s_) / 95.0f, (gh - 12 * s_) / 18.0f)));
    const ImVec2 o(std::round(g0.x + (gw - 95 * p) * 0.5f), std::round(g0.y + (gh - 18 * p) * 0.5f));
    const ImU32 on = rgb(28, 40, 12, 235), off = lit ? rgb(0, 0, 0, 16) : rgb(0, 0, 0, 0);
    const float dot = p < 2.0f ? 1.0f : std::clamp(std::round(p * 0.86f), 1.0f, p - 1.0f);

    std::array<uint8_t, 32> text = v.lcd;
    bool display_on = (v.lcd_display & 0x04u) != 0;
    if (!lit) {
        const char *msg = has_builtin_ ? "                " : "  LOAD A ROM    ";
        for (int i = 0; i < 16; ++i) text[16 + i] = static_cast<uint8_t>(msg[i]);
        for (int i = 0; i < 16; ++i) text[i] = ' ';
        display_on = !has_builtin_;
    }
    const bool blink_phase = std::fmod(t, 0.8) < 0.4;
    for (int cell = 0; cell < 32; ++cell) {
        const int row = cell / 16, col = cell % 16;
        const ImVec2 c0(o.x + col * 6 * p, o.y + row * 10 * p);
        const uint8_t *g = glyph(text[cell], v);
        const bool cursor = lit && display_on && v.lcd_cursor == cell;
        const bool block = cursor && (v.lcd_display & 0x01u) && blink_phase;
        const bool underline = cursor && (v.lcd_display & 0x02u);
        for (int y = 0; y < 8; ++y) {
            uint8_t bits = display_on ? g[y] : 0;
            if (block || (underline && y == 7)) bits = 0x1F;
            for (int x = 0; x < 5; ++x) {
                const ImVec2 d0(c0.x + x * p, c0.y + y * p);
                dl->AddRectFilled(d0, ImVec2(d0.x + dot, d0.y + dot), (bits & (0x10 >> x)) ? on : off);
            }
        }
    }
    dl->AddRectFilledMultiColor(g0, ImVec2(g1.x, g0.y + gh * 0.45f), rgb(255, 255, 255, 26), rgb(255, 255, 255, 26),
                                rgb(255, 255, 255, 0), rgb(255, 255, 255, 0));
}

void PanelUi::draw_key(ImDrawList *dl, int i, bool down, const mmt_emu::PanelView &v) {
    const PanelKey &k = kPanelKeys[i];
    const float press = down ? 1.5f * s_ : 0.0f;
    const ImVec2 a = P(k.x, k.y), b = P(k.x + k.w, k.y + k.h);
    const float r = 4.0f * s_;
    dl->AddRectFilled(ImVec2(a.x, a.y + 2.5f * s_), ImVec2(b.x, b.y + 2.5f * s_), rgb(0, 0, 0, 110), r);
    const ImVec2 ka(a.x, a.y + press), kb(b.x, b.y + press);
    const bool dark = k.cap == Cap::Transport || k.cap == Cap::Record;
    ImU32 top, bot, txt;
    if (dark) {
        top = down ? rgb(44, 45, 48) : rgb(66, 67, 71);
        bot = down ? rgb(34, 35, 38) : rgb(44, 45, 49);
        txt = rgb(232, 232, 228);
    } else {
        top = down ? rgb(170, 168, 160) : rgb(214, 212, 204);
        bot = down ? rgb(150, 148, 141) : rgb(180, 178, 170);
        txt = rgb(30, 30, 30);
    }
    dl->AddRectFilled(ka, kb, bot, r);
    dl->AddRectFilledMultiColor(ImVec2(ka.x + 1, ka.y + 1), ImVec2(kb.x - 1, ka.y + (kb.y - ka.y) * 0.6f), top, top, bot, bot);
    dl->AddRect(ka, kb, rgb(0, 0, 0, 140), r, 0, 1.0f);
    dl->AddLine(ImVec2(ka.x + r, ka.y + 1), ImVec2(kb.x - r, ka.y + 1), rgb(255, 255, 255, dark ? 30 : 110), 1.0f);
    if (latched_[i])
        dl->AddRect(ImVec2(ka.x - 2 * s_, ka.y - 2 * s_), ImVec2(kb.x + 2 * s_, kb.y + 2 * s_), rgb(255, 176, 32),
                    r + 2 * s_, 0, 2.0f * s_);

    const ImVec2 c((ka.x + kb.x) * 0.5f, (ka.y + kb.y) * 0.5f);
    if (k.cap == Cap::Transport || k.cap == Cap::Record) {
        draw_icon(dl, k.id, ImVec2(c.x, c.y - 6 * s_), 6.5f * s_, k.cap == Cap::Record ? rgb(235, 52, 40) : txt);
        text_centered(dl, 10 * s_, ImVec2(c.x, c.y + 11 * s_), rgb(190, 190, 186), k.label2);
    } else if (k.id == 22 || k.id == 23) {
        text_centered(dl, 11.5f * s_, ImVec2(c.x, c.y - 7 * s_), txt, k.label);
        draw_icon(dl, k.id, ImVec2(c.x, c.y + 8 * s_), 5.0f * s_, txt);
    } else if (k.label2) {
        text_centered(dl, 11.5f * s_, ImVec2(c.x, c.y - 7 * s_), txt, k.label);
        text_centered(dl, 11.5f * s_, ImVec2(c.x, c.y + 7 * s_), txt, k.label2);
    } else {
        const float size = (k.cap == Cap::Function ? 12.5f : 17.0f) * s_;
        text_centered(dl, size, c, txt, k.label);
    }

    if (k.led == Led::Mode) {
        draw_led(dl, P(k.x + 8, k.y - 8), v.rom_loaded && !(v.led_modes & (1u << k.led_bit)));
    } else if (k.led == Led::Track) {
        draw_led(dl, P(k.x + k.w * 0.5f, k.y - 9), v.rom_loaded && !(v.led_tracks & (1u << k.led_bit)));
    }
}

void PanelUi::draw_footswitch(ImDrawList *dl) {
    const ImVec2 a = P(32, 392), b = P(132, 416);
    const bool down = foot_down_;
    dl->AddRectFilled(a, b, down ? rgb(40, 41, 44) : rgb(24, 24, 26), 12 * s_);
    dl->AddRect(a, b, foot_latched_ ? rgb(255, 176, 32) : rgb(255, 255, 255, 30), 12 * s_, 0, 1.5f * s_);
    dl->AddCircleFilled(P(46, 404), 4 * s_, down ? rgb(255, 176, 32) : rgb(70, 70, 70), 16);
    dl->AddText(font_, 10.5f * s_, P(56, 397), rgb(200, 200, 196), "FOOTSWITCH");
    foot_rect_[0] = a;
    foot_rect_[1] = b;
}

bool PanelUi::toolbar_toggle(const char *id, const char *label, bool on, bool indicator, ImVec2 pos, float w) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushID(id);
    const ImVec2 sz(w, 24 * s_);
    const bool clicked = ImGui::InvisibleButton("t", sz);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 b(pos.x + sz.x, pos.y + sz.y);
    dl->AddRectFilled(pos, b, hovered ? rgb(36, 37, 40) : rgb(22, 22, 24), 12 * s_);
    dl->AddRect(pos, b, rgb(255, 255, 255, 30), 12 * s_, 0, 1.0f);
    const ImVec2 dot(pos.x + 13 * s_, pos.y + sz.y * 0.5f);
    dl->AddCircleFilled(dot, 4 * s_, on ? (indicator ? rgb(80, 220, 110) : rgb(60, 140, 80)) : rgb(70, 70, 70), 16);
    dl->AddText(font_, 11 * s_, ImVec2(pos.x + 23 * s_, pos.y + 5.5f * s_), rgb(220, 220, 216), label);
    ImGui::PopID();
    return clicked;
}

void PanelUi::draw_toolbar() {
    ImGui::PushFont(font_, 13 * s_);
    const bool sync = engine_.param(kParamSync) >= 0.5;
    const bool receiving = engine_.host_sync_receiving();
    const ImVec2 base = P(470, 12);
    if (toolbar_toggle("sync", "DAW SYNC", sync, receiving, base, 96 * s_)) {
        engine_.set_param_from_gui(kParamSync, sync ? 0.0 : 1.0);
        if (host_.params_changed) host_.params_changed();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Acts as a MIDI cable from the DAW: START, STOP, CONTINUE, SONG POSITION\n"
                          "and CLOCK follow the DAW transport. Set CLOCK to MIDI on OpenMMT-8\n"
                          "to make it follow the DAW tempo.");

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddText(font_, 11 * s_, P(580, 17.5f), rgb(170, 170, 166), "CLICK");
    ImGui::SetCursorScreenPos(P(616, 15));
    ImGui::SetNextItemWidth(84 * s_);
    float click = static_cast<float>(engine_.param(kParamClick));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4 * s_, 2 * s_));
    if (ImGui::SliderFloat("##click", &click, 0.0f, 1.0f, "")) {
        engine_.set_param_from_gui(kParamClick, click);
        if (host_.params_changed) host_.params_changed();
    }
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Click output level (the ROM's click on the main output)");

    const ImVec2 rp = P(714, 12);
    ImGui::SetCursorScreenPos(rp);
    const ImVec2 rs(254 * s_, 24 * s_);
    if (ImGui::InvisibleButton("rom", rs)) ImGui::OpenPopup("rom_menu");
    const bool hovered = ImGui::IsItemHovered();
    dl->AddRectFilled(rp, ImVec2(rp.x + rs.x, rp.y + rs.y), hovered ? rgb(36, 37, 40) : rgb(22, 22, 24), 12 * s_);
    dl->AddRect(rp, ImVec2(rp.x + rs.x, rp.y + rs.y), rgb(255, 255, 255, 30), 12 * s_, 0, 1.0f);
    std::string label = "ROM  " + rom_name_;
    while (label.size() > 6 && font_->CalcTextSizeA(11 * s_, FLT_MAX, 0, label.c_str()).x > rs.x - 40 * s_)
        label.erase(label.size() - 1);
    dl->AddText(font_, 11 * s_, ImVec2(rp.x + 12 * s_, rp.y + 5.5f * s_), rgb(220, 220, 216), label.c_str());
    draw_icon(dl, 23, ImVec2(rp.x + rs.x - 15 * s_, rp.y + rs.y * 0.5f), 4.0f * s_, rgb(170, 170, 166));
    ImGui::SetNextWindowPos(ImVec2(rp.x, rp.y + rs.y + 4 * s_));
    if (ImGui::BeginPopup("rom_menu")) {
        if (ImGui::MenuItem("Load ROM file...")) {
            if (path_prompt_) show_path_ = true;
            else pending_load_ = true;
        }
        if (ImGui::MenuItem("Use built-in OpenMMT-8", nullptr, rom_builtin_ == Builtin::OpenMMT8, has_builtin_)) {
            engine_.use_builtin_rom(Builtin::OpenMMT8);
            if (host_.state_changed) host_.state_changed();
        }
        if (ImGui::MenuItem("Use built-in JAM-8", nullptr, rom_builtin_ == Builtin::Jam8, has_jam_)) {
            engine_.use_builtin_rom(Builtin::Jam8);
            if (host_.state_changed) host_.state_changed();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Power off and on", nullptr, false, engine_.rom_loaded())) engine_.power_cycle();
        ImGui::TextDisabled("Each ROM keeps its own memory, like the battery backup.");
        ImGui::TextDisabled("A ROM used for the first time starts with empty memory.");
        ImGui::TextDisabled("Not affiliated with Alesis.");
        ImGui::EndPopup();
    }
    if (!status_.empty()) {
        const double age = ImGui::GetTime() - status_time_;
        if (age < 6.0) dl->AddText(font_, 11 * s_, P(470, 52), rgb(255, 120, 100), status_.c_str());
        else status_.clear();
    }
    ImGui::PopFont();
}

void PanelUi::draw_path_prompt() {
    if (!show_path_) return;
    ImGui::SetNextWindowPos(P(420, 48), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(520 * s_, 0));
    ImGui::PushFont(font_, 14 * s_);
    bool open = true;
    if (ImGui::Begin("Load ROM file", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Type the path of a ROM file that you have the right to use.");
        ImGui::SetNextItemWidth(480 * s_);
        ImGui::InputText("##rompath", path_buf_, sizeof(path_buf_));
        if (ImGui::Button("Load")) {
            std::string error;
            if (engine_.load_rom_file(path_buf_, error)) {
                status_.clear();
                show_path_ = false;
                if (host_.state_changed) host_.state_changed();
            } else {
                status_ = error.empty() ? "The ROM file did not load." : error;
                status_time_ = ImGui::GetTime();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) show_path_ = false;
    }
    ImGui::End();
    ImGui::PopFont();
    if (!open) show_path_ = false;
}

void PanelUi::set_key(int idx, bool down) {
    const PanelKey &k = kPanelKeys[idx];
    const bool was = held_[idx] != 0;
    held_[idx] = static_cast<uint8_t>(down ? 1 : 0);
    if (was != down) engine_.press(k.id, down);
}

void PanelUi::release_keyboard() {
    for (int i = 0; i < kPanelKeyCount; ++i)
        if (kb_[i]) {
            kb_[i] = 0;
            if (!latched_[i] && mouse_key_ != i) set_key(i, false);
        }
}

void PanelUi::release_all() {
    for (int i = 0; i < kPanelKeyCount; ++i) {
        if (held_[i]) engine_.press(kPanelKeys[i].id, false);
        held_[i] = latched_[i] = kb_[i] = 0;
    }
    mouse_key_ = -1;
    if (foot_down_) engine_.press(kKeyFootswitch, false);
    foot_down_ = foot_latched_ = foot_mouse_ = false;
}

int PanelUi::key_for_shortcut(ImGuiKey key) const {
    uint8_t id = 0xFF;
    static const uint8_t digit_ids[10] = {29, 33, 34, 35, 36, 37, 25, 26, 27, 28};
    if (key >= ImGuiKey_0 && key <= ImGuiKey_9) id = digit_ids[key - ImGuiKey_0];
    else if (key >= ImGuiKey_Keypad0 && key <= ImGuiKey_Keypad9) id = digit_ids[key - ImGuiKey_Keypad0];
    else if (key >= ImGuiKey_F1 && key <= ImGuiKey_F8) id = static_cast<uint8_t>(8 + (key - ImGuiKey_F1));
    else if (key == ImGuiKey_Minus || key == ImGuiKey_KeypadSubtract) id = 17;
    else if (key == ImGuiKey_Equal || key == ImGuiKey_KeypadAdd) id = 18;
    else if (key == ImGuiKey_PageUp) id = 22;
    else if (key == ImGuiKey_PageDown) id = 23;
    for (int i = 0; i < kPanelKeyCount; ++i)
        if (kPanelKeys[i].id == id && kPanelKeys[i].hint) return i;
    return -1;
}

void PanelUi::keyboard() {
    static const ImGuiKey keys[] = {
        ImGuiKey_0,       ImGuiKey_1,        ImGuiKey_2,           ImGuiKey_3,        ImGuiKey_4,
        ImGuiKey_5,       ImGuiKey_6,        ImGuiKey_7,           ImGuiKey_8,        ImGuiKey_9,
        ImGuiKey_Keypad0, ImGuiKey_Keypad1,  ImGuiKey_Keypad2,     ImGuiKey_Keypad3,  ImGuiKey_Keypad4,
        ImGuiKey_Keypad5, ImGuiKey_Keypad6,  ImGuiKey_Keypad7,     ImGuiKey_Keypad8,  ImGuiKey_Keypad9,
        ImGuiKey_F1,      ImGuiKey_F2,       ImGuiKey_F3,          ImGuiKey_F4,       ImGuiKey_F5,
        ImGuiKey_F6,      ImGuiKey_F7,       ImGuiKey_F8,          ImGuiKey_Minus,    ImGuiKey_KeypadSubtract,
        ImGuiKey_Equal,   ImGuiKey_KeypadAdd, ImGuiKey_PageUp,     ImGuiKey_PageDown};
    if (show_path_ || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    for (ImGuiKey key : keys) {
        const int i = key_for_shortcut(key);
        if (i < 0) continue;
        if (ImGui::IsKeyPressed(key, false)) {
            kb_[i] = 1;
            set_key(i, true);
        } else if (ImGui::IsKeyReleased(key)) {
            kb_[i] = 0;
            if (!latched_[i] && mouse_key_ != i) set_key(i, false);
        }
    }
}

void PanelUi::set_foot(bool down) {
    if (foot_down_ == down) return;
    foot_down_ = down;
    engine_.press(kKeyFootswitch, down);
}

double PanelUi::hover_time(int i) {
    const double now = ImGui::GetTime();
    if (i != hover_key_) {
        hover_key_ = i;
        hover_since_ = now;
    }
    return now - hover_since_;
}

bool PanelUi::draw(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (engine_.rom_version() != rom_version_seen_) {
        rom_version_seen_ = engine_.rom_version();
        rom_name_ = engine_.rom_name();
        rom_builtin_ = engine_.rom_builtin();
    }
    has_builtin_ = Engine::has_builtin_rom(Builtin::OpenMMT8);
    has_jam_ = Engine::has_builtin_rom(Builtin::Jam8);

    s_ = std::min(width / kPanelW, height / kPanelH);
    org_ = ImVec2(std::round((width - kPanelW * s_) * 0.5f), std::round((height - kPanelH * s_) * 0.5f));

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)));
    ImGui::Begin("panel", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    mmt_emu::PanelView v;
    engine_.read_panel(v);
    const double t = ImGui::GetTime();

    draw_chassis(dl, ImVec2(static_cast<float>(width), static_cast<float>(height)));
    draw_lcd(dl, v, t);

    const ImVec2 m = ImGui::GetIO().MousePos;
    const bool popup = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) || show_path_;
    int hover = -1;
    for (int i = 0; i < kPanelKeyCount && !popup; ++i) {
        const PanelKey &k = kPanelKeys[i];
        const ImVec2 a = P(k.x, k.y), b = P(k.x + k.w, k.y + k.h);
        if (m.x >= a.x && m.x < b.x && m.y >= a.y && m.y < b.y) hover = i;
    }
    const bool over_foot = !popup && m.x >= foot_rect_[0].x && m.x < foot_rect_[1].x && m.y >= foot_rect_[0].y &&
                           m.y < foot_rect_[1].y;
    if (hover >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (latched_[hover]) {
            latched_[hover] = 0;
            set_key(hover, false);
        } else {
            mouse_key_ = hover;
            set_key(hover, true);
        }
    }
    if (hover >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        latched_[hover] = latched_[hover] ? 0 : 1;
        set_key(hover, latched_[hover] != 0 || kb_[hover] != 0);
    }
    if (mouse_key_ >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!latched_[mouse_key_] && !kb_[mouse_key_]) set_key(mouse_key_, false);
        mouse_key_ = -1;
    }
    if (over_foot && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        foot_latched_ = !foot_latched_;
        set_foot(foot_latched_);
    } else if (over_foot && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        foot_latched_ = false;
        foot_mouse_ = true;
        set_foot(true);
    }
    if (foot_mouse_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        foot_mouse_ = false;
        if (!foot_latched_) set_foot(false);
    }
    keyboard();

    for (int i = 0; i < kPanelKeyCount; ++i) draw_key(dl, i, held_[i] != 0, v);
    draw_footswitch(dl);
    dl->AddText(font_, 10 * s_, P(150, 391), rgb(130, 130, 126), "Right-click a key to hold it down.");
    dl->AddText(font_, 10 * s_, P(150, 405), rgb(130, 130, 126), "Keys: 0-9  + -  PgUp PgDn  F1-F8 tracks");
    if (hover >= 0 &&
        ImGui::IsMouseHoveringRect(P(kPanelKeys[hover].x, kPanelKeys[hover].y),
                                   P(kPanelKeys[hover].x + kPanelKeys[hover].w, kPanelKeys[hover].y + kPanelKeys[hover].h)) &&
        ImGui::GetIO().MouseDownDuration[0] < 0 && hover_time(hover) > 0.8) {
        ImGui::PushFont(font_, 13 * s_);
        if (kPanelKeys[hover].hint) ImGui::SetTooltip("Key: %s   (right-click to hold)", kPanelKeys[hover].hint);
        else ImGui::SetTooltip("Right-click to hold");
        ImGui::PopFont();
    }
    draw_toolbar();
    ImGui::End();
    draw_path_prompt();

    const bool native = pending_load_;
    pending_load_ = false;
    return native;
}

}  // namespace mmt8
