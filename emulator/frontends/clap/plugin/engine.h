#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <clap/clap.h>

#include "mmt_emu/plugin_port.hpp"
#include "project_state.h"
#include "rom_store.h"

namespace mmt8 {

enum ParamId : clap_id { kParamSync = 1, kParamClick = 2, kParamLast = kParamClick };

/** Where the ROM comes from: a file, or one of the ROMs built into the plugin. */
enum class Builtin : uint8_t { File, OpenMMT8, Jam8 };

/**
 * One emulated MMT-8. Threads:
 *  - audio thread: process(); takes the runtime lock with try_lock and outputs silence if busy.
 *  - main thread: ROM loading, power cycling, state; takes the runtime lock and blocks.
 *  - GUI (main thread): press() queues key changes lock-free; read_panel() reads a seqlock
 *    snapshot the audio thread publishes after every block.
 */
class Engine {
public:
    Engine();
    ~Engine();

    // main thread
    /** Keeps the last ROM chosen and each ROM's memory in dir between sessions (see RomStore). */
    void attach_store(const std::string &dir);
    /** A new instance: the ROM last chosen, with the memory it last had. */
    void restore_last();
    bool activate(double sample_rate, uint32_t max_frames);
    void deactivate();
    bool load_rom_file(const std::string &path, std::string &error);
    bool use_builtin_rom(Builtin which = Builtin::OpenMMT8);
    void power_cycle();
    std::vector<uint8_t> save_state();
    bool load_state(const std::vector<uint8_t> &chunk);
    std::string rom_name() const;
    bool rom_is_builtin() const;
    Builtin rom_builtin() const;
    bool rom_loaded() const { return rom_loaded_.load(std::memory_order_acquire); }
    /** Changes whenever a ROM is loaded, the state restored or the unit power-cycled. */
    uint32_t rom_version() const { return rom_version_.load(std::memory_order_acquire); }
    static bool has_builtin_rom(Builtin which = Builtin::OpenMMT8);

    // GUI
    void press(uint8_t key, bool down);
    void read_panel(mmt_emu::PanelView &out) const;
    uint32_t gui_width() const { return gui_width_.load(std::memory_order_relaxed); }
    void set_gui_width(uint32_t w) { gui_width_.store(w, std::memory_order_relaxed); }
    /** A parameter changed from the GUI; the next process() or flush() tells the host. */
    void set_param_from_gui(clap_id id, double value);
    bool host_sync_receiving() const { return sync_active_.load(std::memory_order_relaxed); }

    // parameters, any thread
    double param(clap_id id) const;

    // audio thread
    clap_process_status process(const clap_process_t *p);
    void flush_params(const clap_input_events_t *in, const clap_output_events_t *out);

private:
    struct KeyCmd {
        uint8_t key;
        uint8_t down;
    };
    struct BridgeMsg {
        uint32_t frame;
        uint8_t size;
        uint8_t data[3];
    };

    void apply_param(clap_id id, double value);
    void handle_event(const clap_event_header_t *h, uint32_t frame_base);
    void emit_param_changes(const clap_output_events_t *out);
    void run_bridge(const clap_process_t *p);
    void bridge_add(uint32_t frame, uint8_t a, int b = -1, int c = -1);
    void publish_panel();
    bool load_image_locked(const std::vector<uint8_t> &image, const std::string &name, Builtin builtin);
    /** Another ROM: the current one's memory is kept, and the new one gets its own back. */
    bool switch_rom_locked(const std::vector<uint8_t> &image, const std::string &name, Builtin builtin);
    void keep_ram_locked();
    void restore_ram_locked();
    static void on_midi_out(void *ctx, uint32_t frame, const uint8_t *data, uint32_t size);

    std::unique_ptr<mmt_emu::IPluginRuntime> rt_;
    mutable std::mutex rt_mu_;

    std::atomic<bool> rom_loaded_{false};
    std::atomic<uint32_t> rom_version_{0};
    Builtin rom_builtin_ = Builtin::OpenMMT8;
    std::string rom_name_;
    std::vector<uint8_t> rom_image_;
    std::string rom_key_;
    /** Memory of the ROMs used in this project other than the current one, by key. */
    std::map<std::string, std::vector<uint8_t>> ram_bank_;
    std::unique_ptr<RomStore> store_;
    double sample_rate_ = 48000.0;
    bool active_ = false;

    // parameters
    std::atomic<bool> host_sync_{true};
    std::atomic<float> click_level_{0.8f};
    std::atomic<uint32_t> gui_dirty_params_{0};
    std::atomic<uint32_t> gui_width_{0};

    // GUI -> audio key queue (single producer, single consumer)
    static constexpr uint32_t kKeyQueue = 256;
    KeyCmd keys_[kKeyQueue]{};
    std::atomic<uint32_t> key_head_{0};
    std::atomic<uint32_t> key_tail_{0};

    // panel snapshot (seqlock, writers serialized by rt_mu_)
    mutable std::atomic<uint32_t> panel_seq_{0};
    mmt_emu::PanelView panel_{};

    // host transport -> MIDI cable
    bool br_playing_ = false;
    bool br_have_pos_ = false;
    double br_next_beats_ = 0.0;    // expected position at the start of the next block
    int64_t br_next_clock_ = 0;     // next 1/24-beat clock index to send
    bool br_pending_ = false;       // waiting for br_start_beats_ to send FA/FB
    double br_start_beats_ = 0.0;
    bool br_start_is_fa_ = false;
    bool br_spp_due_ = false;       // SONG POSITION waiting for br_spp_beats_
    double br_spp_beats_ = 0.0;
    int32_t br_spp_value_ = 0;
    /* STOP to SONG POSITION on a relocation: the stock ROM ignores SPP that arrives within
     * ~1 ms of STOP (OpenMMT-8 needs none); measured with mmt8_clap_engine_test. */
    double relocate_gap_ms_ = 4.0;
    int32_t br_stopped_spp_ = -1;   // last SPP sent while stopped
    std::atomic<bool> sync_active_{false};
    BridgeMsg bridge_[512]{};
    uint32_t bridge_n_ = 0;

    // ROM MIDI out -> host
    const clap_output_events_t *out_events_ = nullptr;
    uint32_t out_frame_base_ = 0;
    std::vector<uint8_t> sysex_out_;
    std::vector<float> scratch_l_, scratch_r_;
};

}  // namespace mmt8
