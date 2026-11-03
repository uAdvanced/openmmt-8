/* Drives the plugin engine like a DAW and a user would, and reads back what the ROM shows.
 * usage: mmt8_clap_engine_test [-v] */
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "engine.h"
#include "panel_layout.h"

namespace {

int g_failures = 0;
bool g_verbose = false;
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

struct Daw {
    mmt8::Engine &e;
    double sr = 48000.0;
    uint32_t block = 512;
    double tempo = 120.0;
    double beats = 0.0;
    bool playing = false;
    std::vector<float> l = std::vector<float>(512), r = std::vector<float>(512), tape = std::vector<float>(512);
    std::vector<std::vector<uint8_t>> midi_out;
    double click_energy = 0.0;

    static uint32_t no_events(const clap_input_events_t *) { return 0; }
    static const clap_event_header_t *no_get(const clap_input_events_t *, uint32_t) { return nullptr; }
    static bool push(const clap_output_events_t *o, const clap_event_header_t *h) {
        auto *self = static_cast<Daw *>(o->ctx);
        if (h->type == CLAP_EVENT_MIDI) {
            const auto *m = reinterpret_cast<const clap_event_midi_t *>(h);
            self->midi_out.emplace_back(m->data, m->data + 3);
        }
        return true;
    }

    void run(double seconds) {
        const int n = static_cast<int>(seconds * sr / block + 0.5);
        for (int i = 0; i < n; ++i) step();
    }

    void step() {
        float *main_ch[2] = {l.data(), r.data()};
        float *tape_ch[1] = {tape.data()};
        clap_audio_buffer_t outs[2] = {};
        outs[0].data32 = main_ch;
        outs[0].channel_count = 2;
        outs[1].data32 = tape_ch;
        outs[1].channel_count = 1;
        clap_input_events_t in = {nullptr, no_events, no_get};
        clap_output_events_t out = {this, push};
        clap_event_transport_t tr{};
        tr.header = {sizeof(tr), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
        tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | (playing ? CLAP_TRANSPORT_IS_PLAYING : 0);
        tr.tempo = tempo;
        tr.song_pos_beats = static_cast<clap_beattime>(beats * CLAP_BEATTIME_FACTOR);
        clap_process_t p{};
        p.frames_count = block;
        p.transport = &tr;
        p.audio_outputs = outs;
        p.audio_outputs_count = 2;
        p.in_events = &in;
        p.out_events = &out;
        e.process(&p);
        for (float v : l) click_energy += v * v;
        if (playing) beats += block / sr * tempo / 60.0;
    }

    std::string lcd() const {
        mmt_emu::PanelView v;
        e.read_panel(v);
        std::string s;
        for (int i = 0; i < 32; ++i) {
            const uint8_t c = v.lcd[i];
            s += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
            if (i == 15) s += '|';
        }
        return s;
    }
    uint8_t modes() const {
        mmt_emu::PanelView v;
        e.read_panel(v);
        return v.led_modes;
    }
    bool play_led() const { return !(modes() & 1); }

    void tap(uint8_t key, double hold = 0.12) {
        e.press(key, true);
        run(hold);
        e.press(key, false);
        run(0.12);
    }
    void show(const char *what) const {
        if (g_verbose) std::printf("      [%s] '%s' leds=%02X beats=%.2f\n", what, lcd().c_str(), modes(), beats);
    }
};

/* The "BEAT 012" counter on the bottom line of the PLAYING screen. */
int lcd_beat(const std::string &s) {
    const size_t at = s.find("BEAT ");
    return at == std::string::npos ? -1 : std::atoi(s.c_str() + at + 5);
}

}  // namespace

int main(int argc, char **argv) {
    g_verbose = argc > 1 && !std::strcmp(argv[1], "-v");
    CHECK(mmt8::Engine::has_builtin_rom(), "OpenMMT-8 is built in");
    mmt8::Engine e;
    e.activate(48000.0, 512);
    Daw daw{e};
    daw.run(3.0);
    daw.show("boot");
    CHECK(daw.lcd().find("PART") != std::string::npos || daw.lcd().find("SONG") != std::string::npos,
          "boots to the part/song screen: '%s'", daw.lcd().c_str());

    /* CLOCK held + 1 selects MIDI & INTERNAL clock. */
    e.press(32, true);
    daw.run(0.2);
    daw.tap(33);
    daw.show("clock");
    CHECK(daw.lcd().find("midi + internal") != std::string::npos, "CLOCK + 1 selects MIDI clock: '%s'", daw.lcd().c_str());
    e.press(32, false);
    daw.run(0.3);

    daw.playing = true;
    daw.beats = 0.0;
    daw.run(0.3);
    daw.show("daw play");
    CHECK(daw.play_led(), "DAW play starts the MMT-8 (PLAY LED on)");
    daw.run(2.0);
    daw.show("daw +2s");
    const int b1 = lcd_beat(daw.lcd());
    daw.run(2.0);
    daw.show("daw +4s");
    const int b2 = lcd_beat(daw.lcd());
    CHECK(b2 - b1 == 4, "MMT-8 advances with DAW clock: beat %d -> %d over 4 DAW beats", b1, b2);

    daw.playing = false;
    daw.run(0.3);
    daw.show("daw stop");
    CHECK(!daw.play_led(), "DAW stop stops the MMT-8 (PLAY LED off)");

    /* Start mid-song: SPP + CONTINUE. Beat 8.0 is 16th 32 -> the MMT-8 continues at beat 9. */
    daw.beats = 8.0;
    daw.run(0.1);
    daw.playing = true;
    daw.run(0.3);
    daw.show("daw play @8");
    CHECK(daw.play_led(), "DAW play from beat 8 continues the MMT-8");
    const int b3 = lcd_beat(daw.lcd());
    CHECK(b3 == 9, "continue lands on beat 9 (1-based): %d", b3);

    /* Loop jump back to 0 while playing. */
    daw.beats = 0.0;
    daw.run(0.3);
    daw.show("daw loop to 0");
    CHECK(daw.play_led() && lcd_beat(daw.lcd()) == 1, "loop jump to the top relocates the MMT-8 to beat 1: '%s'",
          daw.lcd().c_str());
    daw.playing = false;
    daw.run(0.3);

    /* DAW sync off: the host transport no longer reaches the MMT-8. */
    e.set_param_from_gui(mmt8::kParamSync, 0.0);
    daw.playing = true;
    daw.beats = 0.0;
    daw.run(0.5);
    CHECK(!daw.play_led(), "DAW SYNC off: host play is not sent");
    daw.playing = false;
    e.set_param_from_gui(mmt8::kParamSync, 1.0);
    daw.run(0.2);

    /* The footswitch is a pedal on P3.3: the ROM decides what it does. */
    e.press(mmt8::kKeyFootswitch, true);
    daw.run(0.3);
    e.press(mmt8::kKeyFootswitch, false);
    daw.run(0.3);
    daw.show("foot");
    const bool foot_played = daw.play_led();
    std::printf("      footswitch press -> PLAY LED %s\n", foot_played ? "on" : "off");
    if (foot_played) daw.tap(5);

    /* Battery RAM survives save/load: the clock source is still MIDI in a new instance. */
    const std::vector<uint8_t> chunk = e.save_state();
    mmt8::Engine e2;
    e2.activate(48000.0, 512);
    CHECK(e2.load_state(chunk), "state loads into a new instance");
    Daw daw2{e2};
    daw2.run(3.0);
    e2.press(32, true);
    daw2.run(0.3);
    daw2.show("restored clock");
    CHECK(daw2.lcd().find("midi + internal") != std::string::npos, "clock source kept in battery RAM: '%s'",
          daw2.lcd().c_str());
    e2.press(32, false);
    daw2.run(0.2);

    /* A loaded ROM (the stock one) travels inside the project. */
    const char *stock = nullptr;
    for (int i = 1; i < argc; ++i)
        if (argv[i][0] != '-') stock = argv[i];
    if (stock) {
        std::string err;
        CHECK(e2.load_rom_file(stock, err), "load ROM file %s %s", stock, err.c_str());
        daw2.run(3.0);
        daw2.show("stock boot");
        const std::string boot = daw2.lcd();
        CHECK(boot.find("PART") != std::string::npos || boot.find("SONG") != std::string::npos,
              "stock ROM boots: '%s'", boot.c_str());
        const std::vector<uint8_t> c2 = e2.save_state();
        mmt8::ProjectState ps;
        CHECK(mmt8::decode_state(c2, ps) && !ps.builtin_rom && ps.rom.size() >= 0x8000,
              "project embeds the loaded ROM (%zu bytes, '%s')", ps.rom.size(), ps.rom_name.c_str());
        mmt8::Engine e3;
        e3.activate(48000.0, 512);
        CHECK(e3.load_state(c2) && !e3.rom_is_builtin(), "a new instance restores the stock ROM from the project");
        Daw daw3{e3};
        daw3.run(3.0);
        CHECK(daw3.lcd() == boot, "and shows the same screen: '%s'", daw3.lcd().c_str());

        /* The stock ROM defaults to MIDI & INTERNAL clock. */
        daw3.playing = true;
        daw3.beats = 0.0;
        daw3.run(2.3);
        daw3.show("stock daw play");
        const int s1 = lcd_beat(daw3.lcd());
        daw3.run(2.0);
        daw3.show("stock +2s");
        CHECK(daw3.play_led() && lcd_beat(daw3.lcd()) - s1 == 4, "stock ROM follows the DAW clock: beat %d -> %d", s1,
              lcd_beat(daw3.lcd()));
        daw3.playing = false;
        daw3.run(0.3);
        CHECK(!daw3.play_led(), "stock ROM stops with the DAW");
        daw3.beats = 8.0;
        daw3.run(0.1);
        daw3.playing = true;
        daw3.run(0.3);
        daw3.show("stock play @8");
        CHECK(daw3.play_led() && lcd_beat(daw3.lcd()) == 9, "stock ROM continues from SONG POSITION at beat 9: '%s'",
              daw3.lcd().c_str());
        daw3.beats = 0.0;
        daw3.run(0.3);
        daw3.show("stock loop");
        CHECK(daw3.play_led() && lcd_beat(daw3.lcd()) == 1, "stock ROM follows a loop jump to beat 1: '%s'",
              daw3.lcd().c_str());

        /* With a store, each ROM gets its own memory back, and a new instance starts with the
         * ROM last chosen. */
        const std::string dir = (std::filesystem::temp_directory_path() / "mmt8_engine_test_store").string();
        std::filesystem::remove_all(dir);
        {
            mmt8::Engine e4;
            e4.attach_store(dir);
            e4.restore_last();
            e4.activate(48000.0, 512);
            Daw daw4{e4};
            daw4.run(3.0);
            e4.press(32, true);
            daw4.run(0.2);
            daw4.tap(33);
            e4.press(32, false);
            daw4.run(0.3);
            CHECK(e4.load_rom_file(stock, err), "switch to the stock ROM %s", err.c_str());
            daw4.run(3.0);
            CHECK(e4.use_builtin_rom(), "switch back to OpenMMT-8");
            daw4.run(3.0);
            e4.press(32, true);
            daw4.run(0.3);
            daw4.show("builtin again");
            CHECK(daw4.lcd().find("midi + internal") != std::string::npos,
                  "OpenMMT-8 gets its memory back after the stock ROM: '%s'", daw4.lcd().c_str());
            e4.press(32, false);
            daw4.run(0.2);
            const std::vector<uint8_t> c4 = e4.save_state();
            mmt8::ProjectState ps4;
            CHECK(mmt8::decode_state(c4, ps4) && ps4.other_ram.size() == 1,
                  "the project keeps the stock ROM's memory too (%zu other ROMs)", ps4.other_ram.size());
            CHECK(e4.load_rom_file(stock, err), "and the stock ROM again %s", err.c_str());
            daw4.run(0.5);
        }
        mmt8::Engine e5;
        e5.attach_store(dir);
        e5.restore_last();
        const std::string stock_name = std::filesystem::path(stock).filename().string();
        CHECK(!e5.rom_is_builtin() && e5.rom_name() == stock_name, "a new instance starts with the ROM last chosen: '%s'",
              e5.rom_name().c_str());
        std::filesystem::remove_all(dir);
    }

    /* JAM-8, the second built-in ROM: it builds its factory scenes on first boot, plays from
     * its own clock, and keeps its memory apart from OpenMMT-8's. Key ids: PLAY 4, STOP 5. */
    CHECK(mmt8::Engine::has_builtin_rom(mmt8::Builtin::Jam8), "JAM-8 is built in");
    if (mmt8::Engine::has_builtin_rom(mmt8::Builtin::Jam8)) {
        const auto kicks = [](const Daw &d) {
            return std::count_if(d.midi_out.begin(), d.midi_out.end(),
                                 [](const std::vector<uint8_t> &m) { return m[0] == 0x99 && m[1] == 36 && m[2]; });
        };
        mmt8::Engine ej;
        ej.activate(48000.0, 512);
        Daw dj{ej};
        dj.run(1.0);
        CHECK(ej.use_builtin_rom(mmt8::Builtin::Jam8) && ej.rom_builtin() == mmt8::Builtin::Jam8 &&
                  ej.rom_name() == "JAM-8 (built-in)",
              "switch to JAM-8: '%s'", ej.rom_name().c_str());
        dj.run(0.5);
        dj.show("jam8 first boot");
        CHECK(dj.lcd().find("MEMORY CLEARED") != std::string::npos, "first boot builds the factory scenes: '%s'",
              dj.lcd().c_str());
        dj.run(3.5);
        dj.show("jam8 home");
        CHECK(dj.lcd().find("HOUSE    Am   T1") != std::string::npos && dj.lcd().find("S01") != std::string::npos,
              "JAM-8 home screen, scene 1: '%s'", dj.lcd().c_str());
        dj.midi_out.clear();
        dj.tap(4);
        dj.run(2.0);
        dj.show("jam8 play");
        CHECK(dj.play_led() && kicks(dj) >= 3, "PLAY runs the factory groove: %ld kicks in 2 s",
              static_cast<long>(kicks(dj)));
        dj.tap(5);
        dj.midi_out.clear();
        dj.run(1.0);
        CHECK(!dj.play_led() && kicks(dj) == 0, "STOP stops it");

        const std::vector<uint8_t> cj = ej.save_state();
        mmt8::ProjectState psj;
        CHECK(mmt8::decode_state(cj, psj) && psj.builtin_rom && psj.builtin_jam8 && psj.ram.size() == 0xFF00,
              "the project says built-in JAM-8 and keeps its memory");

        mmt8::Engine ej2;
        ej2.activate(48000.0, 512);
        Daw dj2{ej2};
        CHECK(ej2.load_state(cj) && ej2.rom_builtin() == mmt8::Builtin::Jam8, "a new instance restores JAM-8");
        dj2.run(0.5);
        dj2.show("jam8 restored");
        CHECK(dj2.lcd().find("LOOP ROM") != std::string::npos, "with its memory: splash, no factory build: '%s'",
              dj2.lcd().c_str());

        CHECK(ej.use_builtin_rom(mmt8::Builtin::OpenMMT8) && ej.rom_builtin() == mmt8::Builtin::OpenMMT8,
              "switch to OpenMMT-8");
        dj.run(3.0);
        CHECK(dj.lcd().find("PART") != std::string::npos || dj.lcd().find("SONG") != std::string::npos,
              "OpenMMT-8 boots: '%s'", dj.lcd().c_str());
        CHECK(ej.use_builtin_rom(mmt8::Builtin::Jam8), "and back to JAM-8");
        dj.run(0.5);
        CHECK(dj.lcd().find("LOOP ROM") != std::string::npos, "JAM-8 gets its own memory back: '%s'",
              dj.lcd().c_str());
    }

    std::printf(g_failures ? "\n%d FAILED\n" : "\nall passed\n", g_failures);
    return g_failures ? 1 : 0;
}
