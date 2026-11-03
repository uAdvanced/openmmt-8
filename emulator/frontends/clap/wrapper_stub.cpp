#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <deque>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" {
#include "emu8051.h"
}

#include "mmt_emu/plugin_port.hpp"

namespace mmt_emu {

namespace {

template <typename T>
void append_raw(std::vector<uint8_t>& out, const T& value) {
    const auto* ptr = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), ptr, ptr + sizeof(T));
}

std::string fnv1a_64_hex(const std::vector<uint8_t>& bytes) {
    uint64_t hash = 1469598103934665603ull;
    for (uint8_t b : bytes) {
        hash ^= static_cast<uint64_t>(b);
        hash *= 1099511628211ull;
    }
    std::ostringstream oss;
    oss << std::hex;
    oss.width(16);
    oss.fill('0');
    oss << hash;
    return oss.str();
}

void wrapper_sfrwrite_sbuf(struct em8051* cpu, uint8_t address) {
    (void)address;
    cpu->serial_out_remaining_bits = 10u;
    cpu->serial_baud_div = 0u;
}

uint8_t midi_data_length(uint8_t status) {
    if ((status & 0x80u) == 0u) return 0u;
    if (status < 0xF0u) {
        switch (status & 0xF0u) {
            case 0xC0u:
            case 0xD0u:
                return 1u;
            default:
                return 2u;
        }
    }
    switch (status) {
        case 0xF1u:
        case 0xF3u:
            return 1u;
        case 0xF2u:
            return 2u;
        default:
            return 0u;
    }
}

/** FIFO that only allocates when it outgrows its capacity (a large sysex dump), never per element. */
template <typename T>
class RingQueue {
public:
    RingQueue() : buf_(1u << 16) {}
    bool empty() const { return head_ == tail_; }
    size_t size() const { return tail_ - head_; }
    const T& front() const { return buf_[head_ & (buf_.size() - 1u)]; }
    void pop_front() { ++head_; }
    void clear() { head_ = tail_ = 0u; }
    void push_back(const T& v) {
        if (size() == buf_.size()) {
            std::vector<T> grown(buf_.size() * 2u);
            for (size_t i = 0; i < size(); ++i) grown[i] = buf_[(head_ + i) & (buf_.size() - 1u)];
            tail_ = size();
            head_ = 0u;
            buf_.swap(grown);
        }
        buf_[tail_++ & (buf_.size() - 1u)] = v;
    }

private:
    std::vector<T> buf_;
    size_t head_ = 0u;
    size_t tail_ = 0u;
};

bool env_flag_enabled(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) return false;
    std::string lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
}

}  // namespace

class ClapWrapperRuntime final : public IPluginRuntime {
public:
    using PanelMask = uint64_t;
    struct ScheduledSerialByte {
        uint64_t due_tick = 0u;
        uint8_t value = 0u;
    };
    enum : uint32_t {
        kBtnPlay = 0, kBtnStop = 1, kBtnRec = 2, kBtnErase = 2, kBtnTap = 3, kBtnPad1 = 4, kBtnPad8 = 11, kBtnSong = 20, kBtnPart = 21,
        kPanelBankCount = 6, kPanelBitsPerBank = 8, kPanelButtonCount = 48
    };

    ClapWrapperRuntime() {
        /* Room for a full sysex dump, so the audio thread does not allocate. */
        out_bytes_.reserve(1u << 17);
        out_msgs_.reserve(4096u);
        midi_tx_message_.reserve(1u << 16);
        audio_out_left_.reserve(1u << 14);
        audio_out_right_.reserve(1u << 14);
    }
    ~ClapWrapperRuntime() override = default;

    bool load_rom(const std::string& rom_path) override {
        std::ifstream in(rom_path, std::ios::binary);
        if (!in) return false;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return load_rom_image(bytes, rom_path);
    }

    bool load_rom_image(const std::vector<uint8_t>& bytes, const std::string& rom_path) override {
        if (bytes.empty()) return false;

        rom_path_ = rom_path;
        rom_hash_ = fnv1a_64_hex(bytes);
        boot_milestone_reached_ = false;
        rom_loaded_ = false;

        code_mem_.assign(65536u, 0u);
        ext_mem_.assign(65536u, 0u);
        upper_mem_.assign(128u, 0u);
        std::copy(bytes.begin(), bytes.begin() + std::min<size_t>(bytes.size(), code_mem_.size()), code_mem_.begin());

        std::memset(&cpu_, 0, sizeof(cpu_));
        cpu_.mCodeMem = code_mem_.data();
        cpu_.mCodeMemMaxIdx = static_cast<uint16_t>(code_mem_.size() - 1u);
        cpu_.mExtData = ext_mem_.data();
        cpu_.mExtDataMaxIdx = static_cast<uint16_t>(ext_mem_.size() - 1u);
        cpu_.mUpperData = upper_mem_.data();
        cpu_.xread = &ClapWrapperRuntime::cpu_xread;
        cpu_.xwrite = &ClapWrapperRuntime::cpu_xwrite;
        box_.owner = this;
        reset(&cpu_, false);
        install_sfr_hooks();

        lcd_reset();
        reset_mmio_state();
        xdata_writes_ = 0;
        lcd_cmd_writes_ = 0;
        lcd_data_writes_ = 0;
        xram_04ee_writes_ = 0;
        ff1b_writes_ = 0;
        ff1c_writes_ = 0;
        last_ri_ = false;
        pending_sbuf_rx_ = false;
        pending_sbuf_rx_value_ = 0;
        sbuf_rx_ = 0;
        uart_rx_schedule_.clear();
        midi_rx_next_due_tick_ = 0u;
        last_serial_out_idx_ = cpu_.serial_out_idx;
        reset_midi_tx_parser();
        last_xdata_addr_ = 0;
        last_xdata_value_ = 0;
        last_xdata_pc_ = 0;
        rom_loaded_ = true;
        telemetry_.rom_loaded = true;
        /* OpenMMT-8: SDCC DSEG base 0x30 (openmmt8_firmware.map: DSEG = 0x0030). .sym offsets are
         * DSEG-relative: _edge_20 @ IRAM 0x30, _raw_26 @ 0x36 (raw_28 @ 0x38). */
        {
            std::string lower = rom_path_;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("openmmt8") != std::string::npos) {
                panel_iram_edge_base_ = 0x30u;
                panel_iram_raw_base_ = 0x36u;
            } else {
                panel_iram_edge_base_ = 0x20u;
                panel_iram_raw_base_ = 0x26u;
            }
            panel_inject_edges_to_iram_ = true;
            panel_apply_iram_panel_shadow_ = true;
        }
        return true;
    }

    std::string rom_hash() const override { return rom_hash_; }
    std::string build_id() const override { return "mmt8_clap_runtime_emu8051_v1"; }

    void reset_power() override {
        transport_ = HostTransportState{};
        process_mode_ = ProcessMode::Realtime;
        frames_processed_ = 0;
        samples_until_next_clock_ = 0.0;
        last_transport_playing_ = false;
        boot_phase_ = 0;
        out_bytes_.clear();
        out_msgs_.clear();
        if (rom_loaded_) reset(&cpu_, false);
        lcd_reset();
        reset_mmio_state();
        xdata_writes_ = 0;
        lcd_cmd_writes_ = 0;
        lcd_data_writes_ = 0;
        xram_04ee_writes_ = 0;
        ff1b_writes_ = 0;
        ff1c_writes_ = 0;
        last_ri_ = false;
        pending_sbuf_rx_ = false;
        pending_sbuf_rx_value_ = 0;
        sbuf_rx_ = 0;
        uart_rx_schedule_.clear();
        midi_rx_next_due_tick_ = 0u;
        last_serial_out_idx_ = cpu_.serial_out_idx;
        reset_midi_tx_parser();
        last_xdata_addr_ = 0;
        last_xdata_value_ = 0;
        last_xdata_pc_ = 0;
        telemetry_.panel_playing = false;
        telemetry_.panel_record_armed = false;
        telemetry_.ui_mode_shadow = cpu_.mLowerData[0x5Cu];
        telemetry_.ui_mode_p2 = cpu_.mSFR[REG_P2];
        telemetry_.ui_song_mode = (cpu_.mLowerData[0x2Fu] & 0x04u) != 0u;
        telemetry_.iram_2c = cpu_.mLowerData[0x2Cu];
        telemetry_.iram_2d = cpu_.mLowerData[0x2Du];
        telemetry_.iram_2f = cpu_.mLowerData[0x2Fu];
        telemetry_.xram_04ce = ext_mem_[0x04CEu];
        telemetry_.panel_ff02 = ff02_panel_out_latch_;
        telemetry_.panel_ff04 = ff04_control_;
        telemetry_.panel_p3 = sfrread_p3(REG_P3);
        telemetry_.panel_p3_min = panel_p3_min_;
        telemetry_.panel_p3_max = panel_p3_max_;
        telemetry_.panel_track_led_latch = panel_track_led_latch_;
        telemetry_.panel_button_led_latch = panel_button_led_latch_;
        telemetry_.panel_track_led_observed = panel_track_led_observed_;
        telemetry_.panel_button_led_observed = panel_button_led_observed_;
        telemetry_.panel_ff04_fb_current = panel_ff04_fb_current_;
        telemetry_.panel_ff04_fd_current = panel_ff04_fd_current_;
        telemetry_.panel_ff04_fe_current = panel_ff04_fe_current_;
        telemetry_.panel_ff04_fb_observed = panel_ff04_fb_observed_;
        telemetry_.panel_ff04_fd_observed = panel_ff04_fd_observed_;
        telemetry_.panel_ff04_fe_observed = panel_ff04_fe_observed_;
        telemetry_.ff0e_clock_source = ff0e_clock_source_;
        telemetry_.panel_shadow_injection_enabled = panel_shadow_injection_enabled_;
        install_sfr_hooks();
    }

    void set_transport(const HostTransportState& state) override { transport_ = state; }
    void push_midi_in(const MidiMessage& msg) override {
        push_midi_bytes(msg.frame_offset, msg.data.data(), static_cast<uint32_t>(msg.data.size()));
    }
    void push_midi_bytes(uint32_t frame, const uint8_t* data, uint32_t size) override {
        const double ticks_per_sample = 1000000.0 / sample_rate_;
        uint64_t due_tick = telemetry_.machine_cycle +
                            static_cast<uint64_t>(static_cast<double>(frame) * ticks_per_sample);
        if (due_tick < midi_rx_next_due_tick_) {
            due_tick = midi_rx_next_due_tick_;
        }
        for (uint32_t i = 0; i < size; ++i) {
            uart_rx_schedule_.push_back(ScheduledSerialByte{due_tick, data[i]});
            due_tick += 320u; // MIDI UART byte time at 31.25 kbps, including framing.
        }
        midi_rx_next_due_tick_ = due_tick;
    }
    void set_audio_inputs(const float* left, const float* right, uint32_t frames) override {
        audio_in_left_ = left;
        audio_in_right_ = right;
        audio_in_frames_ = frames;
    }

    void panel_press(uint32_t button_id) override {
        panel_inject_press(button_id);
    }

    void panel_set_held(uint32_t button_id, bool held) override {
        if (button_id >= kPanelButtonCount) return;
        const PanelMask bit = (PanelMask{1} << button_id);
        const bool was_held = (panel_hold_mask_ & bit) != 0u;
        if (held) {
            panel_hold_mask_ |= bit;
            // A real held button produces both the sustained raw level and the
            // initial press edge that enters the modal UI.
            if (!was_held) {
                panel_inject_press(button_id);
            }
        } else {
            panel_hold_mask_ &= ~bit;
        }
        update_ff04_panel_p1_sample();
    }

    bool debug_set_ffxx(uint16_t address, uint8_t value) {
        switch (address) {
            case 0xFF02u:
            case 0xFF04u:
            case 0xFF06u:
            case 0xFF0Eu:
            case 0xFF0Fu:
            case 0xFF1Au:
            case 0xFF1Bu:
            case 0xFF1Cu:
                xwrite(address, value);
                return true;
            default:
                return false;
        }
    }

    bool debug_serial_rx(uint8_t value) {
        serial_rx_queue_.push_back(value);
        return true;
    }

    bool debug_uart_rx(uint8_t value) {
        uint64_t due_tick = telemetry_.machine_cycle;
        if (due_tick < midi_rx_next_due_tick_) {
            due_tick = midi_rx_next_due_tick_;
        }
        uart_rx_schedule_.push_back(ScheduledSerialByte{due_tick, value});
        midi_rx_next_due_tick_ = due_tick + 320u;
        return true;
    }

    void debug_enable_panel_write_log(bool on) {
        panel_write_log_on_ = on;
        panel_write_log_.clear();
    }
    uint32_t debug_drain_panel_writes(uint32_t* out, uint32_t max_entries) {
        uint32_t n = 0u;
        while (out != nullptr && n < max_entries && !panel_write_log_.empty()) {
            out[n++] = panel_write_log_.front();
            panel_write_log_.pop_front();
        }
        return n;
    }

    uint32_t debug_drain_serial_tx(uint8_t* bytes_out, uint32_t max_bytes) {
        if (bytes_out == nullptr || max_bytes == 0u) return 0u;
        uint32_t written = 0u;
        serial_tx_log_on_ = true;
        while (written < max_bytes && !serial_tx_log_.empty()) {
            bytes_out[written] = serial_tx_log_.front();
            serial_tx_log_.pop_front();
            ++written;
        }
        return written;
    }

    bool debug_set_pc(uint16_t pc) {
        if (!rom_loaded_) return false;
        cpu_.mPC = pc;
        return true;
    }

    bool debug_set_iram(uint8_t address, uint8_t value) {
        if (!rom_loaded_) return false;
        cpu_.mLowerData[address] = value;
        return true;
    }

    bool debug_set_sfr(uint8_t sfr_addr, uint8_t value) {
        if (!rom_loaded_) return false;
        if (sfr_addr < 0x80u) return false;
        cpu_.mSFR[sfr_addr & 0x7Fu] = value;
        return true;
    }

    bool debug_set_xram(uint16_t address, uint8_t value) {
        if (!rom_loaded_) return false;
        xwrite(address, value);
        return true;
    }
    void debug_watch_pc(uint16_t pc) { pc_watch_.push_back(pc); }
    uint32_t debug_drain_pc_hits(uint64_t* cycles, uint16_t* pcs, uint8_t* sps, uint32_t max) {
        uint32_t n = 0;
        while (n < max && n < pc_hits_.size()) {
            cycles[n] = pc_hits_[n].cycle;
            pcs[n] = pc_hits_[n].pc;
            if (sps != nullptr) sps[n] = pc_hits_[n].sp;
            ++n;
        }
        pc_hits_.erase(pc_hits_.begin(), pc_hits_.begin() + n);
        return n;
    }
    bool debug_peek_iram(uint8_t address, uint8_t* value_out) const {
        if (!rom_loaded_ || value_out == nullptr) return false;
        *value_out = cpu_.mLowerData[address];
        return true;
    }
    bool debug_peek_sfr(uint8_t sfr_addr, uint8_t* value_out) const {
        if (!rom_loaded_ || value_out == nullptr) return false;
        if (sfr_addr < 0x80u) return false;
        const uint8_t idx = static_cast<uint8_t>(sfr_addr & 0x7Fu);
        if (cpu_.sfrread[idx] != nullptr) {
            em8051* cpu = const_cast<em8051*>(&cpu_);
            *value_out = cpu_.sfrread[idx](cpu, sfr_addr);
            return true;
        }
        *value_out = cpu_.mSFR[idx];
        return true;
    }
    bool debug_peek_xram(uint16_t address, uint8_t* value_out) const {
        if (!rom_loaded_ || value_out == nullptr) return false;
        *value_out = xread(address);
        return true;
    }
    uint8_t debug_sp_max() const { return sp_max_; }
    void set_process_mode(ProcessMode mode) override { process_mode_ = mode; }
    void set_sample_rate(double sample_rate) override { sample_rate_ = std::max(1.0, sample_rate); }
    void set_click_out_volume(float volume) override {
        click_out_volume_ = std::max(0.0f, std::min(1.0f, volume));
    }
    void trigger_click_test() override {
        click_test_remaining_ = static_cast<uint32_t>(0.5 * sample_rate_ + 0.5);
        if (click_test_remaining_ > 48000u) click_test_remaining_ = 48000u;
        click_test_phase_ = 0.0;
    }
    void copy_audio_outputs(float* left, float* right, uint32_t frames) const override {
        for (uint32_t i = 0; i < frames; ++i) {
            const float l = (i < audio_out_left_.size()) ? audio_out_left_[i] : 0.0f;
            const float r = (i < audio_out_right_.size()) ? audio_out_right_[i] : 0.0f;
            if (left != nullptr) left[i] = l;
            if (right != nullptr) right[i] = r;
        }
    }

    void process_block(uint32_t sample_count) override {
        frames_processed_ += sample_count;
        audio_out_left_.assign(sample_count, 0.0f);
        audio_out_right_.assign(sample_count, 0.0f);
        panel_track_led_observed_ = 0u;
        panel_button_led_observed_ = 0u;
        panel_ff04_fb_seen_this_block_ = false;
        panel_ff04_fd_seen_this_block_ = false;
        panel_ff04_fe_seen_this_block_ = false;
        panel_ff04_fb_saw_nonzero_this_block_ = false;
        panel_ff04_fd_saw_nonzero_this_block_ = false;
        panel_ff04_fe_saw_nonzero_this_block_ = false;
        panel_ff04_fb_observed_ = 0u;
        panel_ff04_fd_observed_ = 0u;
        panel_ff04_fe_observed_ = 0u;
        panel_p3_min_ = 0xff;
        panel_p3_max_ = 0x00;
        if (rom_loaded_) step_cpu(sample_count);

        /* Apply click-out volume and optional test tone (left channel). */
        {
            const double two_pi_f_over_fs = 2.0 * 3.141592653589793 * 440.0 / sample_rate_;
            for (uint32_t i = 0; i < sample_count; ++i) {
                float L = audio_out_left_[i];
                if (click_test_remaining_ > 0u) {
                    L = 0.3f * static_cast<float>(std::sin(click_test_phase_));
                    click_test_phase_ += two_pi_f_over_fs;
                    --click_test_remaining_;
                }
                audio_out_left_[i] = L * click_out_volume_;
            }
        }

        if (panel_ff04_fb_seen_this_block_ && !panel_ff04_fb_saw_nonzero_this_block_) {
            panel_ff04_fb_current_ = 0u;
        }
        if (panel_ff04_fd_seen_this_block_ && !panel_ff04_fd_saw_nonzero_this_block_) {
            panel_ff04_fd_current_ = 0u;
        }
        if (panel_ff04_fe_seen_this_block_ && !panel_ff04_fe_saw_nonzero_this_block_) {
            panel_ff04_fe_current_ = 0u;
        }


        telemetry_.song_tick = 0u;
        telemetry_.sequencer_events = 0u;
        telemetry_.pending_out_events = static_cast<uint32_t>(out_msgs_.size());
        telemetry_.boot_phase = boot_phase_;
        telemetry_.panel_playing = (ff04_control_ & 0x01u) == 0u;
        telemetry_.panel_record_armed = (ff04_control_ & 0x02u) == 0u;
        telemetry_.xdata_writes = xdata_writes_;
        telemetry_.lcd_cmd_writes = lcd_cmd_writes_;
        telemetry_.lcd_data_writes = lcd_data_writes_;
        telemetry_.xram_04ee_writes = xram_04ee_writes_;
        telemetry_.ff1b_writes = ff1b_writes_;
        telemetry_.ff1c_writes = ff1c_writes_;
        telemetry_.serial_rx_reads = serial_rx_reads_;
        telemetry_.serial_rx_queue_depth = static_cast<uint32_t>(serial_rx_queue_.size());
        telemetry_.serial_irq_asserts = serial_irq_asserts_;
        telemetry_.serial_irq_clears = serial_irq_clears_;
        telemetry_.xdata_reads = xdata_reads_;
        telemetry_.ff06_reads = ff06_reads_;
        telemetry_.isr_int0_hits = isr_int0_hits_;
        telemetry_.isr_timer0_hits = isr_timer0_hits_;
        telemetry_.isr_int1_hits = isr_int1_hits_;
        telemetry_.isr_timer1_hits = isr_timer1_hits_;
        telemetry_.isr_serial_hits = isr_serial_hits_;
        telemetry_.isr_timer2_hits = isr_timer2_hits_;
        telemetry_.panel_shadow_injection_enabled = panel_shadow_injection_enabled_;
        telemetry_.ui_mode_shadow = cpu_.mLowerData[0x5Cu];
        telemetry_.ui_mode_p2 = cpu_.mSFR[REG_P2];
        telemetry_.ui_song_mode = (cpu_.mLowerData[0x2Fu] & 0x04u) != 0u;
        telemetry_.iram_2c = cpu_.mLowerData[0x2Cu];
        telemetry_.iram_2d = cpu_.mLowerData[0x2Du];
        telemetry_.iram_2f = cpu_.mLowerData[0x2Fu];
        telemetry_.xram_04ce = ext_mem_[0x04CEu];
        telemetry_.panel_ff02 = ff02_panel_out_latch_;
        telemetry_.panel_ff04 = ff04_control_;
        telemetry_.panel_p3 = sfrread_p3(REG_P3);
        telemetry_.panel_p3_min = panel_p3_min_;
        telemetry_.panel_p3_max = panel_p3_max_;
        telemetry_.panel_track_led_latch = panel_track_led_latch_;
        telemetry_.panel_button_led_latch = panel_button_led_latch_;
        telemetry_.panel_track_led_observed = panel_track_led_observed_;
        telemetry_.panel_button_led_observed = panel_button_led_observed_;
        telemetry_.panel_ff04_fb_current = panel_ff04_fb_current_;
        telemetry_.panel_ff04_fd_current = panel_ff04_fd_current_;
        telemetry_.panel_ff04_fe_current = panel_ff04_fe_current_;
        telemetry_.panel_ff04_fb_observed = panel_ff04_fb_observed_;
        telemetry_.panel_ff04_fd_observed = panel_ff04_fd_observed_;
        telemetry_.panel_ff04_fe_observed = panel_ff04_fe_observed_;
        telemetry_.ff0e_clock_source = ff0e_clock_source_;
        telemetry_.panel_scan_bank = current_panel_scan_bank();
        for (uint16_t bank = 0; bank < kPanelBankCount; ++bank) {
            telemetry_.panel_edge_shadow[bank] = cpu_.mLowerData[panel_iram_edge_base_ + bank];
            telemetry_.panel_raw_shadow[bank] = cpu_.mLowerData[panel_iram_raw_base_ + bank];
        }
        telemetry_.last_xread_addr = last_xread_addr_;
        telemetry_.last_xread_pc = last_xread_pc_;
        telemetry_.last_xdata_addr = last_xdata_addr_;
        telemetry_.last_xdata_value = last_xdata_value_;
        telemetry_.last_xdata_pc = last_xdata_pc_;
    }

    std::vector<MidiMessage> drain_midi_out() override {
        std::vector<MidiMessage> out;
        out.reserve(out_msgs_.size());
        for (const OutMsg& m : out_msgs_)
            out.push_back(MidiMessage{m.frame, std::vector<uint8_t>(out_bytes_.begin() + m.offset,
                                                                    out_bytes_.begin() + m.offset + m.size)});
        out_bytes_.clear();
        out_msgs_.clear();
        return out;
    }

    void drain_midi_out(MidiOutSink sink, void* ctx) override {
        for (const OutMsg& m : out_msgs_) sink(ctx, m.frame, out_bytes_.data() + m.offset, m.size);
        out_bytes_.clear();
        out_msgs_.clear();
    }

    std::vector<uint8_t> save_state() const override {
        std::vector<uint8_t> out;
        const uint32_t version = 4u;
        append_raw(out, version);
        append_raw(out, transport_.playing);
        append_raw(out, transport_.recording);
        append_raw(out, transport_.tempo_bpm);
        append_raw(out, transport_.song_position_ticks);
        append_raw(out, sample_rate_);
        append_raw(out, frames_processed_);
        append_raw(out, static_cast<uint32_t>(process_mode_));
        append_raw(out, samples_until_next_clock_);
        append_raw(out, telemetry_.cpu_cycles);
        append_raw(out, static_cast<uint64_t>(0));
        append_raw(out, cpu_.mPC);
        append_raw(out, cpu_.mLowerData[0x5Cu]);
        return out;
    }

    bool load_state(const std::vector<uint8_t>& blob) override {
        size_t off = 0u;
        auto pull = [&](auto& dst) {
            if (off + sizeof(dst) > blob.size()) return false;
            std::memcpy(&dst, blob.data() + off, sizeof(dst));
            off += sizeof(dst);
            return true;
        };
        uint32_t version = 0u;
        uint32_t mode = 0u;
        uint16_t saved_pc = 0u;
        if (!pull(version) || version < 2u || version > 4u) return false;
        if (!pull(transport_.playing) || !pull(transport_.recording) || !pull(transport_.tempo_bpm) ||
            !pull(transport_.song_position_ticks) || !pull(sample_rate_) || !pull(frames_processed_) || !pull(mode)) return false;
        process_mode_ = (mode == static_cast<uint32_t>(ProcessMode::Offline)) ? ProcessMode::Offline : ProcessMode::Realtime;
        (void)pull(samples_until_next_clock_);
        (void)pull(telemetry_.cpu_cycles);
        uint64_t ignored_song_tick = 0;
        (void)pull(ignored_song_tick);
        if (version >= 3u && pull(saved_pc) && rom_loaded_) cpu_.mPC = saved_pc;
        if (version >= 4u) {
            uint8_t ignored_ui_mode_shadow = 0u;
            (void)pull(ignored_ui_mode_shadow);
        }
        return true;
    }

    RuntimeTelemetry telemetry() const override {
        RuntimeTelemetry t = telemetry_;
        t.lcd_line1 = lcd_line(lcd_ddram_, 0x00u, lcd_display_shift_);
        t.lcd_line2 = lcd_line(lcd_ddram_, 0x40u, lcd_display_shift_);
        t.rom_loaded = rom_loaded_;
        t.boot_milestone_reached = boot_milestone_reached_;
        return t;
    }

    void panel_view(PanelView& out) const override {
        for (int row = 0; row < 2; ++row)
            for (int i = 0; i < 16; ++i)
                out.lcd[static_cast<size_t>(row * 16 + i)] = static_cast<uint8_t>(
                    lcd_ddram_[static_cast<size_t>(row * 0x28 + (lcd_display_shift_ + i) % 0x28)]);
        out.cgram = lcd_cgram_;
        out.lcd_cursor = 0xFFu;
        const int idx = lcd_ddram_index(lcd_addr_);
        if (idx >= 0) {
            const int row = idx / 0x28;
            const int col = (idx % 0x28 - lcd_display_shift_ + 0x28) % 0x28;
            if (col < 16) out.lcd_cursor = static_cast<uint8_t>(row * 16 + col);
        }
        out.lcd_display = lcd_display_;
        out.led_tracks = ff02_panel_out_latch_;
        out.led_modes = ff04_control_;
        out.rom_loaded = rom_loaded_;
    }

    static constexpr size_t kBatteryRamBytes = 0xFF00u;

    std::vector<uint8_t> battery_ram() const override {
        if (ext_mem_.size() < kBatteryRamBytes) return {};
        return std::vector<uint8_t>(ext_mem_.begin(), ext_mem_.begin() + kBatteryRamBytes);
    }

    bool set_battery_ram(const std::vector<uint8_t>& ram) override {
        if (!rom_loaded_ || ram.size() != kBatteryRamBytes) return false;
        std::copy(ram.begin(), ram.end(), ext_mem_.begin());
        std::memset(cpu_.mLowerData, 0, sizeof(cpu_.mLowerData));
        reset_power();
        return true;
    }

private:
    /* The CPU's callbacks find their runtime through the box the CPU sits at the start of. */
    struct CpuBox {
        struct em8051 cpu;
        ClapWrapperRuntime* owner;
    };
    static ClapWrapperRuntime* owner_of(struct em8051* cpu) { return reinterpret_cast<CpuBox*>(cpu)->owner; }

    static uint8_t cpu_xread(struct em8051* cpu, uint16_t address) { return owner_of(cpu)->xread(address); }
    static void cpu_xwrite(struct em8051* cpu, uint16_t address, uint8_t value) {
        owner_of(cpu)->xwrite(address, value);
    }
    static uint8_t cpu_sfrread_p3(struct em8051* cpu, uint8_t address) { return owner_of(cpu)->sfrread_p3(address); }
    /* SBUF is two registers on the 8051: reads return the receive buffer, writes load the
     * transmitter (which shifts out of mSFR[REG_SBUF]). */
    static uint8_t cpu_sfrread_sbuf(struct em8051* cpu, uint8_t) { return owner_of(cpu)->sbuf_rx_; }
    static uint8_t cpu_sfrread_p1(struct em8051* cpu, uint8_t) { return owner_of(cpu)->sfrread_p1(); }

    void install_sfr_hooks() {
        cpu_.sfrwrite[REG_SBUF] = &wrapper_sfrwrite_sbuf;
        cpu_.sfrread[REG_SBUF] = &ClapWrapperRuntime::cpu_sfrread_sbuf;
        cpu_.sfrread[REG_P3] = &ClapWrapperRuntime::cpu_sfrread_p3;
        cpu_.sfrread[REG_P1] = &ClapWrapperRuntime::cpu_sfrread_p1;
    }

    uint8_t xread(uint16_t address) const {
        ++xdata_reads_;
        last_xread_addr_ = address;
        last_xread_pc_ = cpu_.mPC;
        switch (address) {
            case 0xFF02u: return ff02_panel_out_latch_;
            /* The ROM tests FF04 bit 3 after setting it and updates its LED bits by read-modify-write,
             * so FF04 reads back the written latch; keys come in on P1. */
            case 0xFF04u: return panel_real_matrix_ ? ff04_control_ : ff04_panel_p1_sample_;
            case 0xFF06u:
                ++ff06_reads_;
                if (!serial_rx_queue_.empty()) {
                    const uint8_t v = serial_rx_queue_.front();
                    serial_rx_queue_.pop_front();
                    ++serial_rx_reads_;
                    ff06_serial_tape_ = v;
                    return v;
                }
                return ff06_serial_tape_;
            case 0xFF08u: return ff08_lcd_cmd_;
            case 0xFF09u: return ff09_lcd_data_;
            case 0xFF0Eu: return ff0e_clock_source_;
            case 0xFF0Fu: return ff0f_realtime_tx_gate_;
            case 0xFF1Au: return ff1a_autostart_;
            case 0xFF1Bu: return ff1b_callback_hi_;
            case 0xFF1Cu: return ff1c_callback_lo_;
            default: return ext_mem_[address];
        }
    }

    uint8_t sfrread_p1() const {
        const uint8_t latch = cpu_.mSFR[REG_P1];
        if (!panel_real_matrix_) return latch;
        return static_cast<uint8_t>(latch & ff04_panel_p1_sample_);
    }

    uint8_t sfrread_p3(uint8_t address) const {
        (void)address;
        const uint8_t latch = cpu_.mSFR[REG_P3];
        /* Quasi-bidirectional: masked pins follow external tape/audio, not latched P3. */
        return static_cast<uint8_t>((latch & static_cast<uint8_t>(~external_p3_mask_)) |
                                    (external_p3_value_ & external_p3_mask_));
    }

    void xwrite(uint16_t address, uint8_t value) {
        ext_mem_[address] = value;
        ++xdata_writes_;
        if (address == 0x04EEu) ++xram_04ee_writes_;
        last_xdata_addr_ = address;
        last_xdata_value_ = value;
        last_xdata_pc_ = cpu_.mPC;
        if (panel_write_log_on_ && (address == 0xFF02u || address == 0xFF04u || address == 0xFF06u) &&
            panel_write_log_.size() < 65536u) {
            /* reg low nibble << 28 | pc << 8 | value */
            panel_write_log_.push_back((static_cast<uint32_t>(address & 0x0Fu) << 28) |
                                       (static_cast<uint32_t>(cpu_.mPC) << 8) | value);
        }
        switch (address) {
            case 0xFF02u:
                ff02_panel_out_latch_ = value;
                if ((ff04_control_ & 0x08u) != 0u) {
                    panel_track_led_latch_ = value;
                } else {
                    panel_button_led_latch_ = value;
                }
                record_panel_output_sample(ff02_panel_out_latch_, ff04_control_);
                update_ff04_panel_p1_sample();
                break;
            case 0xFF04u:
                if (((ff04_control_ ^ value) & 0x08u) != 0u && (value & 0x08u) != 0u) {
                    panel_track_led_latch_ = ff02_panel_out_latch_;
                }
                ff04_control_ = value;
                record_panel_output_sample(ff02_panel_out_latch_, ff04_control_);
                break;
            case 0xFF06u:
                ff06_control_ = value;
                ff06_scan_bank_ = decode_scan_bank(value);
                update_ff04_panel_p1_sample();
                break;
            case 0xFF08u:
                ff08_lcd_cmd_ = value;
                ++lcd_cmd_writes_;
                lcd_cmd(value);
                break;
            case 0xFF09u:
                ff09_lcd_data_ = value;
                ++lcd_data_writes_;
                lcd_data(value);
                break;
            case 0xFF0Eu:
                ff0e_clock_source_ = value;
                break;
            case 0xFF0Fu:
                ff0f_realtime_tx_gate_ = value;
                break;
            case 0xFF1Au:
                ff1a_autostart_ = value;
                break;
            case 0xFF1Bu:
                ff1b_callback_hi_ = value;
                ++ff1b_writes_;
                break;
            case 0xFF1Cu:
                ff1c_callback_lo_ = value;
                ++ff1c_writes_;
                break;
            case 0x04EEu:
                // Deferred callback countdown: firmware writes a countdown value here
                // to arm the callback installed via ff1b/ffc.
                // countdown=0 wraps to 0xFF (fires after 256 ticks).
                deferred_countdown_ = value;
                deferred_active_ = true;
                deferred_fired_ = false;
                break;
            default:
                break;
        }
    }

    void reset_mmio_state() {
        ff02_panel_out_latch_ = 0u;
        ff04_control_ = 0u;
        panel_track_led_latch_ = 0u;
        panel_button_led_latch_ = 0u;
        panel_track_led_observed_ = 0u;
        panel_button_led_observed_ = 0u;
        panel_ff04_fb_current_ = 0u;
        panel_ff04_fd_current_ = 0u;
        panel_ff04_fe_current_ = 0u;
        panel_ff04_fb_seen_this_block_ = false;
        panel_ff04_fd_seen_this_block_ = false;
        panel_ff04_fe_seen_this_block_ = false;
        panel_ff04_fb_saw_nonzero_this_block_ = false;
        panel_ff04_fd_saw_nonzero_this_block_ = false;
        panel_ff04_fe_saw_nonzero_this_block_ = false;
        panel_ff04_fb_observed_ = 0u;
        panel_ff04_fd_observed_ = 0u;
        panel_ff04_fe_observed_ = 0u;
        ff06_control_ = 0u;
        ff06_serial_tape_ = 0u;
        ff08_lcd_cmd_ = 0u;
        ff09_lcd_data_ = 0u;
        ff0e_clock_source_ = 0u;
        ff0f_realtime_tx_gate_ = 1u;
        ff1a_autostart_ = 0u;
        ff1b_callback_hi_ = 0u;
        ff1c_callback_lo_ = 0u;
        deferred_countdown_ = 0u;
        deferred_active_ = false;
        deferred_fired_ = false;
        ff04_panel_p1_sample_ = 0xFFu;
        cpu_.mSFR[REG_P1] = ff04_panel_p1_sample_;
        ff06_scan_bank_ = -1;
        panel_press_mask_ = 0u;
        panel_edge_mask_ = 0u;
        panel_hold_mask_ = 0u;
        panel_press_ticks_.fill(0u);
        panel_edge_ticks_.fill(0u);
        serial_rx_queue_.clear();
        serial_tx_log_.clear();
        serial_rx_reads_ = 0;
        serial_irq_asserts_ = 0;
        serial_irq_clears_ = 0;
        xdata_reads_ = 0;
        ff06_reads_ = 0;
        isr_int0_hits_ = 0;
        isr_timer0_hits_ = 0;
        isr_int1_hits_ = 0;
        isr_timer1_hits_ = 0;
        isr_serial_hits_ = 0;
        isr_timer2_hits_ = 0;
        panel_real_matrix_ = !env_flag_enabled("MMT8_WRAPPER_LEGACY_PANEL");
        legacy_deferred_timer_ = env_flag_enabled("MMT8_WRAPPER_LEGACY_DEFERRED_TIMER");
        panel_shadow_injection_enabled_ =
            !panel_real_matrix_ && !env_flag_enabled("MMT8_WRAPPER_NO_PANEL_SHADOW_INJECTION");
        last_xread_addr_ = 0;
        last_xread_pc_ = 0;
        external_p3_mask_ = panel_real_matrix_ ? 0x0Cu : 0x04u;
        external_p3_value_ = panel_real_matrix_ ? 0x08u : 0u;
    }

    static int decode_scan_bank(uint8_t value) {
        static constexpr std::array<uint8_t, kPanelBankCount> kScanSelect = {0xFEu, 0xFDu, 0xFBu, 0xF7u, 0xEFu, 0xDFu};
        for (int i = 0; i < static_cast<int>(kScanSelect.size()); ++i) {
            if (value == kScanSelect[static_cast<size_t>(i)]) return i;
        }
        if (value == 0xFFu) return -1;
        return -1;
    }

    int current_panel_scan_bank() const {
        if (ff06_scan_bank_ >= 0 && ff06_scan_bank_ < static_cast<int>(kPanelBankCount)) {
            return ff06_scan_bank_;
        }
        // Record hold path behaved correctly before fallback; prefer legacy behavior there.
        if ((panel_hold_mask_ & (PanelMask{1} << 7u)) != 0u) {
            return -1;
        }
        // FF06 frequently carries serial/tape values while FF02 low bits still select
        // the currently sampled panel bank.
        return static_cast<int>(ff02_panel_out_latch_ & 0x07u) % static_cast<int>(kPanelBankCount);
    }

    void record_panel_output_sample(uint8_t ff02_value, uint8_t ff04_value) {
        const uint8_t active_high = static_cast<uint8_t>(~ff02_value);
        switch (ff04_value) {
            case 0xFBu:
                panel_ff04_fb_seen_this_block_ = true;
                if (active_high != 0u) {
                    panel_ff04_fb_current_ = active_high;
                    panel_ff04_fb_saw_nonzero_this_block_ = true;
                }
                panel_ff04_fb_observed_ |= active_high;
                break;
            case 0xFDu:
                panel_ff04_fd_seen_this_block_ = true;
                if (active_high != 0u) {
                    panel_ff04_fd_current_ = active_high;
                    panel_ff04_fd_saw_nonzero_this_block_ = true;
                }
                panel_ff04_fd_observed_ |= active_high;
                break;
            case 0xFEu:
                panel_ff04_fe_seen_this_block_ = true;
                if (active_high != 0u) {
                    panel_ff04_fe_current_ = active_high;
                    panel_ff04_fe_saw_nonzero_this_block_ = true;
                }
                panel_ff04_fe_observed_ |= active_high;
                break;
            default:
                break;
        }
        if (panel_output_is_track_write(cpu_.mPC, last_xread_addr_)) {
            panel_track_led_observed_ |= active_high;
        } else {
            panel_button_led_observed_ |= active_high;
        }
    }

    static bool panel_output_is_track_write(uint16_t pc, uint16_t last_xread_addr) {
        switch (pc) {
            case 0x2E2Bu:
            case 0x4794u:
            case 0x5CF3u:
            case 0x5D09u:
                return true;
            default:
                break;
        }
        return last_xread_addr == 0x04CEu || last_xread_addr == 0x04DAu;
    }

    uint8_t panel_active_high_sample_for_bank(PanelMask mask, int bank) const {
        if (bank < 0 || bank >= static_cast<int>(kPanelBankCount)) return 0u;
        const uint32_t shift = static_cast<uint32_t>(bank) * kPanelBitsPerBank;
        return static_cast<uint8_t>((mask >> shift) & 0xFFu);
    }

    static bool is_track_button_id(uint32_t button_id) {
        return button_id >= 8u && button_id <= 15u;
    }

    void update_ff04_panel_p1_sample() {
        const PanelMask down = panel_press_mask_ | panel_hold_mask_;
        uint8_t active_high = 0u;
        if (panel_real_matrix_) {
            // FF06 bits 0..5 drive the six key rows active-low; any number of rows may be
            // selected at once (the ROM idle check writes 0x80). P1 reads the columns active-low.
            // The footswitch is wired to P3.3 (read into raw 0x28.3), not to the matrix.
            const PanelMask matrix_down = down & ~(PanelMask{1} << kPanelFootswitchId);
            const uint8_t rows = static_cast<uint8_t>(~ff06_control_ & 0x3Fu);
            for (int bank = 0; bank < static_cast<int>(kPanelBankCount); ++bank) {
                if ((rows & (1u << bank)) != 0u) {
                    active_high |= panel_active_high_sample_for_bank(matrix_down, bank);
                }
            }
            if ((down & (PanelMask{1} << kPanelFootswitchId)) != 0u) {
                external_p3_value_ = static_cast<uint8_t>(external_p3_value_ & ~0x08u);
            } else {
                external_p3_value_ = static_cast<uint8_t>(external_p3_value_ | 0x08u);
            }
        } else {
            const int bank = current_panel_scan_bank();
            active_high = panel_active_high_sample_for_bank(down, bank);
        }
        ff04_panel_p1_sample_ = static_cast<uint8_t>(~active_high);
        if (!panel_real_matrix_) cpu_.mSFR[REG_P1] = ff04_panel_p1_sample_;
    }

    void panel_inject_press(uint32_t button_id) {
        if (button_id >= kPanelButtonCount) return;
        panel_press_mask_ |= (PanelMask{1} << button_id);
        panel_edge_mask_ |= (PanelMask{1} << button_id);
        panel_edge_ticks_[button_id] =
            is_track_button_id(button_id) ? kPanelEdgePulseTicks : kPanelPressPulseTicks;
        if (panel_real_matrix_) {
            panel_press_ticks_[button_id] = kPanelRealPressTicks;
        } else {
            panel_press_ticks_[button_id] = is_track_button_id(button_id)
                ? kPanelTrackPressPulseTicks
                : kPanelPressPulseTicks;
        }
        update_ff04_panel_p1_sample();
    }

    void tick_panel_input_decay() {
        bool changed = false;
        for (uint32_t i = 0; i < panel_press_ticks_.size(); ++i) {
            if (panel_press_ticks_[i] == 0u) continue;
            --panel_press_ticks_[i];
            if (panel_press_ticks_[i] == 0u) {
                panel_press_mask_ &= ~(PanelMask{1} << i);
                changed = true;
            }
        }
        for (uint32_t i = 0; i < panel_edge_ticks_.size(); ++i) {
            if (panel_edge_ticks_[i] == 0u) continue;
            --panel_edge_ticks_[i];
            if (panel_edge_ticks_[i] == 0u) {
                panel_edge_mask_ &= ~(PanelMask{1} << i);
            }
        }
        if (changed) update_ff04_panel_p1_sample();
    }

    void apply_panel_edge_injection() {
        for (uint16_t bank = 0; bank < kPanelBankCount; ++bank) {
            const uint8_t raw_sample = panel_active_high_sample_for_bank(
                panel_press_mask_ | panel_hold_mask_, static_cast<int>(bank));
            const uint16_t raw_addr = static_cast<uint16_t>(panel_iram_raw_base_ + bank);
            cpu_.mLowerData[raw_addr] = raw_sample;
            if (panel_inject_edges_to_iram_) {
                const uint8_t edge_sample = panel_active_high_sample_for_bank(panel_edge_mask_, static_cast<int>(bank));
                const uint16_t edge_addr = static_cast<uint16_t>(panel_iram_edge_base_ + bank);
                uint8_t merged = static_cast<uint8_t>(cpu_.mLowerData[edge_addr] | edge_sample);
                /* Bank 2 bit 6 = PAGE (MMT-8 + OpenMMT-8). Host ORs edge pulses into idata every tick;
                 * without this, PAGE edge can stick while raw_sample shows PAGE released, which pegs
                 * OpenMMT-8 page_down_hold_ticks and breaks erase/copy second PAGE->bend scenarios. */
                if (bank == 2u && (raw_sample & 0x40u) == 0u) {
                    merged = static_cast<uint8_t>(merged & static_cast<uint8_t>(~0x40u));
                }
                cpu_.mLowerData[edge_addr] = merged;
            }
        }
    }

    void lcd_reset() {
        lcd_ddram_.fill(' ');
        lcd_cgram_.fill(0u);
        lcd_addr_ = 0u;
        lcd_cgram_addr_ = 0u;
        lcd_addr_is_cgram_ = false;
        lcd_display_shift_ = 0u;
        lcd_display_ = 0u;
        lcd_entry_inc_ = true;
        lcd_entry_shift_ = false;
    }

    void lcd_cmd(uint8_t value) {
        if (value == 0x01u) {
            lcd_ddram_.fill(' ');
            lcd_addr_ = 0u;
            lcd_addr_is_cgram_ = false;
            lcd_display_shift_ = 0u;
            return;
        }
        if (value == 0x02u) {
            lcd_addr_ = 0u;
            lcd_addr_is_cgram_ = false;
            lcd_display_shift_ = 0u;
            return;
        }
        if ((value & 0xFCu) == 0x04u) {
            lcd_entry_inc_ = (value & 0x02u) != 0u;
            lcd_entry_shift_ = (value & 0x01u) != 0u;
            return;
        }
        if ((value & 0xF8u) == 0x08u) {
            // Display/cursor/blink control does not affect DDRAM contents.
            lcd_display_ = static_cast<uint8_t>(value & 0x07u);
            return;
        }
        if ((value & 0x80u) != 0u) {
            lcd_addr_ = static_cast<uint8_t>(value & 0x7Fu);
            lcd_addr_is_cgram_ = false;
            return;
        }
        if ((value & 0x40u) != 0u) {
            lcd_cgram_addr_ = static_cast<uint8_t>(value & 0x3Fu);
            lcd_addr_is_cgram_ = true;
            return;
        }
        if ((value & 0xF0u) == 0x10u) {
            const bool shift_display = (value & 0x08u) != 0u;
            const bool shift_right = (value & 0x04u) != 0u;
            if (shift_display) {
                lcd_display_shift_ = static_cast<uint8_t>(
                    (lcd_display_shift_ + (shift_right ? static_cast<uint8_t>(0x27u) : 1u)) % 0x28u);
            } else if (lcd_addr_is_cgram_) {
                lcd_cgram_addr_ = static_cast<uint8_t>(
                    (lcd_cgram_addr_ + (shift_right ? 1u : static_cast<uint8_t>(-1))) & 0x3Fu);
            } else {
                lcd_addr_ = static_cast<uint8_t>(lcd_addr_ + (shift_right ? 1u : static_cast<uint8_t>(-1)));
            }
            return;
        }
        if ((value & 0xE0u) == 0x20u) {
            // Function set: bus width, rows, and font configuration.
            return;
        }
    }

    void lcd_data(uint8_t value) {
        if (lcd_addr_is_cgram_) {
            if (lcd_cgram_addr_ < lcd_cgram_.size()) lcd_cgram_[lcd_cgram_addr_] = static_cast<uint8_t>(value & 0x1Fu);
            lcd_cgram_addr_ =
                static_cast<uint8_t>((lcd_cgram_addr_ + (lcd_entry_inc_ ? 1u : static_cast<uint8_t>(-1))) & 0x3Fu);
            return;
        }
        const int ddram_index = lcd_ddram_index(lcd_addr_);
        if (ddram_index >= 0) {
            lcd_ddram_[static_cast<size_t>(ddram_index)] = static_cast<char>(value);
        }
        lcd_addr_ = static_cast<uint8_t>(lcd_addr_ + (lcd_entry_inc_ ? 1u : static_cast<uint8_t>(-1)));
        if (lcd_entry_shift_) {
            lcd_display_shift_ = static_cast<uint8_t>(
                (lcd_display_shift_ + (lcd_entry_inc_ ? 1u : static_cast<uint8_t>(0x27u))) % 0x28u);
        }
    }

    bool lcd_alive() const {
        for (int row = 0; row < 2; ++row)
            for (int i = 0; i < 16; ++i) {
                const uint8_t c = static_cast<uint8_t>(lcd_ddram_[static_cast<size_t>(row * 0x28 + (lcd_display_shift_ + i) % 0x28)]);
                if (c != ' ' && c >= 0x08u) return true;
            }
        return false;
    }

    static int lcd_ddram_index(uint8_t addr) {
        addr &= 0x7Fu;
        if (addr < 0x28u) {
            return static_cast<int>(addr);
        }
        if (addr >= 0x40u && addr < 0x68u) {
            return static_cast<int>(0x28u + (addr - 0x40u));
        }
        return -1;
    }

    static char lcd_decode_data(uint8_t value) {
        if (value < 0x08u) {
            // The frontend does not yet render custom CGRAM glyphs, so keep them blank.
            return ' ';
        }
        return std::isprint(value) ? static_cast<char>(value) : ' ';
    }

    static std::string lcd_line(const std::array<char, 80>& ddram, uint8_t base, uint8_t display_shift) {
        std::string out(16, ' ');
        const int row_base = lcd_ddram_index(base);
        if (row_base < 0) {
            return out;
        }
        const uint8_t row_shift = static_cast<uint8_t>(display_shift % 0x28u);
        for (size_t i = 0; i < out.size(); ++i) {
            const size_t row_offset = static_cast<size_t>((row_shift + static_cast<uint8_t>(i)) % 0x28u);
            out[i] = lcd_decode_data(static_cast<uint8_t>(ddram[static_cast<size_t>(row_base) + row_offset]));
        }
        return out;
    }

    void reset_midi_tx_parser() {
        midi_tx_running_status_ = 0u;
        midi_tx_expected_data_bytes_ = 0u;
        midi_tx_sysex_active_ = false;
        midi_tx_message_.clear();
    }

    void out_push(const uint8_t* data, size_t size) {
        out_msgs_.push_back(OutMsg{cur_frame_, static_cast<uint32_t>(out_bytes_.size()), static_cast<uint32_t>(size)});
        out_bytes_.insert(out_bytes_.end(), data, data + size);
    }

    void emit_midi_tx_message() {
        if (midi_tx_message_.empty()) return;
        out_push(midi_tx_message_.data(), midi_tx_message_.size());
        midi_tx_message_.clear();
        midi_tx_expected_data_bytes_ = 0u;
    }

    void push_midi_tx_byte(uint8_t value) {
        if (value >= 0xF8u) {
            out_push(&value, 1u);
            return;
        }

        if (midi_tx_sysex_active_) {
            if ((value & 0x80u) == 0u) {
                midi_tx_message_.push_back(value);
                return;
            }
            midi_tx_sysex_active_ = false;
            midi_tx_message_.push_back(0xF7u);
            emit_midi_tx_message();
            if (value == 0xF7u) return;
        }

        if ((value & 0x80u) != 0u) {
            midi_tx_message_.clear();
            midi_tx_expected_data_bytes_ = 0u;

            if (value == 0xF0u) {
                midi_tx_running_status_ = 0u;
                midi_tx_sysex_active_ = true;
                midi_tx_message_.push_back(value);
                return;
            }

            midi_tx_message_.push_back(value);
            midi_tx_expected_data_bytes_ = midi_data_length(value);
            midi_tx_running_status_ = (value < 0xF0u) ? value : 0u;
            if (midi_tx_expected_data_bytes_ == 0u) {
                emit_midi_tx_message();
            }
            return;
        }

        if (midi_tx_message_.empty()) {
            if (midi_tx_running_status_ == 0u) return;
            midi_tx_message_.push_back(midi_tx_running_status_);
            midi_tx_expected_data_bytes_ = midi_data_length(midi_tx_running_status_);
        }

        midi_tx_message_.push_back(value);
        if (midi_tx_message_.size() >= static_cast<size_t>(midi_tx_expected_data_bytes_ + 1u)) {
            emit_midi_tx_message();
        }
    }

    void drain_completed_uart_tx() {
        const uint8_t current_idx = cpu_.serial_out_idx;
        while (last_serial_out_idx_ != current_idx) {
            const uint8_t value =
                static_cast<uint8_t>(cpu_.serial_out[last_serial_out_idx_ % sizeof(cpu_.serial_out)]);
            last_serial_out_idx_ = static_cast<uint8_t>((last_serial_out_idx_ + 1u) % sizeof(cpu_.serial_out));
            if (serial_tx_log_on_) serial_tx_log_.push_back(value);
            push_midi_tx_byte(value);
        }
    }

    float input_sample(const float* buffer, uint32_t frame) const {
        return (buffer != nullptr && frame < audio_in_frames_) ? buffer[frame] : 0.0f;
    }

    void update_tape_input_for_sample(uint32_t sample_index) {
        const float mono = 0.5f * (input_sample(audio_in_left_, sample_index) + input_sample(audio_in_right_, sample_index));
        const uint8_t prev_pin = sfrread_p3(REG_P3);
        external_p3_value_ = static_cast<uint8_t>((external_p3_value_ & ~0x04u) | ((mono >= 0.0f) ? 0x04u : 0u));
        const uint8_t pin = sfrread_p3(REG_P3);
        /* P3.2 is INT0. A falling edge sets IE0; the core clears it when the edge interrupt is taken. */
        if ((prev_pin & 0x04u) != 0u && (pin & 0x04u) == 0u)
            cpu_.mSFR[REG_TCON] = static_cast<uint8_t>(cpu_.mSFR[REG_TCON] | 0x02u);
    }


    void step_cpu(uint32_t sample_count) {
        const double ticks_per_sample = 1000000.0 / sample_rate_;
        uint32_t click_edges_total = 0u;
        for (uint32_t sample = 0; sample < sample_count; ++sample) {
            cur_frame_ = sample;
            update_tape_input_for_sample(sample);
            cpu_tick_carry_ += ticks_per_sample;
            uint32_t ticks_this_sample = static_cast<uint32_t>(cpu_tick_carry_);
            if (ticks_this_sample == 0u) {
                ticks_this_sample = 1u;
            } else {
                cpu_tick_carry_ -= static_cast<double>(ticks_this_sample);
            }

            float tape_acc = 0.0f;
            uint32_t tape_ticks = 0u;
            for (uint32_t i = 0; i < ticks_this_sample; ++i) {
                switch (cpu_.mPC) {
                    case 0x0003u: ++isr_int0_hits_; break;
                    case 0x000Bu: ++isr_timer0_hits_; break;
                    case 0x0013u: ++isr_int1_hits_; break;
                    case 0x001Bu: ++isr_timer1_hits_; break;
                    case 0x0023u: ++isr_serial_hits_; break;
                    case 0x002Bu: ++isr_timer2_hits_; break;
                    default: break;
                }
                const bool ri = (cpu_.mSFR[REG_SCON] & SCONMASK_RI) != 0u;
                if (!pending_sbuf_rx_ && !uart_rx_schedule_.empty() &&
                    telemetry_.machine_cycle >= uart_rx_schedule_.front().due_tick && !ri &&
                    (cpu_.mSFR[REG_SCON] & SCONMASK_REN) != 0u) {
                    pending_sbuf_rx_value_ = uart_rx_schedule_.front().value;
                    uart_rx_schedule_.pop_front();
                    sbuf_rx_ = pending_sbuf_rx_value_;
                    cpu_.mSFR[REG_SCON] = static_cast<uint8_t>(cpu_.mSFR[REG_SCON] | SCONMASK_RI);
                    cpu_.serial_interrupt_trigger = true;
                    ++serial_irq_asserts_;
                    pending_sbuf_rx_ = true;
                }
                if (panel_shadow_injection_enabled_ && panel_apply_iram_panel_shadow_) {
                    apply_panel_edge_injection();
                }
                /* Deferred callback countdown: if armed and not yet fired, decrement.
                 * When countdown reaches 0, set TF1 (Timer 1 overflow flag) so the 8051
                 * vectors to ISR vector 0x001B on the next tick. The firmware's ISR
                 * reads the callback PC from ff1b/ffc and calls it. */
                if (legacy_deferred_timer_ && deferred_active_ && !deferred_fired_) {
                    if (deferred_countdown_ > 0u) {
                        --deferred_countdown_;
                    }
                    /* Mirror countdown into XRAM so MOVX read of 0x04EE matches the shadow (xwrite
                     * only set ext_mem_ once; without this, xram[0x04EE] stayed at the armed value). */
                    ext_mem_[0x04EEu] = deferred_countdown_;
                    if (deferred_countdown_ == 0u) {
                        /* Set TF1 (Timer 1 overflow flag) to trigger ISR vector 0x001B */
                        cpu_.mSFR[REG_TCON] |= TCONMASK_TF1;
                        /* Ensure Timer 1 interrupt and global interrupt are enabled */
                        cpu_.mSFR[REG_IE] |= (IEMASK_ET1 | IEMASK_EA);
                        deferred_fired_ = true;
                    }
                }
                if (!pc_watch_.empty() && cpu_.mTickDelay == 0u) {
                    for (uint16_t w : pc_watch_) {
                        if (w == cpu_.mPC) pc_hits_.push_back({telemetry_.machine_cycle, w, cpu_.mSFR[REG_SP]});
                    }
                }
                tick(&cpu_);
                if (cpu_.mSFR[REG_SP] > sp_max_) sp_max_ = cpu_.mSFR[REG_SP];
                if (pending_sbuf_rx_ && (cpu_.mSFR[REG_SCON] & SCONMASK_RI) == 0u) {
                    ++serial_irq_clears_;
                    pending_sbuf_rx_ = false;
                }
                drain_completed_uart_tx();
                /* TAPE OUT is P3.4: driven by the tape Timer1 ISR (0x76b2) and by the playback sync-out path (0x1129/0x1382). */
                tape_acc += (cpu_.mSFR[REG_P3] & 0x10u) != 0u ? 1.0f : -1.0f;
                ++tape_ticks;
                /* P3.5 is the click strobe; the ROM decides when it clicks (record, count-in, PLAY CLICK). */
                const bool p3_5_high = (cpu_.mSFR[REG_P3] & 0x20u) != 0u;
                if (!last_click_p3_5_ && p3_5_high) {
                    ++click_edges_total;
                    uint32_t burst_samples = static_cast<uint32_t>(kClickBurstMs * 0.001 * sample_rate_ + 0.5);
                    if (burst_samples > 256u) burst_samples = 256u;
                    click_burst_remaining_ = burst_samples;
                }
                last_click_p3_5_ = p3_5_high;
                {
                    const uint8_t p3 = sfrread_p3(REG_P3);
                    if (p3 < panel_p3_min_) panel_p3_min_ = p3;
                    if (p3 > panel_p3_max_) panel_p3_max_ = p3;
                }
                last_ri_ = (cpu_.mSFR[REG_SCON] & SCONMASK_RI) != 0u;
                telemetry_.cpu_cycles += 12u;
                telemetry_.machine_cycle += 1u;
                if ((panel_press_mask_ | panel_edge_mask_) != 0u) tick_panel_input_decay();
            }
            {
                /* Output coupling capacitor: one-pole DC block (~20 Hz). */
                const float x = (tape_ticks != 0u) ? (tape_acc / static_cast<float>(tape_ticks)) : 0.0f;
                const float r = 1.0f - static_cast<float>(2.0 * 3.14159265358979 * 20.0 / sample_rate_);
                tape_hp_y_ = x - tape_hp_x_ + r * tape_hp_y_;
                if (std::fabs(tape_hp_y_) < 1e-12f) tape_hp_y_ = 0.0f;
                tape_hp_x_ = x;
                audio_out_right_[sample] = 0.5f * tape_hp_y_;
            }
            if (click_burst_remaining_ > 0u) {
                audio_out_left_[sample] = kClickBurstLevel;
                --click_burst_remaining_;
            } else {
                audio_out_left_[sample] = 0.0f;
            }
        }
        telemetry_.click_edges_last_block = click_edges_total;
        telemetry_.pc = cpu_.mPC;
        telemetry_.acc = cpu_.mSFR[REG_ACC];
        telemetry_.psw = cpu_.mSFR[REG_PSW];
        telemetry_.lcd_addr = lcd_addr_;
        telemetry_.lcd_display = lcd_display_;

        if (telemetry_.cpu_cycles < 5000u) boot_phase_ = 1u;
        else if (telemetry_.cpu_cycles < 15000u) boot_phase_ = 2u;
        else if (telemetry_.cpu_cycles < 30000u) boot_phase_ = 3u;
        else boot_phase_ = 4u;

        if (!boot_milestone_reached_ && telemetry_.cpu_cycles > 10000u && lcd_alive()) {
            boot_milestone_reached_ = true;
            telemetry_.boot_milestone_reached = true;
        }
    }

    std::string rom_path_;
    std::string rom_hash_;
    CpuBox box_{};
    struct em8051& cpu_ = box_.cpu;
    std::vector<uint8_t> code_mem_;
    std::vector<uint8_t> ext_mem_;
    std::vector<uint8_t> upper_mem_;
    std::array<char, 80> lcd_ddram_{};
    std::array<uint8_t, 64> lcd_cgram_{};
    uint8_t lcd_addr_ = 0u;
    uint8_t lcd_cgram_addr_ = 0u;
    bool lcd_addr_is_cgram_ = false;
    uint8_t lcd_display_shift_ = 0u;
    uint8_t lcd_display_ = 0u;
    bool lcd_entry_inc_ = true;
    bool lcd_entry_shift_ = false;

    HostTransportState transport_{};
    ProcessMode process_mode_ = ProcessMode::Realtime;
    double sample_rate_ = 48000.0;
    uint64_t frames_processed_ = 0;
    uint64_t loop_length_ticks_ = 384u;
    /* ROM MIDI output: messages stamped with the sample they completed in, bytes back to back. */
    struct OutMsg {
        uint32_t frame;
        uint32_t offset;
        uint32_t size;
    };
    std::vector<uint8_t> out_bytes_;
    std::vector<OutMsg> out_msgs_;
    uint32_t cur_frame_ = 0u;
    RuntimeTelemetry telemetry_{};
    bool rom_loaded_ = false;
    bool boot_milestone_reached_ = false;
    uint32_t boot_phase_ = 0;
    bool last_transport_playing_ = false;
    double samples_until_next_clock_ = 0.0;
    uint64_t xdata_writes_ = 0;
    uint64_t lcd_cmd_writes_ = 0;
    uint64_t lcd_data_writes_ = 0;
    uint64_t xram_04ee_writes_ = 0;
    uint64_t ff1b_writes_ = 0;
    uint64_t ff1c_writes_ = 0;
    mutable uint64_t serial_rx_reads_ = 0;
    mutable uint64_t xdata_reads_ = 0;
    mutable uint64_t ff06_reads_ = 0;
    mutable uint16_t last_xread_addr_ = 0;
    mutable uint16_t last_xread_pc_ = 0;
    uint16_t last_xdata_addr_ = 0;
    uint8_t last_xdata_value_ = 0;
    uint16_t last_xdata_pc_ = 0;
    uint8_t ff02_panel_out_latch_ = 0;
    uint8_t ff04_control_ = 0;
    uint8_t panel_p3_min_ = 0xff;
    uint8_t panel_p3_max_ = 0x00;
    uint8_t panel_track_led_latch_ = 0;
    uint8_t panel_button_led_latch_ = 0;
    uint8_t panel_track_led_observed_ = 0;
    uint8_t panel_button_led_observed_ = 0;
    uint8_t panel_ff04_fb_current_ = 0;
    uint8_t panel_ff04_fd_current_ = 0;
    uint8_t panel_ff04_fe_current_ = 0;
    bool panel_ff04_fb_seen_this_block_ = false;
    bool panel_ff04_fd_seen_this_block_ = false;
    bool panel_ff04_fe_seen_this_block_ = false;
    bool panel_ff04_fb_saw_nonzero_this_block_ = false;
    bool panel_ff04_fd_saw_nonzero_this_block_ = false;
    bool panel_ff04_fe_saw_nonzero_this_block_ = false;
    uint8_t panel_ff04_fb_observed_ = 0;
    uint8_t panel_ff04_fd_observed_ = 0;
    uint8_t panel_ff04_fe_observed_ = 0;
    uint8_t ff06_control_ = 0;
    mutable uint8_t ff06_serial_tape_ = 0;
    std::vector<uint16_t> pc_watch_;
    struct PcHit {
        uint64_t cycle;
        uint16_t pc;
        uint8_t sp;
    };
    std::vector<PcHit> pc_hits_;
    float tape_hp_x_ = 0.0f;
    float tape_hp_y_ = 0.0f;
    uint8_t ff08_lcd_cmd_ = 0;
    uint8_t ff09_lcd_data_ = 0;
    uint8_t ff0e_clock_source_ = 0;
    uint8_t ff0f_realtime_tx_gate_ = 1;
    uint8_t ff1a_autostart_ = 0;
    uint8_t ff1b_callback_hi_ = 0;
    uint8_t ff1c_callback_lo_ = 0;
    uint8_t deferred_countdown_ = 0;
    bool deferred_active_ = false;
    bool deferred_fired_ = false;
    uint8_t sp_max_ = 0;
    uint8_t ff04_panel_p1_sample_ = 0xFF;
    int ff06_scan_bank_ = -1;
    // Keep clicks long enough for matrix scan windows without turning taps into holds.
    // Keep non-track presses short so menu-digit/commit keys do not autorepeat
    // across many scan passes while still remaining reliably visible to the ROM.
    static constexpr uint32_t kPanelPressPulseTicks = 4096u;
    // Track presses should be near-instant to avoid double-toggle from re-scans.
    static constexpr uint32_t kPanelTrackPressPulseTicks = 8u;
    // Edge pulse must survive until the emulated main loop reaches handlers that run after
    // a long ui_render (e.g. OpenMMT-8 openmmt8_panel_page_after_ui). 8 ticks expired inside
    // ui_render, clearing the OR'd edge before PAGE was consumed. Track buttons keep a short
    // edge so a single physical tap does not appear as multiple edges across rescans.
    static constexpr uint32_t kPanelEdgePulseTicks = 8u;
    // A physical tap: the key stays closed for 30 ms of 1 MHz machine cycles.
    static constexpr uint32_t kPanelRealPressTicks = 30000u;
    static constexpr uint32_t kPanelFootswitchId = 19u;
    bool panel_real_matrix_ = true;
    bool legacy_deferred_timer_ = false;
    PanelMask panel_press_mask_ = 0u;
    PanelMask panel_edge_mask_ = 0u;
    PanelMask panel_hold_mask_ = 0u;
    std::array<uint32_t, kPanelButtonCount> panel_press_ticks_{};
    std::array<uint32_t, kPanelButtonCount> panel_edge_ticks_{};
    mutable std::deque<uint8_t> serial_rx_queue_{};
    std::deque<uint8_t> serial_tx_log_{};
    bool serial_tx_log_on_ = false; /* set by the first debug drain, so plain hosts never grow it */
    std::deque<uint32_t> panel_write_log_{};
    bool panel_write_log_on_ = false;
    RingQueue<ScheduledSerialByte> uart_rx_schedule_{};
    uint64_t serial_irq_asserts_ = 0;
    uint64_t serial_irq_clears_ = 0;
    uint64_t isr_int0_hits_ = 0;
    uint64_t isr_timer0_hits_ = 0;
    uint64_t isr_int1_hits_ = 0;
    uint64_t isr_timer1_hits_ = 0;
    uint64_t isr_serial_hits_ = 0;
    uint64_t isr_timer2_hits_ = 0;
    bool panel_shadow_injection_enabled_ = true;
    uint8_t panel_iram_edge_base_ = 0x20u;
    uint8_t panel_iram_raw_base_ = 0x26u;
    bool panel_inject_edges_to_iram_ = true;
    bool panel_apply_iram_panel_shadow_ = true;
    bool last_ri_ = false;
    bool pending_sbuf_rx_ = false;
    uint8_t pending_sbuf_rx_value_ = 0;
    uint8_t sbuf_rx_ = 0;
    uint64_t midi_rx_next_due_tick_ = 0u;
    uint8_t last_serial_out_idx_ = 0u;
    uint8_t midi_tx_running_status_ = 0u;
    uint8_t midi_tx_expected_data_bytes_ = 0u;
    bool midi_tx_sysex_active_ = false;
    std::vector<uint8_t> midi_tx_message_{};
    const float* audio_in_left_ = nullptr;
    const float* audio_in_right_ = nullptr;
    uint32_t audio_in_frames_ = 0u;
    std::vector<float> audio_out_left_{};
    std::vector<float> audio_out_right_{};
    uint8_t external_p3_mask_ = 0x04u;
    uint8_t external_p3_value_ = 0u;
    double cpu_tick_carry_ = 0.0;
    /* ROM-driven record click: edge + burst so click is audible in DAW (P3.5 = beat strobe). */
    static constexpr float kClickBurstLevel = 0.4f;
    static constexpr double kClickBurstMs = 1.5;
    uint32_t click_burst_remaining_ = 0u;
    bool last_click_p3_5_ = false;
    float click_out_volume_ = 1.0f;
    uint32_t click_test_remaining_ = 0u;
    double click_test_phase_ = 0.0;
};

extern "C" IPluginRuntime* mmt8_clap_wrapper_create() { return new ClapWrapperRuntime(); }
extern "C" void mmt8_clap_wrapper_destroy(IPluginRuntime* runtime) { delete runtime; }
extern "C" bool mmt8_clap_wrapper_debug_set_ffxx(IPluginRuntime* runtime, uint16_t address, uint8_t value) {
    auto* impl = dynamic_cast<ClapWrapperRuntime*>(runtime);
    if (impl == nullptr) return false;
    return impl->debug_set_ffxx(address, value);
}
extern "C" bool mmt8_clap_wrapper_debug_serial_rx(IPluginRuntime* runtime, uint8_t value) {
    auto* impl = dynamic_cast<ClapWrapperRuntime*>(runtime);
    if (impl == nullptr) return false;
    return impl->debug_serial_rx(value);
}
extern "C" bool mmt8_clap_wrapper_debug_uart_rx(IPluginRuntime* runtime, uint8_t value) {
    auto* impl = dynamic_cast<ClapWrapperRuntime*>(runtime);
    if (impl == nullptr) return false;
    return impl->debug_uart_rx(value);
}
extern "C" bool mmt8_clap_wrapper_debug_set_pc(IPluginRuntime* runtime, uint16_t pc) {
    auto* impl = dynamic_cast<ClapWrapperRuntime*>(runtime);
    if (impl == nullptr) return false;
    return impl->debug_set_pc(pc);
}
extern "C" bool mmt8_clap_wrapper_debug_set_iram(IPluginRuntime* runtime, uint8_t address, uint8_t value) {
    auto* impl = dynamic_cast<ClapWrapperRuntime*>(runtime);
    if (impl == nullptr) return false;
    return impl->debug_set_iram(address, value);
}
extern "C" bool mmt8_clap_wrapper_debug_set_sfr(IPluginRuntime* runtime, uint8_t sfr_addr, uint8_t value) {
    auto* impl = dynamic_cast<ClapWrapperRuntime*>(runtime);
    if (impl == nullptr) return false;
    return impl->debug_set_sfr(sfr_addr, value);
}
extern "C" bool mmt8_clap_wrapper_debug_set_xram(IPluginRuntime* runtime, uint16_t address, uint8_t value) {
    auto* impl = dynamic_cast<ClapWrapperRuntime*>(runtime);
    if (impl == nullptr) return false;
    return impl->debug_set_xram(address, value);
}
extern "C" bool mmt8_clap_wrapper_debug_peek_iram(IPluginRuntime* runtime, uint8_t address, uint8_t* value_out) {
    auto* impl = dynamic_cast<ClapWrapperRuntime*>(runtime);
    if (impl == nullptr) return false;
    return impl->debug_peek_iram(address, value_out);
}
extern "C" void mmt8_clap_wrapper_debug_watch_pc(IPluginRuntime* runtime, uint16_t pc) {
    auto* impl = dynamic_cast<ClapWrapperRuntime*>(runtime);
    if (impl != nullptr) impl->debug_watch_pc(pc);
}
extern "C" uint32_t mmt8_clap_wrapper_debug_drain_pc_hits(IPluginRuntime* runtime, uint64_t* cycles, uint16_t* pcs,
                                                          uint8_t* sps, uint32_t max) {
    auto* impl = dynamic_cast<ClapWrapperRuntime*>(runtime);
    return impl != nullptr ? impl->debug_drain_pc_hits(cycles, pcs, sps, max) : 0u;
}
extern "C" bool mmt8_clap_wrapper_debug_peek_sfr(IPluginRuntime* runtime, uint8_t sfr_addr, uint8_t* value_out) {
    auto* impl = dynamic_cast<ClapWrapperRuntime*>(runtime);
    if (impl == nullptr) return false;
    return impl->debug_peek_sfr(sfr_addr, value_out);
}
extern "C" bool mmt8_clap_wrapper_debug_peek_xram(IPluginRuntime* runtime, uint16_t address, uint8_t* value_out) {
    auto* impl = dynamic_cast<ClapWrapperRuntime*>(runtime);
    if (impl == nullptr) return false;
    return impl->debug_peek_xram(address, value_out);
}
extern "C" uint8_t mmt8_clap_wrapper_debug_sp_max(IPluginRuntime* runtime) {
    auto* impl = dynamic_cast<ClapWrapperRuntime*>(runtime);
    return impl == nullptr ? 0u : impl->debug_sp_max();
}
extern "C" void mmt8_clap_wrapper_debug_enable_panel_write_log(IPluginRuntime* runtime, bool on) {
    auto* impl = dynamic_cast<ClapWrapperRuntime*>(runtime);
    if (impl != nullptr) impl->debug_enable_panel_write_log(on);
}
extern "C" uint32_t mmt8_clap_wrapper_debug_drain_panel_writes(IPluginRuntime* runtime, uint32_t* out, uint32_t max_entries) {
    auto* impl = dynamic_cast<ClapWrapperRuntime*>(runtime);
    return impl == nullptr ? 0u : impl->debug_drain_panel_writes(out, max_entries);
}
extern "C" uint32_t mmt8_clap_wrapper_debug_drain_serial_tx(
    IPluginRuntime* runtime,
    uint8_t* bytes_out,
    uint32_t max_bytes) {
    auto* impl = dynamic_cast<ClapWrapperRuntime*>(runtime);
    if (impl == nullptr) return 0u;
    return impl->debug_drain_serial_tx(bytes_out, max_bytes);
}

}  // namespace mmt_emu
