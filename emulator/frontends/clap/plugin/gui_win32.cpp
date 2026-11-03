#include "gui.h"

#include <windows.h>
#include <commdlg.h>
#include <GL/gl.h>

#include <string>

#include "builtin_rom.h"
#include "engine.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_win32.h"
#include "log.h"
#include "panel_ui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandlerEx(HWND, UINT, WPARAM, LPARAM, ImGuiIO &);
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace mmt8 {
namespace {

constexpr wchar_t kClassName[] = L"OpenMMT8PanelWindow";
constexpr UINT_PTR kTimerId = 1;

/* Open panel windows (GUI calls come on the main thread). Windows keeps a DLL's window class
 * after the DLL unloads, and a rebuilt plugin loads at the same address: a class left over
 * from an earlier load would send messages to code that is no longer there. So the class
 * lives only while a panel is open. */
int g_windows = 0;

std::string utf8(const wchar_t *w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

class Win32Gui final : public Gui {
public:
    Win32Gui(Engine &engine, GuiHost host) : engine_(engine), host_(std::move(host)), panel_(engine_, host_) {}
    ~Win32Gui() override { destroy(); }

    bool set_parent(void *native) override {
        destroy();
        HWND parent = static_cast<HWND>(native);
        if (!parent || !register_class()) return false;
        if (scale_ <= 0.0) {
            using DpiFn = UINT(WINAPI *)(HWND);
            auto dpi_for = reinterpret_cast<DpiFn>(
                reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
            const UINT dpi = dpi_for ? dpi_for(parent) : 96;
            scale_ = dpi ? dpi / 96.0 : 1.0;
        }
        hwnd_ = CreateWindowExW(0, kClassName, L"OpenMMT-8", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0,
                                0, static_cast<int>(width_), static_cast<int>(height_), parent, nullptr, instance(), this);
        if (!hwnd_) {
            log("gui: CreateWindowEx failed (%lu)", GetLastError());
            if (g_windows == 0) UnregisterClassW(kClassName, instance());
            return false;
        }
        ++g_windows;
        if (!create_gl()) {
            log("gui: OpenGL context creation failed");
            destroy();
            return false;
        }
        ImGuiContext *prev = ImGui::GetCurrentContext();
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
        ImGui_ImplWin32_Init(hwnd_);
        ImGui_ImplOpenGL3_Init("#version 130");
        ImGui::SetCurrentContext(prev);
        SetTimer(hwnd_, kTimerId, 16, nullptr);
        log("gui: opened %ux%u, scale %.2f", width_, height_, scale_);
        return true;
    }

    void set_scale(double scale) override { scale_ = scale > 0.25 ? scale : 1.0; }
    double scale() const override { return scale_ > 0.0 ? scale_ : 1.0; }

    void set_size(uint32_t w, uint32_t h) override {
        width_ = w;
        height_ = h;
        if (hwnd_)
            SetWindowPos(hwnd_, nullptr, 0, 0, static_cast<int>(w), static_cast<int>(h), SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    }

    void show(bool visible) override {
        if (hwnd_) ShowWindow(hwnd_, visible ? SW_SHOWNA : SW_HIDE);
        if (!visible) panel_.release_all();
    }

private:
    static HINSTANCE instance() { return reinterpret_cast<HINSTANCE>(&__ImageBase); }

    static bool register_class() {
        if (g_windows > 0) return true;
        UnregisterClassW(kClassName, instance());
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC | CS_DBLCLKS;
        wc.lpfnWndProc = &Win32Gui::wndproc;
        wc.hInstance = instance();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        if (RegisterClassExW(&wc)) return true;
        log("gui: RegisterClassEx failed (%lu)", GetLastError());
        return false;
    }

    static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_NCCREATE) {
            auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        auto *self = reinterpret_cast<Win32Gui *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
        switch (msg) {
            case WM_TIMER:
                if (wp == kTimerId) self->frame();
                return 0;
            case WM_PAINT: {
                PAINTSTRUCT ps;
                BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);
                self->frame();
                return 0;
            }
            case WM_ERASEBKGND: return 1;
            case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
                SetFocus(hwnd);
                break;
            case WM_KILLFOCUS:
                self->panel_.release_keyboard();
                break;
            default: break;
        }
        if (self->ctx_) {
            ImGuiContext *prev = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(self->ctx_);
            const LRESULT r = ImGui_ImplWin32_WndProcHandlerEx(hwnd, msg, wp, lp, ImGui::GetIO());
            ImGui::SetCurrentContext(prev);
            if (r) return r;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    bool create_gl() {
        hdc_ = GetDC(hwnd_);
        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cAlphaBits = 8;
        const int fmt = ChoosePixelFormat(hdc_, &pfd);
        if (!fmt || !SetPixelFormat(hdc_, fmt, &pfd)) return false;
        glrc_ = wglCreateContext(hdc_);
        return glrc_ && wglMakeCurrent(hdc_, glrc_);
    }

    void destroy() {
        panel_.release_all();
        if (hwnd_) KillTimer(hwnd_, kTimerId);
        if (ctx_) {
            ImGuiContext *prev = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(ctx_);
            if (glrc_) wglMakeCurrent(hdc_, glrc_);
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext(ctx_);
            ImGui::SetCurrentContext(prev == ctx_ ? nullptr : prev);
            ctx_ = nullptr;
        }
        if (glrc_) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(glrc_);
            glrc_ = nullptr;
        }
        if (hwnd_) {
            if (hdc_) ReleaseDC(hwnd_, hdc_);
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            DestroyWindow(hwnd_);
            if (--g_windows == 0) UnregisterClassW(kClassName, instance());
        }
        hdc_ = nullptr;
        hwnd_ = nullptr;
    }

    void frame() {
        if (!ctx_ || in_modal_) return;
        RECT rc;
        GetClientRect(hwnd_, &rc);
        const int W = rc.right - rc.left, H = rc.bottom - rc.top;
        if (W <= 0 || H <= 0) return;
        ImGuiContext *prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(ctx_);
        wglMakeCurrent(hdc_, glrc_);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        const bool load_rom = panel_.draw(W, H);
        ImGui::Render();
        glViewport(0, 0, W, H);
        glClearColor(0.055f, 0.055f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(hdc_);
        ImGui::SetCurrentContext(prev);
        if (load_rom) pick_rom();
    }

    void pick_rom() {
        wchar_t file[MAX_PATH * 2] = L"";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetAncestor(hwnd_, GA_ROOT);
        ofn.lpstrFilter = L"ROM images (*.bin;*.rom;*.hex)\0*.bin;*.rom;*.hex\0All files\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = static_cast<DWORD>(sizeof(file) / sizeof(file[0]));
        ofn.lpstrTitle = L"Load ROM";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        in_modal_ = true;
        const BOOL ok = GetOpenFileNameW(&ofn);
        in_modal_ = false;
        if (!ok) return;
        std::string error;
        if (engine_.load_rom_file(utf8(file), error)) {
            if (host_.state_changed) host_.state_changed();
        } else {
            ImGuiContext *prev = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(ctx_);
            panel_.set_status(error.empty() ? "The ROM file did not load." : error);
            ImGui::SetCurrentContext(prev);
        }
    }

    Engine &engine_;
    GuiHost host_;
    PanelUi panel_;
    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HGLRC glrc_ = nullptr;
    ImGuiContext *ctx_ = nullptr;
    uint32_t width_ = kDefaultWidth;
    uint32_t height_ = height_for(kDefaultWidth);
    double scale_ = 0.0;
    bool in_modal_ = false;
};

}  // namespace

bool Gui::supported() { return true; }

std::unique_ptr<Gui> Gui::create(Engine &engine, GuiHost host) {
    return std::unique_ptr<Gui>(new Win32Gui(engine, std::move(host)));
}

}  // namespace mmt8
