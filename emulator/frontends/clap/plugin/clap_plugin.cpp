#include <clap/clap.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "engine.h"
#include "gui.h"
#include "log.h"

namespace mmt8 {
namespace {

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_NOTE_EFFECT,
                                 CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

/* Frozen plugin id. Do not change it. A different id does not open saved projects. */
const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION_INIT,
    "com.mmt8.emulator.stage2",
    "OpenMMT-8",
    "OpenMMT-8",
    "",
    "",
    "",
    "2.0.0",
    "MIDI sequencer. The built-in ROM is OpenMMT-8. This project is not affiliated with Alesis. "
    "Load another ROM only if you have the right to use it.",
    kFeatures,
};

struct Plugin {
    clap_plugin_t clap{};
    const clap_host_t *host = nullptr;
    const clap_host_params_t *host_params = nullptr;
    const clap_host_state_t *host_state = nullptr;
    const clap_host_gui_t *host_gui = nullptr;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<Gui> gui;
};

Plugin *self(const clap_plugin_t *p) { return static_cast<Plugin *>(p->plugin_data); }

// ---- audio ports ---------------------------------------------------------------------------

uint32_t audio_ports_count(const clap_plugin_t *, bool is_input) { return is_input ? 1 : 2; }

bool audio_ports_get(const clap_plugin_t *, uint32_t index, bool is_input, clap_audio_port_info_t *info) {
    std::memset(info, 0, sizeof(*info));
    info->in_place_pair = CLAP_INVALID_ID;
    if (is_input) {
        if (index != 0) return false;
        info->id = 10;
        std::snprintf(info->name, sizeof(info->name), "Tape In");
        info->channel_count = 1;
        info->port_type = CLAP_PORT_MONO;
        return true;
    }
    if (index == 0) {
        info->id = 0;
        std::snprintf(info->name, sizeof(info->name), "Click");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        return true;
    }
    if (index == 1) {
        info->id = 1;
        std::snprintf(info->name, sizeof(info->name), "Tape Out");
        info->channel_count = 1;
        info->port_type = CLAP_PORT_MONO;
        return true;
    }
    return false;
}

const clap_plugin_audio_ports_t kAudioPorts = {audio_ports_count, audio_ports_get};

// ---- note ports ----------------------------------------------------------------------------

uint32_t note_ports_count(const clap_plugin_t *, bool) { return 1; }

bool note_ports_get(const clap_plugin_t *, uint32_t index, bool is_input, clap_note_port_info_t *info) {
    if (index != 0) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = is_input ? 20 : 21;
    info->supported_dialects = is_input ? (CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_CLAP) : CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    std::snprintf(info->name, sizeof(info->name), is_input ? "MIDI In" : "MIDI Out");
    return true;
}

const clap_plugin_note_ports_t kNotePorts = {note_ports_count, note_ports_get};

// ---- params --------------------------------------------------------------------------------

uint32_t params_count(const clap_plugin_t *) { return 2; }

bool params_get_info(const clap_plugin_t *, uint32_t index, clap_param_info_t *info) {
    std::memset(info, 0, sizeof(*info));
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    info->min_value = 0.0;
    info->max_value = 1.0;
    switch (index) {
        case 0:
            info->id = kParamSync;
            info->flags |= CLAP_PARAM_IS_STEPPED;
            info->default_value = 1.0;
            std::snprintf(info->name, sizeof(info->name), "DAW Sync");
            return true;
        case 1:
            info->id = kParamClick;
            info->default_value = 0.8;
            std::snprintf(info->name, sizeof(info->name), "Click Level");
            return true;
        default: return false;
    }
}

bool params_get_value(const clap_plugin_t *p, clap_id id, double *value) {
    if (id < kParamSync || id > kParamLast) return false;
    *value = self(p)->engine->param(id);
    return true;
}

bool params_value_to_text(const clap_plugin_t *, clap_id id, double v, char *out, uint32_t size) {
    switch (id) {
        case kParamSync: std::snprintf(out, size, "%s", v >= 0.5 ? "On" : "Off"); return true;
        case kParamClick: std::snprintf(out, size, "%d%%", static_cast<int>(std::lround(v * 100.0))); return true;
        default: return false;
    }
}

bool params_text_to_value(const clap_plugin_t *, clap_id id, const char *text, double *v) {
    switch (id) {
        case kParamSync: {
            const bool on = !std::strncmp(text, "On", 2) || !std::strncmp(text, "on", 2) || !std::strncmp(text, "1", 1);
            *v = on ? 1.0 : 0.0;
            return true;
        }
        case kParamClick: *v = std::strtod(text, nullptr) / 100.0; return true;
        default: return false;
    }
}

void params_flush(const clap_plugin_t *p, const clap_input_events_t *in, const clap_output_events_t *out) {
    self(p)->engine->flush_params(in, out);
}

const clap_plugin_params_t kParams = {params_count, params_get_info, params_get_value, params_value_to_text,
                                      params_text_to_value, params_flush};

// ---- state ---------------------------------------------------------------------------------

bool state_save(const clap_plugin_t *p, const clap_ostream_t *stream) {
    const std::vector<uint8_t> chunk = self(p)->engine->save_state();
    size_t at = 0;
    while (at < chunk.size()) {
        const int64_t n = stream->write(stream, chunk.data() + at, chunk.size() - at);
        if (n <= 0) return false;
        at += static_cast<size_t>(n);
    }
    return true;
}

bool state_load(const clap_plugin_t *p, const clap_istream_t *stream) {
    std::vector<uint8_t> chunk;
    uint8_t buf[8192];
    for (;;) {
        const int64_t n = stream->read(stream, buf, sizeof(buf));
        if (n < 0) return false;
        if (n == 0) break;
        chunk.insert(chunk.end(), buf, buf + n);
        if (chunk.size() > (4u << 20)) return false;
    }
    Plugin *pl = self(p);
    const bool ok = pl->engine->load_state(chunk);
    if (ok && pl->host_params && pl->host_params->rescan) pl->host_params->rescan(pl->host, CLAP_PARAM_RESCAN_VALUES);
    return ok;
}

const clap_plugin_state_t kState = {state_save, state_load};

// ---- gui -----------------------------------------------------------------------------------

bool gui_is_api_supported(const clap_plugin_t *, const char *api, bool floating) {
    if (!Gui::supported() || floating || !api) return false;
#if defined(_WIN32)
    return std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
#elif defined(__APPLE__)
    return std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
#elif defined(__linux__)
    return std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
#else
    (void)api;
    return false;
#endif
}

bool gui_get_preferred_api(const clap_plugin_t *, const char **api, bool *floating) {
    if (!Gui::supported()) return false;
    *floating = false;
#if defined(_WIN32)
    *api = CLAP_WINDOW_API_WIN32;
    return true;
#elif defined(__APPLE__)
    *api = CLAP_WINDOW_API_COCOA;
    return true;
#elif defined(__linux__)
    *api = CLAP_WINDOW_API_X11;
    return true;
#else
    (void)api;
    return false;
#endif
}

bool gui_create(const clap_plugin_t *p, const char *api, bool floating) {
    if (!gui_is_api_supported(p, api, floating)) return false;
    Plugin *pl = self(p);
    GuiHost gh;
    gh.params_changed = [pl] {
        if (pl->host_params && pl->host_params->request_flush) pl->host_params->request_flush(pl->host);
    };
    gh.state_changed = [pl] {
        if (pl->host_state && pl->host_state->mark_dirty) pl->host_state->mark_dirty(pl->host);
    };
    gh.request_resize = [pl](uint32_t w, uint32_t h) {
        return pl->host_gui && pl->host_gui->request_resize && pl->host_gui->request_resize(pl->host, w, h);
    };
    pl->gui = Gui::create(*pl->engine, std::move(gh));
    return pl->gui != nullptr;
}

void gui_destroy(const clap_plugin_t *p) { self(p)->gui.reset(); }

bool gui_set_scale(const clap_plugin_t *p, double scale) {
    Plugin *pl = self(p);
    if (!pl->gui) return false;
    pl->gui->set_scale(scale);
    return true;
}

uint32_t clamp_width(uint32_t w) { return std::max(Gui::kMinWidth, std::min(Gui::kMaxWidth, w)); }

bool gui_get_size(const clap_plugin_t *p, uint32_t *width, uint32_t *height) {
    Plugin *pl = self(p);
    uint32_t w = pl->engine->gui_width();
    if (!w) w = static_cast<uint32_t>(Gui::kDefaultWidth * (pl->gui ? pl->gui->scale() : 1.0) + 0.5);
    w = clamp_width(w);
    *width = w;
    *height = Gui::height_for(w);
    return true;
}

bool gui_can_resize(const clap_plugin_t *) { return true; }

bool gui_get_resize_hints(const clap_plugin_t *, clap_gui_resize_hints_t *hints) {
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically = true;
    hints->preserve_aspect_ratio = true;
    hints->aspect_ratio_width = static_cast<uint32_t>(kPanelW);
    hints->aspect_ratio_height = static_cast<uint32_t>(kPanelH);
    return true;
}

bool gui_adjust_size(const clap_plugin_t *, uint32_t *width, uint32_t *height) {
    const uint32_t by_height = static_cast<uint32_t>(static_cast<float>(*height) * kPanelW / kPanelH);
    const uint32_t w = clamp_width(std::min(*width, by_height));
    *width = w;
    *height = Gui::height_for(w);
    return true;
}

bool gui_set_size(const clap_plugin_t *p, uint32_t width, uint32_t height) {
    Plugin *pl = self(p);
    if (!pl->gui) return false;
    pl->gui->set_size(width, height);
    pl->engine->set_gui_width(width);
    return true;
}

bool gui_set_parent(const clap_plugin_t *p, const clap_window_t *window) {
    Plugin *pl = self(p);
    if (!pl->gui || !window) return false;
    uint32_t w = 0, h = 0;
    gui_get_size(p, &w, &h);
    pl->gui->set_size(w, h);
#if defined(_WIN32)
    return pl->gui->set_parent(window->win32);
#elif defined(__APPLE__)
    return pl->gui->set_parent(window->cocoa);
#elif defined(__linux__)
    return pl->gui->set_parent(reinterpret_cast<void *>(static_cast<uintptr_t>(window->x11)));
#else
    (void)window;
    return false;
#endif
}

bool gui_set_transient(const clap_plugin_t *, const clap_window_t *) { return false; }
void gui_suggest_title(const clap_plugin_t *, const char *) {}

bool gui_show(const clap_plugin_t *p) {
    Plugin *pl = self(p);
    if (!pl->gui) return false;
    pl->gui->show(true);
    return true;
}

bool gui_hide(const clap_plugin_t *p) {
    Plugin *pl = self(p);
    if (!pl->gui) return false;
    pl->gui->show(false);
    return true;
}

const clap_plugin_gui_t kGui = {gui_is_api_supported, gui_get_preferred_api, gui_create,       gui_destroy,
                                gui_set_scale,        gui_get_size,          gui_can_resize,   gui_get_resize_hints,
                                gui_adjust_size,      gui_set_size,          gui_set_parent,   gui_set_transient,
                                gui_suggest_title,    gui_show,              gui_hide};

// ---- plugin --------------------------------------------------------------------------------

bool plugin_init(const clap_plugin_t *p) {
    Plugin *pl = self(p);
    auto ext = [pl](const char *id) { return pl->host->get_extension ? pl->host->get_extension(pl->host, id) : nullptr; };
    pl->host_params = static_cast<const clap_host_params_t *>(ext(CLAP_EXT_PARAMS));
    pl->host_state = static_cast<const clap_host_state_t *>(ext(CLAP_EXT_STATE));
    pl->host_gui = static_cast<const clap_host_gui_t *>(ext(CLAP_EXT_GUI));
    try {
        pl->engine = std::make_unique<Engine>();
    } catch (...) {
        log("plugin: engine construction failed");
        return false;
    }
    /* MMT8_CLAP_DATA_DIR moves the store (empty turns it off); the host replaces the ROM and
     * memory restored here when it loads a project. */
    const char *data_dir = std::getenv("MMT8_CLAP_DATA_DIR");
    pl->engine->attach_store(data_dir ? std::string(data_dir) : module_dir() + "mmt8_clap_data");
    pl->engine->restore_last();
    log("plugin: init in %s %s", pl->host->name ? pl->host->name : "?", pl->host->version ? pl->host->version : "");
    return true;
}

void plugin_destroy(const clap_plugin_t *p) {
    Plugin *pl = self(p);
    pl->gui.reset();
    pl->engine.reset();
    delete pl;
}

bool plugin_activate(const clap_plugin_t *p, double sample_rate, uint32_t, uint32_t max_frames) {
    return self(p)->engine->activate(sample_rate, max_frames);
}

void plugin_deactivate(const clap_plugin_t *p) { self(p)->engine->deactivate(); }
bool plugin_start_processing(const clap_plugin_t *) { return true; }
void plugin_stop_processing(const clap_plugin_t *) {}
void plugin_reset(const clap_plugin_t *) {}

clap_process_status plugin_process(const clap_plugin_t *p, const clap_process_t *process) {
    return self(p)->engine->process(process);
}

const void *plugin_get_extension(const clap_plugin_t *, const char *id) {
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &kAudioPorts;
    if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &kNotePorts;
    if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &kParams;
    if (!std::strcmp(id, CLAP_EXT_STATE)) return &kState;
    if (!std::strcmp(id, CLAP_EXT_GUI) && Gui::supported()) return &kGui;
    return nullptr;
}

void plugin_on_main_thread(const clap_plugin_t *) {}

// ---- factory and entry ---------------------------------------------------------------------

uint32_t factory_count(const clap_plugin_factory_t *) { return 1; }

const clap_plugin_descriptor_t *factory_descriptor(const clap_plugin_factory_t *, uint32_t index) {
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t *factory_create(const clap_plugin_factory_t *, const clap_host_t *host, const char *id) {
    if (!host || !id || std::strcmp(id, kDescriptor.id) != 0 || !clap_version_is_compatible(host->clap_version))
        return nullptr;
    auto *pl = new Plugin();
    pl->host = host;
    pl->clap.desc = &kDescriptor;
    pl->clap.plugin_data = pl;
    pl->clap.init = plugin_init;
    pl->clap.destroy = plugin_destroy;
    pl->clap.activate = plugin_activate;
    pl->clap.deactivate = plugin_deactivate;
    pl->clap.start_processing = plugin_start_processing;
    pl->clap.stop_processing = plugin_stop_processing;
    pl->clap.reset = plugin_reset;
    pl->clap.process = plugin_process;
    pl->clap.get_extension = plugin_get_extension;
    pl->clap.on_main_thread = plugin_on_main_thread;
    return &pl->clap;
}

const clap_plugin_factory_t kFactory = {factory_count, factory_descriptor, factory_create};

bool entry_init(const char *) {
    log_start(module_dir() + "mmt8_clap_runtime.log");
    log("entry: OpenMMT-8 CLAP %s loaded from %s", kDescriptor.version, module_dir().c_str());
    return true;
}

void entry_deinit() {
    log("entry: unloading");
    log_stop();
}

const void *entry_get_factory(const char *id) {
    return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}

}  // namespace
}  // namespace mmt8

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, mmt8::entry_init,
                                                               mmt8::entry_deinit, mmt8::entry_get_factory};
