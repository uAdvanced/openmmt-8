#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace mmt_emu {

struct HostTransportState {
    bool playing = false;
    bool recording = false;
    double tempo_bpm = 120.0;
    uint64_t song_position_ticks = 0;
};

struct MidiMessage {
    uint32_t frame_offset = 0;
    std::vector<uint8_t> data;
};

enum class ProcessMode {
    Realtime = 0,
    Offline = 1,
};

struct RuntimeTelemetry {
    uint64_t cpu_cycles = 0;
    uint64_t machine_cycle = 0;
    uint64_t peripheral_ticks = 0;
    uint64_t interrupt_events = 0;
    uint64_t interrupt_dispatches = 0;
    uint64_t interrupt_serviced = 0;
    uint64_t watchpoint_emits = 0;
    uint16_t pc = 0;
    uint8_t acc = 0;
    uint8_t psw = 0;
    uint64_t song_tick = 0;
    uint32_t sequencer_events = 0;
    uint32_t pending_out_events = 0;
    uint32_t boot_phase = 0;
    bool panel_playing = false;
    bool panel_record_armed = false;
    uint64_t xdata_writes = 0;
    uint64_t lcd_cmd_writes = 0;
    uint64_t lcd_data_writes = 0;
    uint64_t xram_04ee_writes = 0;
    uint64_t ff1b_writes = 0;
    uint64_t ff1c_writes = 0;
    uint64_t serial_rx_reads = 0;
    uint32_t serial_rx_queue_depth = 0;
    uint64_t serial_irq_asserts = 0;
    uint64_t serial_irq_clears = 0;
    uint64_t xdata_reads = 0;
    uint64_t ff06_reads = 0;
    uint64_t isr_int0_hits = 0;
    uint64_t isr_timer0_hits = 0;
    uint64_t isr_int1_hits = 0;
    uint64_t isr_timer1_hits = 0;
    uint64_t isr_serial_hits = 0;
    uint64_t isr_timer2_hits = 0;
    bool panel_shadow_injection_enabled = true;
    uint8_t ui_mode_shadow = 0;
    uint8_t ui_mode_p2 = 0;
    bool ui_song_mode = false;
    uint8_t iram_2c = 0;
    uint8_t iram_2d = 0;
    uint8_t iram_2f = 0;
    uint8_t xram_04ce = 0;
    uint8_t panel_ff02 = 0;
    uint8_t panel_ff04 = 0;
    uint8_t panel_p3 = 0;
    uint8_t panel_p3_min = 0xff;
    uint8_t panel_p3_max = 0x00;
    uint8_t panel_track_led_latch = 0;
    uint8_t panel_button_led_latch = 0;
    uint8_t panel_track_led_observed = 0;
    uint8_t panel_button_led_observed = 0;
    uint8_t panel_ff04_fb_current = 0;
    uint8_t panel_ff04_fd_current = 0;
    uint8_t panel_ff04_fe_current = 0;
    uint8_t panel_ff04_fb_observed = 0;
    uint8_t panel_ff04_fd_observed = 0;
    uint8_t panel_ff04_fe_observed = 0;
    uint8_t ff0e_clock_source = 0;
    /** P3.5 rising edges detected last block (record click strobe). */
    uint32_t click_edges_last_block = 0;
    int32_t panel_scan_bank = -1;
    std::array<uint8_t, 6> panel_edge_shadow{};
    std::array<uint8_t, 6> panel_raw_shadow{};
    uint16_t last_xread_addr = 0;
    uint16_t last_xread_pc = 0;
    uint16_t last_xdata_addr = 0;
    uint8_t last_xdata_value = 0;
    uint16_t last_xdata_pc = 0;
    std::string lcd_line1;
    std::string lcd_line2;
    uint8_t lcd_addr = 0;     // DDRAM address, where the cursor sits
    uint8_t lcd_display = 0;  // display-control bits: 4 display, 2 cursor, 1 blink
    bool rom_loaded = false;
    bool boot_milestone_reached = false;
};

/** What the front panel shows: cheap to copy, no allocation. */
struct PanelView {
    std::array<uint8_t, 32> lcd{};   // visible HD44780 character codes, 2 rows of 16
    std::array<uint8_t, 64> cgram{}; // user glyphs 0-7, 8 rows of 5 bits each
    uint8_t lcd_cursor = 0xFF;       // visible cell 0-31 of the cursor, 0xFF when off screen
    uint8_t lcd_display = 0;         // display-control bits: 4 display, 2 cursor, 1 blink
    uint8_t led_tracks = 0xFF;       // FF02 latch: track LEDs 1-8, active low
    uint8_t led_modes = 0xFF;        // FF04 latch: PLAY 0, REC 1, PART 2, EDIT 3, SONG 4, ECHO 5, LOOP 6; active low
    bool rom_loaded = false;
};

/** Receives one complete MIDI message sent by the ROM; frame is the sample it finished in. */
using MidiOutSink = void (*)(void *ctx, uint32_t frame, const uint8_t *data, uint32_t size);

class IPluginRuntime {
public:
    virtual ~IPluginRuntime() = default;
    virtual bool load_rom(const std::string &path) = 0;
    /** Loads a ROM image; the unit powers up with empty RAM. name is for diagnostics only. */
    virtual bool load_rom_image(const std::vector<uint8_t> &image, const std::string &name) {
        (void)image;
        (void)name;
        return false;
    }
    virtual void panel_view(PanelView &out) const { out = PanelView{}; }
    /** Allocation-free drain of the ROM's MIDI output. */
    virtual void drain_midi_out(MidiOutSink sink, void *ctx) {
        for (const auto &m : drain_midi_out()) sink(ctx, m.frame_offset, m.data.data(), static_cast<uint32_t>(m.data.size()));
    }
    /** The battery-backed external RAM (0x0000-0xFEFF). */
    virtual std::vector<uint8_t> battery_ram() const { return {}; }
    /** Restores the battery-backed RAM and power-cycles the unit, as after a power cut. */
    virtual bool set_battery_ram(const std::vector<uint8_t> &ram) {
        (void)ram;
        return false;
    }
    virtual std::string rom_hash() const = 0;
    virtual std::string build_id() const = 0;
    virtual void reset_power() = 0;
    virtual void set_transport(const HostTransportState &state) = 0;
    virtual void push_midi_in(const MidiMessage &msg) = 0;
    /** Allocation-free push_midi_in: bytes arrive at the MIDI IN jack starting at frame. */
    virtual void push_midi_bytes(uint32_t frame, const uint8_t *data, uint32_t size) {
        MidiMessage m;
        m.frame_offset = frame;
        m.data.assign(data, data + size);
        push_midi_in(m);
    }
    virtual void panel_press(uint32_t button_id) = 0;
    virtual void panel_set_held(uint32_t button_id, bool held) {
        if (held) {
            panel_press(button_id);
        }
    }
    virtual void set_process_mode(ProcessMode mode) = 0;
    virtual void set_sample_rate(double sample_rate) = 0;
    virtual void set_audio_inputs(const float *left, const float *right, uint32_t frames) = 0;
    virtual void process_block(uint32_t sample_count) = 0;
    virtual void copy_audio_outputs(float *left, float *right, uint32_t frames) const = 0;
    /** 0..1 scale for record click (left channel). Default 1.0. No-op if not supported. */
    virtual void set_click_out_volume(float volume) { (void)volume; }
    /** Request a short test tone on the left channel to verify audio output. No-op if not supported. */
    virtual void trigger_click_test() {}
    virtual std::vector<MidiMessage> drain_midi_out() = 0;
    virtual std::vector<uint8_t> save_state() const = 0;
    virtual bool load_state(const std::vector<uint8_t> &blob) = 0;
    virtual RuntimeTelemetry telemetry() const = 0;
};

}  // namespace mmt_emu
