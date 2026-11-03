#include "gui.h"

#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "builtin_rom.h"
#include "engine.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "log.h"
#include "panel_ui.h"

namespace mmt8 {
namespace {

ImGuiKey map_key(KeySym sym) {
    if (sym >= XK_0 && sym <= XK_9) return static_cast<ImGuiKey>(ImGuiKey_0 + (sym - XK_0));
    if (sym >= XK_KP_0 && sym <= XK_KP_9) return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + (sym - XK_KP_0));
    if (sym >= XK_F1 && sym <= XK_F8) return static_cast<ImGuiKey>(ImGuiKey_F1 + (sym - XK_F1));
    switch (sym) {
        case XK_minus:
        case XK_KP_Subtract: return ImGuiKey_Minus;
        case XK_plus:
        case XK_equal:
        case XK_KP_Add: return ImGuiKey_Equal;
        case XK_Page_Up:
        case XK_KP_Page_Up: return ImGuiKey_PageUp;
        case XK_Page_Down:
        case XK_KP_Page_Down: return ImGuiKey_PageDown;
        case XK_BackSpace: return ImGuiKey_Backspace;
        case XK_Return:
        case XK_KP_Enter: return ImGuiKey_Enter;
        case XK_Left: return ImGuiKey_LeftArrow;
        case XK_Right: return ImGuiKey_RightArrow;
        case XK_Home: return ImGuiKey_Home;
        case XK_End: return ImGuiKey_End;
        case XK_Shift_L: return ImGuiKey_LeftShift;
        case XK_Shift_R: return ImGuiKey_RightShift;
        default: return ImGuiKey_None;
    }
}

class X11Gui final : public Gui {
public:
    X11Gui(Engine &engine, GuiHost host) : host_(std::move(host)), panel_(engine, host_) {
        panel_.enable_path_prompt(true);
    }
    ~X11Gui() override { destroy(); }

    bool set_parent(void *native) override {
        destroy();
        const Window parent = static_cast<Window>(reinterpret_cast<uintptr_t>(native));
        if (!parent) return false;
        XInitThreads();
        dpy_ = XOpenDisplay(nullptr);
        if (!dpy_) {
            log("gui: XOpenDisplay failed");
            return false;
        }
        const int screen = DefaultScreen(dpy_);
        int attrs[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None};
        vi_ = glXChooseVisual(dpy_, screen, attrs);
        if (!vi_) {
            log("gui: glXChooseVisual failed");
            destroy();
            return false;
        }
        cmap_ = XCreateColormap(dpy_, parent, vi_->visual, AllocNone);
        XSetWindowAttributes swa{};
        swa.colormap = cmap_;
        swa.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                         KeyPressMask | KeyReleaseMask | FocusChangeMask;
        win_ = XCreateWindow(dpy_, parent, 0, 0, width_, height_, 0, vi_->depth, InputOutput, vi_->visual,
                             CWColormap | CWEventMask, &swa);
        if (!win_) {
            log("gui: XCreateWindow failed");
            destroy();
            return false;
        }
        glx_ = glXCreateContext(dpy_, vi_, nullptr, True);
        if (!glx_ || !glXMakeCurrent(dpy_, win_, glx_)) {
            log("gui: GLX context creation failed");
            destroy();
            return false;
        }
        ctx_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx_);
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        ImFont *font = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(kPanelFont), static_cast<int>(kPanelFontSize),
                                                      18.0f, &cfg);
        panel_.set_font(font);
        panel_.apply_style();
        ImGui_ImplOpenGL3_Init("#version 130");
        glXMakeCurrent(dpy_, None, nullptr);
        XMapWindow(dpy_, win_);
        XFlush(dpy_);
        stop_ = false;
        timer_ = std::thread([this] { loop(); });
        log("gui: opened %ux%u on X11", width_, height_);
        return true;
    }

    void set_scale(double scale) override { scale_ = scale > 0.25 ? scale : 1.0; }
    double scale() const override { return scale_ > 0.0 ? scale_ : 1.0; }

    void set_size(uint32_t w, uint32_t h) override {
        width_ = w;
        height_ = h;
        std::lock_guard<std::mutex> lock(mu_);
        if (dpy_ && win_) {
            XResizeWindow(dpy_, win_, w, h);
            XFlush(dpy_);
        }
    }

    void show(bool visible) override {
        std::lock_guard<std::mutex> lock(mu_);
        if (!dpy_ || !win_) return;
        if (visible) XMapWindow(dpy_, win_);
        else {
            panel_.release_all();
            XUnmapWindow(dpy_, win_);
        }
        XFlush(dpy_);
    }

private:
    void loop() {
        while (!stop_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            std::lock_guard<std::mutex> lock(mu_);
            if (stop_ || !dpy_ || !win_ || !glx_) continue;
            XLockDisplay(dpy_);
            pump();
            frame();
            XUnlockDisplay(dpy_);
        }
    }

    void pump() {
        ImGui::SetCurrentContext(ctx_);
        ImGuiIO &io = ImGui::GetIO();
        while (XPending(dpy_)) {
            XEvent ev;
            XNextEvent(dpy_, &ev);
            switch (ev.type) {
                case MotionNotify: io.AddMousePosEvent(static_cast<float>(ev.xmotion.x), static_cast<float>(ev.xmotion.y)); break;
                case ButtonPress:
                case ButtonRelease: {
                    if (ev.type == ButtonPress) XSetInputFocus(dpy_, win_, RevertToParent, CurrentTime);
                    int button = -1;
                    if (ev.xbutton.button == Button1) button = 0;
                    else if (ev.xbutton.button == Button3) button = 1;
                    else if (ev.xbutton.button == Button2) button = 2;
                    if (button >= 0) io.AddMouseButtonEvent(button, ev.type == ButtonPress);
                    break;
                }
                case KeyPress:
                case KeyRelease: {
                    if (ev.type == KeyRelease && XEventsQueued(dpy_, QueuedAfterReading)) {
                        XEvent next;
                        XPeekEvent(dpy_, &next);
                        if (next.type == KeyPress && next.xkey.time == ev.xkey.time && next.xkey.keycode == ev.xkey.keycode) {
                            XNextEvent(dpy_, &next);
                            break;
                        }
                    }
                    char buf[16];
                    KeySym sym = 0;
                    const int n = XLookupString(&ev.xkey, buf, static_cast<int>(sizeof(buf)), &sym, nullptr);
                    const ImGuiKey key = map_key(sym);
                    if (key != ImGuiKey_None) io.AddKeyEvent(key, ev.type == KeyPress);
                    if (ev.type == KeyPress)
                        for (int i = 0; i < n; ++i)
                            if (static_cast<unsigned char>(buf[i]) >= 32) io.AddInputCharacter(static_cast<unsigned>(buf[i]));
                    break;
                }
                case FocusOut: panel_.release_keyboard(); break;
                default: break;
            }
        }
    }

    void frame() {
        if (!ctx_ || !glXMakeCurrent(dpy_, win_, glx_)) return;
        XWindowAttributes attr{};
        if (!XGetWindowAttributes(dpy_, win_, &attr) || attr.width <= 0 || attr.height <= 0) {
            glXMakeCurrent(dpy_, None, nullptr);
            return;
        }
        ImGui::SetCurrentContext(ctx_);
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(attr.width), static_cast<float>(attr.height));
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        panel_.draw(attr.width, attr.height);
        ImGui::Render();
        glViewport(0, 0, attr.width, attr.height);
        glClearColor(0.055f, 0.055f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glXSwapBuffers(dpy_, win_);
        glXMakeCurrent(dpy_, None, nullptr);
    }

    void destroy() {
        stop_ = true;
        if (timer_.joinable()) timer_.join();
        std::lock_guard<std::mutex> lock(mu_);
        panel_.release_all();
        if (ctx_) {
            ImGui::SetCurrentContext(ctx_);
            if (dpy_ && glx_) glXMakeCurrent(dpy_, win_, glx_);
            ImGui_ImplOpenGL3_Shutdown();
            ImGui::DestroyContext(ctx_);
            ctx_ = nullptr;
        }
        if (dpy_ && glx_) {
            glXMakeCurrent(dpy_, None, nullptr);
            glXDestroyContext(dpy_, glx_);
            glx_ = nullptr;
        }
        if (dpy_ && win_) {
            XDestroyWindow(dpy_, win_);
            win_ = 0;
        }
        if (dpy_ && cmap_) {
            XFreeColormap(dpy_, cmap_);
            cmap_ = 0;
        }
        if (vi_) {
            XFree(vi_);
            vi_ = nullptr;
        }
        if (dpy_) {
            XCloseDisplay(dpy_);
            dpy_ = nullptr;
        }
    }

    GuiHost host_;
    PanelUi panel_;
    Display *dpy_ = nullptr;
    Window win_ = 0;
    Colormap cmap_ = 0;
    XVisualInfo *vi_ = nullptr;
    GLXContext glx_ = nullptr;
    ImGuiContext *ctx_ = nullptr;
    uint32_t width_ = kDefaultWidth;
    uint32_t height_ = height_for(kDefaultWidth);
    double scale_ = 1.0;
    std::mutex mu_;
    std::thread timer_;
    std::atomic<bool> stop_{true};
};

}  // namespace

bool Gui::supported() { return true; }

std::unique_ptr<Gui> Gui::create(Engine &engine, GuiHost host) {
    return std::unique_ptr<Gui>(new X11Gui(engine, std::move(host)));
}

}  // namespace mmt8
