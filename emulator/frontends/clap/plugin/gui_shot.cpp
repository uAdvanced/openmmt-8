/* Windows: opens the plugin GUI in a window, runs the emulation and saves screenshots (BMP).
 * usage: mmt8_clap_gui_shot <plugin.clap> <out-prefix> [width] */
#include <windows.h>

#include <clap/clap.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

const void *host_ext(const clap_host_t *, const char *) { return nullptr; }
void noop(const clap_host_t *) {}
clap_host_t g_host = {CLAP_VERSION_INIT, nullptr, "gui shot", "", "", "1", host_ext, noop, noop, noop};

uint32_t ev_size(const clap_input_events_t *) { return 0; }
const clap_event_header_t *ev_get(const clap_input_events_t *, uint32_t) { return nullptr; }
bool ev_push(const clap_output_events_t *, const clap_event_header_t *) { return true; }

struct Runner {
    const clap_plugin_t *p;
    std::vector<float> l = std::vector<float>(512), r = std::vector<float>(512), t = std::vector<float>(512);
    double beats = 0;
    bool playing = false;

    void block() {
        float *m[2] = {l.data(), r.data()};
        float *tp[1] = {t.data()};
        clap_audio_buffer_t outs[2] = {};
        outs[0].data32 = m;
        outs[0].channel_count = 2;
        outs[1].data32 = tp;
        outs[1].channel_count = 1;
        clap_input_events_t in = {nullptr, ev_size, ev_get};
        clap_output_events_t out = {nullptr, ev_push};
        clap_event_transport_t tr{};
        tr.header = {sizeof(tr), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
        tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | (playing ? CLAP_TRANSPORT_IS_PLAYING : 0);
        tr.tempo = 120;
        tr.song_pos_beats = static_cast<clap_beattime>(beats * CLAP_BEATTIME_FACTOR);
        clap_process_t pr{};
        pr.frames_count = 512;
        pr.transport = &tr;
        pr.audio_outputs = outs;
        pr.audio_outputs_count = 2;
        pr.in_events = &in;
        pr.out_events = &out;
        p->process(p, &pr);
        if (playing) beats += 512.0 / 48000.0 * 2.0;
    }
};

void pump(Runner &run, double emu_seconds, int wall_ms) {
    const int blocks = static_cast<int>(emu_seconds * 48000 / 512);
    const DWORD end = GetTickCount() + wall_ms;
    int done = 0;
    while (done < blocks || GetTickCount() < end) {
        for (int i = 0; i < 8 && done < blocks; ++i, ++done) run.block();
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(1);
    }
}

void shot(HWND hwnd, const std::string &path) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right, h = rc.bottom;
    HDC wdc = GetDC(hwnd);
    HDC mdc = CreateCompatibleDC(wdc);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits = nullptr;
    HBITMAP bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mdc, bmp);
    PrintWindow(hwnd, mdc, PW_CLIENTONLY | 2 /* PW_RENDERFULLCONTENT */);
    BITMAPFILEHEADER fh{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader);
    fh.bfSize = fh.bfOffBits + w * h * 4;
    FILE *f = std::fopen(path.c_str(), "wb");
    if (f) {
        std::fwrite(&fh, sizeof(fh), 1, f);
        std::fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f);
        std::fwrite(bits, 4, static_cast<size_t>(w) * h, f);
        std::fclose(f);
    }
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(hwnd, wdc);
    std::printf("saved %s (%dx%d)\n", path.c_str(), w, h);
}

void mouse(HWND child, UINT msg, int x, int y) {
    const WPARAM buttons = msg == WM_LBUTTONDOWN ? MK_LBUTTON : msg == WM_RBUTTONDOWN ? MK_RBUTTON : 0;
    PostMessageW(child, msg, buttons, MAKELPARAM(x, y));
}

void key(HWND child, UINT vk, bool down) {
    PostMessageW(child, down ? WM_KEYDOWN : WM_KEYUP, vk, down ? 1 : 0xC0000001);
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    SetProcessDPIAware();
    HMODULE lib = LoadLibraryA(argv[1]);
    auto *entry = lib ? reinterpret_cast<const clap_plugin_entry_t *>(GetProcAddress(lib, "clap_entry")) : nullptr;
    if (!entry || !entry->init(argv[1])) return 1;
    auto *factory = static_cast<const clap_plugin_factory_t *>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_t *p = factory->create_plugin(factory, &g_host, factory->get_plugin_descriptor(factory, 0)->id);
    p->init(p);
    p->activate(p, 48000, 1, 512);
    p->start_processing(p);
    auto *gui = static_cast<const clap_plugin_gui_t *>(p->get_extension(p, CLAP_EXT_GUI));
    if (!gui || !gui->create(p, CLAP_WINDOW_API_WIN32, false)) return 3;
    gui->set_scale(p, 1.0);
    uint32_t w = 0, h = 0;
    if (argc > 3) {
        w = static_cast<uint32_t>(std::atoi(argv[3]));
        h = 10000;
        gui->adjust_size(p, &w, &h);
    } else {
        gui->get_size(p, &w, &h);
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Mmt8GuiShotHost";
    RegisterClassW(&wc);
    RECT rc = {0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND top = CreateWindowW(wc.lpszClassName, L"OpenMMT-8 GUI shot", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40,
                             rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);
    clap_window_t win{CLAP_WINDOW_API_WIN32, {top}};
    gui->set_size(p, w, h);
    if (!gui->set_parent(p, &win)) return 4;
    gui->show(p);
    HWND child = GetWindow(top, GW_CHILD);
    SetForegroundWindow(top);
    SetFocus(child);

    Runner run{p};
    const std::string out = argv[2];
    pump(run, 3.0, 600);
    shot(top, out + "_boot.bmp");

    key(child, VK_F3, true);  // track 3 off
    pump(run, 0.15, 100);
    key(child, VK_F3, false);
    key(child, '5', true);    // keypad 5 held down
    pump(run, 0.2, 300);
    shot(top, out + "_keys.bmp");
    key(child, '5', false);

    /* Right-click latches CLOCK down, then a left click on keypad 1 picks MIDI & INTERNAL. Each
     * click carries its own move: the real cursor is elsewhere, so Windows reports a mouse-leave
     * right after every posted move. */
    auto click = [&](UINT down, UINT up, int x, int y, double hold) {
        mouse(child, WM_MOUSEMOVE, x, y);
        mouse(child, down, x, y);
        pump(run, hold, 80);
        mouse(child, WM_MOUSEMOVE, x, y);
        mouse(child, up, x, y);
        pump(run, 0.25, 120);
    };
    click(WM_RBUTTONDOWN, WM_RBUTTONUP, 917, 82, 0.05);
    click(WM_LBUTTONDOWN, WM_LBUTTONUP, 64, 282, 0.15);
    shot(top, out + "_latch.bmp");
    click(WM_LBUTTONDOWN, WM_LBUTTONUP, 917, 82, 0.05);  // a left click releases a latched key

    run.playing = true;       // the DAW plays: START + clocks over the MIDI cable
    pump(run, 2.2, 400);
    shot(top, out + "_play.bmp");

    gui->hide(p);
    gui->destroy(p);
    DestroyWindow(top);
    p->stop_processing(p);
    p->deactivate(p);
    p->destroy(p);
    entry->deinit();
    return 0;
}
