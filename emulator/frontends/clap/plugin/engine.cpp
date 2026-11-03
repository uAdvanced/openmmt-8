#include "engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "builtin_rom.h"
#include "log.h"
#include "panel_layout.h"
#include "wrapper_stub.h"

namespace mmt8 {
namespace {

constexpr uint32_t kChunk = 4096;

uint32_t midi_length(uint8_t status) {
    if (status < 0x80) return 0;
    if (status < 0xF0) return ((status & 0xE0) == 0xC0) ? 2 : 3;
    switch (status) {
        case 0xF1:
        case 0xF3: return 2;
        case 0xF2: return 3;
        case 0xF0:
        case 0xF7: return 0;
        default: return 1;
    }
}

std::string base_name(const std::string &path) {
    const size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? path : path.substr(cut + 1);
}

struct BuiltinInfo {
    const unsigned char *data;
    size_t size;
    const char *name;  // shown in the GUI
    const char *file;  // name the wrapper is given
    const char *key;   // RomStore key
};

BuiltinInfo builtin_info(Builtin b) {
    if (b == Builtin::Jam8) return {kJamRom, kJamRomSize, "JAM-8 (built-in)", "jam8_builtin.bin", RomStore::kJamKey};
    return {kBuiltinRom, kBuiltinRomSize, "OpenMMT-8 (built-in)", "openmmt8_builtin.bin", RomStore::kBuiltinKey};
}

std::vector<uint8_t> builtin_image(Builtin b) {
    const BuiltinInfo i = builtin_info(b);
    return std::vector<uint8_t>(i.data, i.data + i.size);
}

}  // namespace

Engine::Engine() : rt_(mmt8_clap_wrapper_create()) {
    sysex_out_.reserve(1u << 17);
    scratch_l_.assign(kChunk, 0.0f);
    scratch_r_.assign(kChunk, 0.0f);
    std::lock_guard<std::mutex> lk(rt_mu_);
    if (has_builtin_rom()) {
        load_image_locked(builtin_image(Builtin::OpenMMT8), "", Builtin::OpenMMT8);
    } else {
        log("engine: this build has no built-in ROM; load one from the panel");
        publish_panel();
    }
}

Engine::~Engine() {
    std::lock_guard<std::mutex> lk(rt_mu_);
    if (store_ && rom_loaded_.load(std::memory_order_relaxed)) store_->save_ram(rom_key_, rt_->battery_ram());
}

bool Engine::has_builtin_rom(Builtin which) { return which != Builtin::File && builtin_info(which).size > 0; }

void Engine::attach_store(const std::string &dir) {
    std::lock_guard<std::mutex> lk(rt_mu_);
    store_ = dir.empty() ? nullptr : std::make_unique<RomStore>(dir);
    log("engine: memory store %s", dir.empty() ? "off" : dir.c_str());
}

void Engine::restore_last() {
    std::lock_guard<std::mutex> lk(rt_mu_);
    if (!store_) return;
    std::string key, name;
    std::vector<uint8_t> image;
    bool ok = false;
    const bool have_last = store_->last(key, name);
    if (have_last && key == RomStore::kJamKey && has_builtin_rom(Builtin::Jam8))
        ok = rom_builtin_ == Builtin::Jam8 || load_image_locked(builtin_image(Builtin::Jam8), "", Builtin::Jam8);
    else if (have_last && key != RomStore::kBuiltinKey && key != RomStore::kJamKey && store_->load_rom(key, image))
        ok = load_image_locked(image, name, Builtin::File);
    else if (has_builtin_rom())
        ok = rom_builtin_ == Builtin::OpenMMT8 ||
             load_image_locked(builtin_image(Builtin::OpenMMT8), "", Builtin::OpenMMT8);
    if (ok) restore_ram_locked();
    log("engine: new instance with ROM '%s'", rom_name_.c_str());
}

void Engine::keep_ram_locked() {
    if (!rom_loaded_.load(std::memory_order_relaxed)) return;
    std::vector<uint8_t> ram = rt_->battery_ram();
    if (store_) store_->save_ram(rom_key_, ram);
    ram_bank_[rom_key_] = std::move(ram);
}

/* The memory this project last had with the ROM, else the memory it last had anywhere. */
void Engine::restore_ram_locked() {
    std::vector<uint8_t> ram;
    const auto it = ram_bank_.find(rom_key_);
    if (it != ram_bank_.end()) ram = it->second;
    else if (store_) store_->load_ram(rom_key_, ram);
    if (!ram.empty() && rt_->set_battery_ram(ram)) {
        publish_panel();
        log("engine: ROM '%s' gets its memory back", rom_name_.c_str());
    }
}

bool Engine::switch_rom_locked(const std::vector<uint8_t> &image, const std::string &name, Builtin builtin) {
    if (image.empty() || image.size() > 0x10000u) return false;
    keep_ram_locked();
    if (!load_image_locked(image, name, builtin)) return false;
    restore_ram_locked();
    if (store_) {
        if (builtin == Builtin::File) store_->save_rom(rom_key_, image);
        store_->set_last(rom_key_, rom_name_);
    }
    return true;
}

bool Engine::load_image_locked(const std::vector<uint8_t> &image, const std::string &name, Builtin builtin) {
    if (image.empty() || image.size() > 0x10000u) return false;
    const bool is_builtin = builtin != Builtin::File;
    const BuiltinInfo info = builtin_info(builtin);
    if (!rt_->load_rom_image(image, is_builtin ? std::string(info.file) : name)) return false;
    rt_->set_sample_rate(sample_rate_);
    rt_->set_click_out_volume(1.0f);
    rom_builtin_ = builtin;
    rom_name_ = is_builtin ? info.name : name;
    rom_image_ = is_builtin ? std::vector<uint8_t>() : image;
    rom_key_ = is_builtin ? info.key : RomStore::key_of(image);
    br_playing_ = br_pending_ = br_spp_due_ = br_have_pos_ = false;
    br_stopped_spp_ = -1;
    rom_loaded_.store(true, std::memory_order_release);
    rom_version_.fetch_add(1, std::memory_order_release);
    publish_panel();
    log("engine: loaded ROM '%s' (%zu bytes)", rom_name_.c_str(), image.size());
    return true;
}

bool Engine::activate(double sample_rate, uint32_t max_frames) {
    std::lock_guard<std::mutex> lk(rt_mu_);
    sample_rate_ = sample_rate > 1000.0 ? sample_rate : 48000.0;
    rt_->set_sample_rate(sample_rate_);
    active_ = true;
    log("engine: activate %.0f Hz, max %u frames", sample_rate_, max_frames);
    return true;
}

void Engine::deactivate() {
    std::lock_guard<std::mutex> lk(rt_mu_);
    active_ = false;
    sync_active_.store(false, std::memory_order_relaxed);
}

bool Engine::load_rom_file(const std::string &path, std::string &error) {
    std::vector<uint8_t> image;
    if (!read_file(path, image) || image.empty()) {
        error = "Cannot read " + base_name(path);
        return false;
    }
    if (image.size() > 0x10000u) {
        error = base_name(path) + " is larger than 64 KB";
        return false;
    }
    std::lock_guard<std::mutex> lk(rt_mu_);
    if (!switch_rom_locked(image, base_name(path), Builtin::File)) {
        error = "The emulator rejected " + base_name(path);
        return false;
    }
    return true;
}

bool Engine::use_builtin_rom(Builtin which) {
    if (!has_builtin_rom(which)) return false;
    std::lock_guard<std::mutex> lk(rt_mu_);
    return switch_rom_locked(builtin_image(which), "", which);
}

void Engine::power_cycle() {
    std::lock_guard<std::mutex> lk(rt_mu_);
    if (!rom_loaded_.load(std::memory_order_relaxed)) return;
    rt_->set_battery_ram(rt_->battery_ram());
    br_playing_ = br_pending_ = br_spp_due_ = br_have_pos_ = false;
    br_stopped_spp_ = -1;
    publish_panel();
    log("engine: power cycled");
}

std::string Engine::rom_name() const {
    std::lock_guard<std::mutex> lk(rt_mu_);
    return rom_name_;
}

bool Engine::rom_is_builtin() const {
    std::lock_guard<std::mutex> lk(rt_mu_);
    return rom_builtin_ != Builtin::File;
}

Builtin Engine::rom_builtin() const {
    std::lock_guard<std::mutex> lk(rt_mu_);
    return rom_builtin_;
}

std::vector<uint8_t> Engine::save_state() {
    ProjectState s;
    {
        std::lock_guard<std::mutex> lk(rt_mu_);
        s.builtin_rom = rom_builtin_ != Builtin::File;
        s.builtin_jam8 = rom_builtin_ == Builtin::Jam8;
        s.rom_name = rom_name_;
        s.rom = rom_image_;
        if (rom_loaded_.load(std::memory_order_relaxed)) {
            s.ram = rt_->battery_ram();
            if (store_) store_->save_ram(rom_key_, s.ram);
        }
        s.other_ram = ram_bank_;
        s.other_ram.erase(rom_key_);
    }
    s.host_sync = host_sync_.load();
    s.click_level = click_level_.load();
    s.gui_width = gui_width_.load();
    std::vector<uint8_t> chunk = encode_state(s);
    log("engine: saved state, %zu bytes (rom '%s', ram %zu)", chunk.size(), s.rom_name.c_str(), s.ram.size());
    return chunk;
}

bool Engine::load_state(const std::vector<uint8_t> &chunk) {
    ProjectState s;
    if (!decode_state(chunk, s)) {
        log("engine: state chunk not recognized (%zu bytes)", chunk.size());
        return false;
    }
    host_sync_.store(s.host_sync);
    click_level_.store(s.click_level);
    gui_width_.store(s.gui_width);
    std::vector<uint8_t> image;
    std::string name;
    Builtin builtin = !s.builtin_rom ? Builtin::File : s.builtin_jam8 ? Builtin::Jam8 : Builtin::OpenMMT8;
    if (!s.legacy_rom_path.empty()) {
        if (read_file(s.legacy_rom_path, image) && !image.empty() && image.size() <= 0x10000u) {
            name = base_name(s.legacy_rom_path);
            builtin = Builtin::File;
        }
        log("engine: old-plugin project, ROM path '%s' %s", s.legacy_rom_path.c_str(),
            builtin != Builtin::File ? "missing; using built-in" : "loaded");
    } else if (builtin == Builtin::File) {
        image = std::move(s.rom);
        name = s.rom_name;
    }
    std::lock_guard<std::mutex> lk(rt_mu_);
    ram_bank_ = std::move(s.other_ram);
    bool ok;
    if (builtin != Builtin::File) {
        ok = has_builtin_rom(builtin) && load_image_locked(builtin_image(builtin), "", builtin);
    } else {
        ok = load_image_locked(image, name, Builtin::File);
    }
    if (ok && !s.ram.empty()) {
        rt_->set_battery_ram(s.ram);
        publish_panel();
    }
    log("engine: loaded state (rom '%s', ram %zu bytes) %s", rom_name_.c_str(), s.ram.size(), ok ? "ok" : "FAILED");
    return true;
}

void Engine::press(uint8_t key, bool down) {
    const uint32_t h = key_head_.load(std::memory_order_relaxed);
    if (h - key_tail_.load(std::memory_order_acquire) >= kKeyQueue) return;
    keys_[h % kKeyQueue] = KeyCmd{key, static_cast<uint8_t>(down ? 1 : 0)};
    key_head_.store(h + 1, std::memory_order_release);
}

void Engine::publish_panel() {
    mmt_emu::PanelView v;
    rt_->panel_view(v);
    v.rom_loaded = rom_loaded_.load(std::memory_order_relaxed);
    const uint32_t s = panel_seq_.load(std::memory_order_relaxed);
    panel_seq_.store(s + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    panel_ = v;
    panel_seq_.store(s + 2, std::memory_order_release);
}

void Engine::read_panel(mmt_emu::PanelView &out) const {
    for (int tries = 0; tries < 64; ++tries) {
        const uint32_t s1 = panel_seq_.load(std::memory_order_acquire);
        if (s1 & 1u) continue;
        out = panel_;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (panel_seq_.load(std::memory_order_relaxed) == s1) return;
    }
}

double Engine::param(clap_id id) const {
    switch (id) {
        case kParamSync: return host_sync_.load() ? 1.0 : 0.0;
        case kParamClick: return click_level_.load();
        default: return 0.0;
    }
}

void Engine::apply_param(clap_id id, double value) {
    switch (id) {
        case kParamSync: host_sync_.store(value >= 0.5); break;
        case kParamClick: click_level_.store(static_cast<float>(std::clamp(value, 0.0, 1.0))); break;
        default: break;
    }
}

void Engine::set_param_from_gui(clap_id id, double value) {
    apply_param(id, value);
    gui_dirty_params_.fetch_or(1u << id);
}

void Engine::emit_param_changes(const clap_output_events_t *out) {
    const uint32_t dirty = gui_dirty_params_.exchange(0);
    if (!dirty || !out) return;
    for (clap_id id = 1; id <= kParamLast; ++id) {
        if (!(dirty & (1u << id))) continue;
        clap_event_param_gesture_t g{};
        g.header = {sizeof(g), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_GESTURE_BEGIN, 0};
        g.param_id = id;
        out->try_push(out, &g.header);
        clap_event_param_value_t v{};
        v.header = {sizeof(v), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        v.param_id = id;
        v.note_id = -1;
        v.port_index = -1;
        v.channel = -1;
        v.key = -1;
        v.value = param(id);
        out->try_push(out, &v.header);
        g.header.type = CLAP_EVENT_PARAM_GESTURE_END;
        out->try_push(out, &g.header);
    }
}

void Engine::flush_params(const clap_input_events_t *in, const clap_output_events_t *out) {
    if (in) {
        const uint32_t n = in->size(in);
        for (uint32_t i = 0; i < n; ++i) {
            const clap_event_header_t *h = in->get(in, i);
            if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE) {
                const auto *e = reinterpret_cast<const clap_event_param_value_t *>(h);
                apply_param(e->param_id, e->value);
            }
        }
    }
    emit_param_changes(out);
}

void Engine::bridge_add(uint32_t frame, uint8_t a, int b, int c) {
    if (bridge_n_ >= sizeof(bridge_) / sizeof(bridge_[0])) return;
    BridgeMsg &m = bridge_[bridge_n_++];
    m.frame = frame;
    m.data[0] = a;
    m.size = 1;
    if (b >= 0) m.data[m.size++] = static_cast<uint8_t>(b & 0x7F);
    if (c >= 0) m.data[m.size++] = static_cast<uint8_t>(c & 0x7F);
}

void Engine::run_bridge(const clap_process_t *p) {
    bridge_n_ = 0;
    const clap_event_transport_t *t = p->transport;
    const bool usable = host_sync_.load(std::memory_order_relaxed) && t &&
                        (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) && (t->flags & CLAP_TRANSPORT_HAS_TEMPO);
    if (!usable) {
        if (br_playing_) bridge_add(0, 0xFC);
        br_playing_ = br_pending_ = br_spp_due_ = br_have_pos_ = false;
        br_stopped_spp_ = -1;
        sync_active_.store(false, std::memory_order_relaxed);
        return;
    }
    sync_active_.store(true, std::memory_order_relaxed);

    const uint32_t frames = p->frames_count;
    const double beats = static_cast<double>(t->song_pos_beats) / static_cast<double>(CLAP_BEATTIME_FACTOR);
    const double tempo = std::max(1.0, t->tempo);
    const double spb = sample_rate_ * 60.0 / tempo;
    const double end_beats = beats + static_cast<double>(frames) / spb;
    const bool playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
    auto frame_of = [&](double b) {
        const double f = std::ceil((b - beats) * spb - 1e-6);
        return static_cast<uint32_t>(std::clamp(f, 0.0, static_cast<double>(frames ? frames - 1 : 0)));
    };

    if (!playing) {
        if (br_playing_) bridge_add(0, 0xFC);
        br_playing_ = br_pending_ = br_spp_due_ = br_have_pos_ = false;
        /* A locate while stopped moves the MMT-8's continue point, like a DAW sending SPP. */
        const int32_t spp = beats <= 0.0 ? 0 : static_cast<int32_t>(std::floor(beats * 4.0 + 1e-6));
        if (spp != br_stopped_spp_ && spp < 16384) {
            bridge_add(0, 0xF2, spp & 0x7F, spp >> 7);
            br_stopped_spp_ = spp;
        }
        return;
    }

    /* Starts the MMT-8 at the 16th note it should be in (if less than a clock past it) or the
     * next one: START from the top, else SONG POSITION then CONTINUE. A relocation while
     * playing stops it first, as a DAW does, and leaves the ROM time to act on the STOP before
     * the SONG POSITION; clocks that fall due meanwhile follow CONTINUE straight away. */
    auto plan_start = [&](double from, bool relocate) {
        const double ms = tempo / 60000.0;
        double f = std::max(0.0, from);
        if (relocate) {
            bridge_add(0, 0xFC);
            br_spp_beats_ = from + relocate_gap_ms_ * ms;
            f = std::max(0.0, br_spp_beats_ + ms);
        } else {
            br_spp_beats_ = from;
        }
        int32_t spp = static_cast<int32_t>(std::floor(f * 4.0 + 1e-9));
        if (f * 4.0 - spp > 1.0 / 6.0) ++spp;
        spp = std::min(spp, 16383);
        br_pending_ = true;
        br_start_beats_ = std::max(f, spp / 4.0);
        br_next_clock_ = static_cast<int64_t>(spp) * 6;
        br_start_is_fa_ = spp == 0 && !relocate;
        br_spp_value_ = spp;
        br_spp_due_ = !br_start_is_fa_;
    };

    if (!br_playing_) {
        plan_start(beats, false);
    } else if (br_have_pos_ && std::fabs(beats - br_next_beats_) > 1.0 / 12.0) {
        plan_start(beats, true);
    }
    br_playing_ = true;
    br_stopped_spp_ = -1;

    if (br_spp_due_ && br_spp_beats_ < end_beats) {
        bridge_add(frame_of(br_spp_beats_), 0xF2, br_spp_value_ & 0x7F, br_spp_value_ >> 7);
        br_spp_due_ = false;
    }
    if (br_pending_ && !br_spp_due_ && br_start_beats_ < end_beats) {
        bridge_add(frame_of(br_start_beats_), br_start_is_fa_ ? 0xFA : 0xFB);
        br_pending_ = false;
    }
    if (!br_pending_) {
        if (static_cast<double>(br_next_clock_) / 24.0 < beats - 1.0) {
            br_next_clock_ = static_cast<int64_t>(std::ceil(beats * 24.0 - 1e-9));
        }
        for (;;) {
            const double cb = static_cast<double>(br_next_clock_) / 24.0;
            if (cb >= end_beats) break;
            bridge_add(frame_of(cb), 0xF8);
            ++br_next_clock_;
        }
    }
    br_next_beats_ = end_beats;
    br_have_pos_ = true;
}

void Engine::on_midi_out(void *ctx, uint32_t frame, const uint8_t *data, uint32_t size) {
    auto *self = static_cast<Engine *>(ctx);
    const clap_output_events_t *out = self->out_events_;
    if (!out || size == 0) return;
    const uint32_t time = self->out_frame_base_ + frame;
    if (size <= 3) {
        clap_event_midi_t e{};
        e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
        e.port_index = 0;
        std::memcpy(e.data, data, size);
        out->try_push(out, &e.header);
        return;
    }
    /* The host reads sysex buffers until process() returns; sysex_out_ never reallocates. */
    if (self->sysex_out_.size() + size > self->sysex_out_.capacity()) return;
    const size_t at = self->sysex_out_.size();
    self->sysex_out_.insert(self->sysex_out_.end(), data, data + size);
    clap_event_midi_sysex_t e{};
    e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI_SYSEX, 0};
    e.port_index = 0;
    e.buffer = self->sysex_out_.data() + at;
    e.size = size;
    out->try_push(out, &e.header);
}

void Engine::handle_event(const clap_event_header_t *h, uint32_t frame_base) {
    if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    const uint32_t frame = h->time > frame_base ? h->time - frame_base : 0;
    switch (h->type) {
        case CLAP_EVENT_PARAM_VALUE: {
            const auto *e = reinterpret_cast<const clap_event_param_value_t *>(h);
            apply_param(e->param_id, e->value);
            break;
        }
        case CLAP_EVENT_MIDI: {
            const auto *e = reinterpret_cast<const clap_event_midi_t *>(h);
            const uint32_t n = midi_length(e->data[0]);
            if (n) rt_->push_midi_bytes(frame, e->data, n);
            break;
        }
        case CLAP_EVENT_MIDI_SYSEX: {
            const auto *e = reinterpret_cast<const clap_event_midi_sysex_t *>(h);
            if (e->buffer && e->size) rt_->push_midi_bytes(frame, e->buffer, e->size);
            break;
        }
        case CLAP_EVENT_NOTE_ON:
        case CLAP_EVENT_NOTE_OFF: {
            const auto *e = reinterpret_cast<const clap_event_note_t *>(h);
            if (e->key < 0 || e->key > 127) break;
            const int ch = e->channel >= 0 && e->channel < 16 ? e->channel : 0;
            int vel = static_cast<int>(std::lround(std::clamp(e->velocity, 0.0, 1.0) * 127.0));
            uint8_t msg[3];
            if (h->type == CLAP_EVENT_NOTE_ON) {
                msg[0] = static_cast<uint8_t>(0x90 | ch);
                vel = std::max(vel, 1);
            } else {
                msg[0] = static_cast<uint8_t>(0x80 | ch);
            }
            msg[1] = static_cast<uint8_t>(e->key);
            msg[2] = static_cast<uint8_t>(vel);
            rt_->push_midi_bytes(frame, msg, 3);
            break;
        }
        default: break;
    }
}

clap_process_status Engine::process(const clap_process_t *p) {
    const uint32_t frames = p->frames_count;
    float *out_l = nullptr, *out_r = nullptr, *tape_out = nullptr;
    if (p->audio_outputs_count > 0 && p->audio_outputs[0].data32) {
        out_l = p->audio_outputs[0].data32[0];
        out_r = p->audio_outputs[0].channel_count > 1 ? p->audio_outputs[0].data32[1] : nullptr;
    }
    if (p->audio_outputs_count > 1 && p->audio_outputs[1].data32) tape_out = p->audio_outputs[1].data32[0];
    const float *tape_in = (p->audio_inputs_count > 0 && p->audio_inputs[0].data32) ? p->audio_inputs[0].data32[0] : nullptr;
    const uint32_t n_in = p->in_events ? p->in_events->size(p->in_events) : 0;

    std::unique_lock<std::mutex> lk(rt_mu_, std::try_to_lock);
    if (!lk.owns_lock() || !rom_loaded_.load(std::memory_order_relaxed)) {
        for (float *b : {out_l, out_r, tape_out})
            if (b) std::memset(b, 0, sizeof(float) * frames);
        flush_params(p->in_events, p->out_events);
        return CLAP_PROCESS_CONTINUE;
    }

    sysex_out_.clear();
    out_events_ = p->out_events;
    emit_param_changes(p->out_events);

    for (uint32_t t = key_tail_.load(std::memory_order_relaxed), h = key_head_.load(std::memory_order_acquire);
         t != h; ++t) {
        const KeyCmd &k = keys_[t % kKeyQueue];
        rt_->panel_set_held(k.key, k.down != 0);
        key_tail_.store(t + 1, std::memory_order_release);
    }

    run_bridge(p);

    const float click = click_level_.load(std::memory_order_relaxed);
    uint32_t ev = 0, br = 0;
    for (uint32_t c0 = 0; c0 < frames;) {
        const uint32_t c1 = std::min(frames, c0 + kChunk);
        for (;;) {
            const clap_event_header_t *h = ev < n_in ? p->in_events->get(p->in_events, ev) : nullptr;
            const uint32_t hf = h ? h->time : UINT32_MAX;
            const uint32_t bf = br < bridge_n_ ? bridge_[br].frame : UINT32_MAX;
            if (std::min(hf, bf) >= c1) break;
            if (bf <= hf) {
                const BridgeMsg &m = bridge_[br++];
                rt_->push_midi_bytes(m.frame > c0 ? m.frame - c0 : 0, m.data, m.size);
            } else {
                handle_event(h, c0);
                ++ev;
            }
        }
        const uint32_t n = c1 - c0;
        rt_->set_audio_inputs(tape_in ? tape_in + c0 : nullptr, tape_in ? tape_in + c0 : nullptr, n);
        rt_->process_block(n);
        rt_->copy_audio_outputs(scratch_l_.data(), scratch_r_.data(), n);
        for (uint32_t i = 0; i < n; ++i) {
            const float c = scratch_l_[i] * click;
            if (out_l) out_l[c0 + i] = c;
            if (out_r) out_r[c0 + i] = c;
            if (tape_out) tape_out[c0 + i] = scratch_r_[i];
        }
        out_frame_base_ = c0;
        rt_->drain_midi_out(&Engine::on_midi_out, this);
        c0 = c1;
    }
    for (; ev < n_in; ++ev) handle_event(p->in_events->get(p->in_events, ev), frames);
    rt_->set_audio_inputs(nullptr, nullptr, 0);
    out_events_ = nullptr;
    publish_panel();
    return CLAP_PROCESS_CONTINUE;
}

}  // namespace mmt8
