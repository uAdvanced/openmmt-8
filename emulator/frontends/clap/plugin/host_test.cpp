/* Minimal CLAP host: loads the built plugin the way a DAW does and checks the basics.
 * usage: mmt8_clap_host_test <path/to/mmt8_clap_plugin.clap> */
#include <clap/clap.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "project_state.h"

namespace {

int g_failures = 0;
#define CHECK(cond, ...)                         \
    do {                                         \
        if (cond) {                              \
            std::printf("ok    ");               \
        } else {                                 \
            std::printf("FAIL  ");               \
            ++g_failures;                        \
        }                                        \
        std::printf(__VA_ARGS__);                \
        std::printf("\n");                       \
    } while (0)

const void *host_get_extension(const clap_host_t *, const char *) { return nullptr; }
void host_noop(const clap_host_t *) {}

clap_host_t g_host = {CLAP_VERSION_INIT, nullptr, "mmt8 host test", "", "", "1", host_get_extension,
                      host_noop,         host_noop, host_noop};

struct EventList {
    std::vector<std::vector<uint8_t>> events;
    static uint32_t size(const clap_input_events_t *l) {
        return static_cast<uint32_t>(static_cast<EventList *>(l->ctx)->events.size());
    }
    static const clap_event_header_t *get(const clap_input_events_t *l, uint32_t i) {
        return reinterpret_cast<const clap_event_header_t *>(static_cast<EventList *>(l->ctx)->events[i].data());
    }
    static bool push(const clap_output_events_t *l, const clap_event_header_t *e) {
        auto *self = static_cast<EventList *>(l->ctx);
        const auto *b = reinterpret_cast<const uint8_t *>(e);
        self->events.emplace_back(b, b + e->size);
        return true;
    }
};

struct Stream {
    std::vector<uint8_t> data;
    size_t at = 0;
    static int64_t write(const clap_ostream_t *s, const void *buf, uint64_t n) {
        auto *self = static_cast<Stream *>(s->ctx);
        const size_t chunk = n > 1000 ? 1000 : static_cast<size_t>(n);  // hosts may take partial writes
        self->data.insert(self->data.end(), static_cast<const uint8_t *>(buf), static_cast<const uint8_t *>(buf) + chunk);
        return static_cast<int64_t>(chunk);
    }
    static int64_t read(const clap_istream_t *s, void *buf, uint64_t n) {
        auto *self = static_cast<Stream *>(s->ctx);
        const size_t left = self->data.size() - self->at;
        const size_t chunk = std::min<size_t>(left, std::min<uint64_t>(n, 777));
        std::memcpy(buf, self->data.data() + self->at, chunk);
        self->at += chunk;
        return static_cast<int64_t>(chunk);
    }
};

#if defined(_WIN32)
constexpr wchar_t kPanelClass[] = L"OpenMMT8PanelWindow";
bool g_stale_called = false;

LRESULT CALLBACK stale_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    g_stale_called = true;
    return DefWindowProcW(h, m, w, l);
}

/* Creates an instance, opens its panel in a hidden parent window, closes it, destroys it. */
bool open_panel(const clap_plugin_factory_t *factory, const char *id) {
    const clap_plugin_t *p = factory->create_plugin(factory, &g_host, id);
    if (!p || !p->init(p)) return false;
    auto *gui = static_cast<const clap_plugin_gui_t *>(p->get_extension(p, CLAP_EXT_GUI));
    HWND parent = CreateWindowExW(0, L"STATIC", L"host", WS_POPUP, 0, 0, 1000, 440, nullptr, nullptr, nullptr, nullptr);
    bool ok = gui && parent && gui->create(p, CLAP_WINDOW_API_WIN32, false);
    if (ok) {
        clap_window_t w{};
        w.api = CLAP_WINDOW_API_WIN32;
        w.win32 = parent;
        ok = gui->set_parent(p, &w) && gui->show(p);
        MSG msg;
        for (int i = 0; i < 20 && ok; ++i) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
            Sleep(5);
        }
        gui->destroy(p);
    }
    if (parent) DestroyWindow(parent);
    p->destroy(p);
    return ok;
}
#endif

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s plugin.clap\n", argv[0]);
        return 2;
    }
    /* The plugin's memory store, away from any installed plugin's. */
    const std::filesystem::path store = std::filesystem::temp_directory_path() / "mmt8_host_test_store";
    std::filesystem::remove_all(store);
#if defined(_WIN32)
    _putenv_s("MMT8_CLAP_DATA_DIR", store.string().c_str());
#else
    setenv("MMT8_CLAP_DATA_DIR", store.string().c_str(), 1);
#endif
#if defined(_WIN32)
    HMODULE lib = LoadLibraryA(argv[1]);
    auto *entry = lib ? reinterpret_cast<const clap_plugin_entry_t *>(GetProcAddress(lib, "clap_entry")) : nullptr;
#else
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) std::fprintf(stderr, "%s\n", dlerror());
    auto *entry = lib ? static_cast<const clap_plugin_entry_t *>(dlsym(lib, "clap_entry")) : nullptr;
#endif
    CHECK(entry != nullptr, "clap_entry exported");
    if (!entry) return 1;
    CHECK(entry->init(argv[1]), "entry init");
    auto *factory = static_cast<const clap_plugin_factory_t *>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    CHECK(factory && factory->get_plugin_count(factory) == 1, "one plugin in the factory");
    const clap_plugin_descriptor_t *desc = factory->get_plugin_descriptor(factory, 0);
    std::printf("      %s / %s %s\n", desc->id, desc->name, desc->version);

    const clap_plugin_t *plugin = factory->create_plugin(factory, &g_host, desc->id);
    CHECK(plugin && plugin->init(plugin), "create and init");

    auto *ports = static_cast<const clap_plugin_audio_ports_t *>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    auto *notes = static_cast<const clap_plugin_note_ports_t *>(plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    auto *params = static_cast<const clap_plugin_params_t *>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    auto *state = static_cast<const clap_plugin_state_t *>(plugin->get_extension(plugin, CLAP_EXT_STATE));
    CHECK(ports && ports->count(plugin, false) == 2 && ports->count(plugin, true) == 1, "audio ports: click + tape out, tape in");
    CHECK(notes && notes->count(plugin, true) == 1 && notes->count(plugin, false) == 1, "note ports: MIDI in and out");
    CHECK(params && params->count(plugin) == 2, "two parameters");
    CHECK(state != nullptr, "state extension");

    const double sr = 48000.0;
    const uint32_t block = 480;
    CHECK(plugin->activate(plugin, sr, 1, block) && plugin->start_processing(plugin), "activate");

    std::vector<float> l(block), r(block), tape(block), in(block);
    float *main_ch[2] = {l.data(), r.data()};
    float *tape_ch[1] = {tape.data()};
    float *in_ch[1] = {in.data()};
    clap_audio_buffer_t outs[2] = {};
    outs[0].data32 = main_ch;
    outs[0].channel_count = 2;
    outs[1].data32 = tape_ch;
    outs[1].channel_count = 1;
    clap_audio_buffer_t ins[1] = {};
    ins[0].data32 = in_ch;
    ins[0].channel_count = 1;
    EventList in_ev, out_ev;
    clap_input_events_t in_events = {&in_ev, EventList::size, EventList::get};
    clap_output_events_t out_events = {&out_ev, EventList::push};
    clap_event_transport_t tr{};
    tr.header = {sizeof(tr), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
    tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
    tr.tempo = 120.0;
    clap_process_t proc{};
    proc.frames_count = block;
    proc.transport = &tr;
    proc.audio_inputs = ins;
    proc.audio_inputs_count = 1;
    proc.audio_outputs = outs;
    proc.audio_outputs_count = 2;
    proc.in_events = &in_events;
    proc.out_events = &out_events;

    const int blocks = static_cast<int>(10.0 * sr / block);
    const auto t0 = std::chrono::steady_clock::now();
    size_t midi_out = 0;
    double beats = 0.0;
    for (int b = 0; b < blocks; ++b) {
        tr.song_pos_beats = static_cast<clap_beattime>(beats * CLAP_BEATTIME_FACTOR);
        proc.steady_time = static_cast<int64_t>(b) * block;
        out_ev.events.clear();
        plugin->process(plugin, &proc);
        midi_out += out_ev.events.size();
        beats += block / sr * 2.0;
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(secs < 10.0, "10 s of audio in %.2f s (%.1fx realtime)", secs, 10.0 / secs);
    std::printf("      %zu MIDI events out\n", midi_out);

    Stream saved;
    clap_ostream_t os = {&saved, Stream::write};
    CHECK(state->save(plugin, &os), "state save (%zu bytes)", saved.data.size());
    mmt8::ProjectState ps;
    CHECK(mmt8::decode_state(saved.data, ps) && ps.builtin_rom && !ps.builtin_jam8 && ps.ram.size() == 0xFF00,
          "state holds the built-in ROM choice and 0xFF00 bytes of battery RAM");
    {
        mmt8::ProjectState j = ps, back;
        j.builtin_jam8 = true;
        CHECK(mmt8::decode_state(mmt8::encode_state(j), back) && back.builtin_rom && back.builtin_jam8 &&
                  back.ram == ps.ram,
              "the built-in JAM-8 choice survives a state round trip");
    }
    bool ram_used = false;
    for (uint8_t v : ps.ram) ram_used |= v != 0;
    CHECK(ram_used, "the ROM initialized its memory");

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);

    const clap_plugin_t *second = factory->create_plugin(factory, &g_host, desc->id);
    CHECK(second && second->init(second), "second instance");
    auto *state2 = static_cast<const clap_plugin_state_t *>(second->get_extension(second, CLAP_EXT_STATE));
    Stream fresh;
    clap_ostream_t os_fresh = {&fresh, Stream::write};
    state2->save(second, &os_fresh);
    mmt8::ProjectState ps_fresh;
    CHECK(mmt8::decode_state(fresh.data, ps_fresh) && ps_fresh.ram == ps.ram,
          "a new instance gets back the memory the last one left");
    Stream back{saved.data, 0};
    clap_istream_t is = {&back, Stream::read};
    CHECK(state2->load(second, &is), "state load into a new instance");
    Stream again;
    clap_ostream_t os2 = {&again, Stream::write};
    state2->save(second, &os2);
    mmt8::ProjectState ps2;
    CHECK(mmt8::decode_state(again.data, ps2) && ps2.ram == ps.ram, "battery RAM survives the round trip");

    std::vector<uint8_t> legacy = {4, 0, 0, 0, 'n', 'o', 'p', 'e', 0, 0, 0, 0};
    Stream old{legacy, 0};
    clap_istream_t is_old = {&old, Stream::read};
    CHECK(state2->load(second, &is_old), "old-plugin project loads (falls back to the built-in ROM)");
    second->destroy(second);
#if defined(_WIN32)
    /* A DAW removes the plugin, the file is rebuilt and the plugin is added again: the panel
     * must not run through a window class left over from the earlier load. */
    WNDCLASSW stale{};
    stale.lpfnWndProc = stale_wndproc;
    stale.hInstance = reinterpret_cast<HINSTANCE>(lib);
    stale.lpszClassName = kPanelClass;
    RegisterClassW(&stale);
    CHECK(open_panel(factory, desc->id), "panel opens and closes");
    CHECK(!g_stale_called, "panel does not use a window class left from an earlier load");
    entry->deinit();
    FreeLibrary(lib);
    WNDCLASSEXW wc{sizeof(wc)};
    CHECK(!GetClassInfoExW(reinterpret_cast<HINSTANCE>(lib), kPanelClass, &wc), "no window class left after unload");
    lib = LoadLibraryA(argv[1]);
    entry = lib ? reinterpret_cast<const clap_plugin_entry_t *>(GetProcAddress(lib, "clap_entry")) : nullptr;
    CHECK(entry && entry->init(argv[1]), "reload after unload");
    if (entry) {
        factory = static_cast<const clap_plugin_factory_t *>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        CHECK(open_panel(factory, desc->id) && open_panel(factory, desc->id), "panel opens again after reload");
    }
#endif
    if (entry) entry->deinit();
    std::error_code ec;
    std::filesystem::remove_all(store, ec);
    std::printf(g_failures ? "\n%d FAILED\n" : "\nall passed\n", g_failures);
    return g_failures ? 1 : 0;
}
