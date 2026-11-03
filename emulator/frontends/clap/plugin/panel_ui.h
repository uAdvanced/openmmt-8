#pragma once

#include <array>
#include <string>

#include "gui.h"
#include "imgui.h"
#include "mmt_emu/plugin_port.hpp"
#include "engine.h"
#include "panel_layout.h"

struct ImFont;

namespace mmt8 {

class Engine;

/** Panel drawing and key handling. The platform file owns the window and the OpenGL context. */
class PanelUi {
public:
    PanelUi(Engine &engine, GuiHost &host);

    void apply_style() const;
    void set_font(ImFont *font);
    /** When set, Load ROM asks for a path in the panel. Otherwise draw() requests a native dialog. */
    void enable_path_prompt(bool on) { path_prompt_ = on; }

    /** Call after ImGui::NewFrame(). Returns true when the platform must open a file dialog. */
    bool draw(int width, int height);
    void release_keyboard();
    void release_all();
    void set_status(const std::string &error);

private:
    ImVec2 P(float x, float y) const;
    void text_centered(ImDrawList *dl, float size, ImVec2 center, ImU32 col, const char *t);
    void draw_led(ImDrawList *dl, ImVec2 c, bool on);
    void draw_chassis(ImDrawList *dl, ImVec2 size);
    void draw_lcd(ImDrawList *dl, const mmt_emu::PanelView &v, double t);
    void draw_key(ImDrawList *dl, int i, bool down, const mmt_emu::PanelView &v);
    void draw_footswitch(ImDrawList *dl);
    bool toolbar_toggle(const char *id, const char *label, bool on, bool indicator, ImVec2 pos, float w);
    void draw_toolbar();
    void draw_path_prompt();
    void set_key(int idx, bool down);
    void set_foot(bool down);
    int key_for_shortcut(ImGuiKey key) const;
    void keyboard();
    double hover_time(int i);

    Engine &engine_;
    GuiHost &host_;
    ImFont *font_ = nullptr;
    float s_ = 1.0f;
    ImVec2 org_{0, 0};
    std::array<uint8_t, kPanelKeyCount> held_{}, latched_{}, kb_{};
    int mouse_key_ = -1;
    int hover_key_ = -1;
    double hover_since_ = 0.0;
    bool foot_down_ = false;
    bool foot_latched_ = false;
    bool foot_mouse_ = false;
    ImVec2 foot_rect_[2]{};
    uint32_t rom_version_seen_ = ~0u;
    std::string rom_name_;
    Builtin rom_builtin_ = Builtin::OpenMMT8;
    bool has_builtin_ = true;
    bool has_jam_ = false;
    bool pending_load_ = false;
    bool path_prompt_ = false;
    bool show_path_ = false;
    char path_buf_[1024]{};
    std::string status_;
    double status_time_ = 0.0;
};

}  // namespace mmt8
