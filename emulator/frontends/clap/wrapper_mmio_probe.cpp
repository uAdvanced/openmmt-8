#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>
#include <array>
#include <vector>

#include "mmt_emu/plugin_port.hpp"
#include "wrapper_stub.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: wrapper_mmio_probe <rom-path> [blocks] [frames] [press-spec] [midi-spec] [sync-spec] [serial-spec] [ffxx-spec] [transport-spec] [reset-pc-hex] [sfr-spec] [iram-spec] [xram-spec] [pc-spec] [hold-spec] [xpeek-spec] [ipeek-spec] [serial-stream-spec]\n";
        std::cerr << "press-spec format: block:button,block:button (buttons 0..47)\n";
        std::cerr << "hold-spec format: start-end:button,start-end:button (inclusive block range)\n";
        std::cerr << "midi-spec format: block:byte-byte-byte;block:byte (hex bytes)\n";
        std::cerr << "example midi-spec: 300:FA;320:F8;330:90-3C-64;340:80-3C-00\n";
        std::cerr << "sync-spec format: block:START,block:CLOCK,block:STOP (tokens START|CONT|STOP|CLOCK)\n";
        std::cerr << "serial-spec format: block:bytehex,block:bytehex\n";
        std::cerr << "serial-stream-spec format: start[@stride]:byte-byte-byte;... (hex bytes, UART RI path)\n";
        std::cerr << "example serial-stream-spec: 2806:90-3C-40;2826:80-3C-00\n";
        std::cerr << "ffxx-spec format: block:FF0E=02,block:FF1A=01\n";
        std::cerr << "transport-spec format: block:play-record-tempo (e.g., 500:1-0-120,900:0-0-120)\n";
        std::cerr << "sfr-spec format: block:98=51,block:99=3A (hex SFR addr/value)\n";
        std::cerr << "iram-spec format: block:5C=03,block:20=10\n";
        std::cerr << "xram-spec format: block:00FF=01,block:04EE=7F\n";
        std::cerr << "pc-spec format: block:7757,block:7518\n";
        std::cerr << "xpeek-spec format: 0011,0012,005A (hex xram addresses to log on change)\n";
        std::cerr << "ipeek-spec format: 25,26,27,28 (hex iram addresses to log on change)\n";
        std::cerr << "tape-spec format: startBlock:byte-byte-byte (P3.2 audio, MSB first)\n";
        return 2;
    }

    const std::string rom_path = argv[1];
    const uint32_t max_blocks = (argc >= 3) ? static_cast<uint32_t>(std::stoul(argv[2])) : 20000u;
    const uint32_t frames = (argc >= 4) ? static_cast<uint32_t>(std::stoul(argv[3])) : 128u;
    const std::string press_spec = (argc >= 5) ? argv[4] : "";
    const std::string midi_spec = (argc >= 6) ? argv[5] : "";
    const std::string sync_spec = (argc >= 7) ? argv[6] : "";
    const std::string serial_spec = (argc >= 8) ? argv[7] : "";
    const std::string ffxx_spec = (argc >= 9) ? argv[8] : "";
    const std::string transport_spec = (argc >= 10) ? argv[9] : "";
    const std::string reset_pc_hex = (argc >= 11) ? argv[10] : "";
    const std::string sfr_spec = (argc >= 12) ? argv[11] : "";
    const std::string iram_spec = (argc >= 13) ? argv[12] : "";
    const std::string xram_spec = (argc >= 14) ? argv[13] : "";
    const std::string pc_spec = (argc >= 15) ? argv[14] : "";
    const std::string hold_spec = (argc >= 16) ? argv[15] : "";
    const std::string xpeek_spec = (argc >= 17) ? argv[16] : "";
    const std::string ipeek_spec = (argc >= 18) ? argv[17] : "";
    const std::string serial_stream_spec = (argc >= 19) ? argv[18] : "";
    const std::string tape_spec = (argc >= 20) ? argv[19] : "";

    std::vector<std::pair<uint32_t, uint32_t>> presses;
    struct HoldRange { uint32_t start; uint32_t end; uint32_t button; };
    std::vector<HoldRange> hold_ranges;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> midi_events;
    std::vector<std::pair<uint32_t, uint8_t>> sync_events;
    std::vector<std::pair<uint32_t, uint8_t>> serial_events;
    std::vector<std::pair<uint32_t, uint8_t>> uart_events;
    std::vector<std::pair<uint32_t, std::pair<uint16_t, uint8_t>>> ffxx_events;
    std::vector<std::pair<uint32_t, mmt_emu::HostTransportState>> transport_events;
    std::vector<std::pair<uint32_t, std::pair<uint8_t, uint8_t>>> sfr_events;
    std::vector<std::pair<uint32_t, std::pair<uint8_t, uint8_t>>> iram_events;
    std::vector<std::pair<uint32_t, std::pair<uint16_t, uint8_t>>> xram_events;
    std::vector<std::pair<uint32_t, uint16_t>> pc_events;
    std::vector<uint16_t> xpeek_addrs;
    std::vector<uint8_t> ipeek_addrs;
    if (!press_spec.empty()) {
        std::stringstream ss(press_spec);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (tok.empty()) continue;
            const size_t colon = tok.find(':');
            if (colon == std::string::npos) continue;
            const uint32_t block = static_cast<uint32_t>(std::stoul(tok.substr(0, colon)));
            const uint32_t button = static_cast<uint32_t>(std::stoul(tok.substr(colon + 1)));
            if (button <= 47u) presses.push_back({block, button});
        }
    }
    if (!hold_spec.empty()) {
        std::stringstream ss(hold_spec);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (tok.empty()) continue;
            const size_t colon = tok.find(':');
            const size_t dash = tok.find('-');
            if (colon == std::string::npos || dash == std::string::npos || dash >= colon) continue;
            const uint32_t start = static_cast<uint32_t>(std::stoul(tok.substr(0, dash)));
            const uint32_t end = static_cast<uint32_t>(std::stoul(tok.substr(dash + 1, colon - (dash + 1))));
            const uint32_t button = static_cast<uint32_t>(std::stoul(tok.substr(colon + 1)));
            if (button <= 47u && start <= end) hold_ranges.push_back(HoldRange{start, end, button});
        }
    }
    if (!midi_spec.empty()) {
        std::stringstream ss(midi_spec);
        std::string tok;
        while (std::getline(ss, tok, ';')) {
            if (tok.empty()) continue;
            const size_t colon = tok.find(':');
            if (colon == std::string::npos) continue;
            const uint32_t block = static_cast<uint32_t>(std::stoul(tok.substr(0, colon)));
            const std::string bytes = tok.substr(colon + 1);
            std::stringstream bs(bytes);
            std::string byte_tok;
            std::vector<uint8_t> data;
            while (std::getline(bs, byte_tok, '-')) {
                if (byte_tok.empty()) continue;
                const unsigned v = static_cast<unsigned>(std::stoul(byte_tok, nullptr, 16));
                data.push_back(static_cast<uint8_t>(v & 0xFFu));
            }
            if (!data.empty()) midi_events.push_back({block, std::move(data)});
        }
    }
    if (!sync_spec.empty()) {
        std::stringstream ss(sync_spec);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (tok.empty()) continue;
            const size_t colon = tok.find(':');
            if (colon == std::string::npos) continue;
            const uint32_t block = static_cast<uint32_t>(std::stoul(tok.substr(0, colon)));
            std::string kind = tok.substr(colon + 1);
            std::transform(kind.begin(), kind.end(), kind.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            uint8_t status = 0u;
            if (kind == "START") status = 0xFAu;
            else if (kind == "CONT") status = 0xFBu;
            else if (kind == "STOP") status = 0xFCu;
            else if (kind == "CLOCK") status = 0xF8u;
            if (status != 0u) sync_events.push_back({block, status});
        }
    }
    if (!serial_spec.empty()) {
        std::stringstream ss(serial_spec);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (tok.empty()) continue;
            const size_t colon = tok.find(':');
            if (colon == std::string::npos) continue;
            const uint32_t block = static_cast<uint32_t>(std::stoul(tok.substr(0, colon)));
            const unsigned v = static_cast<unsigned>(std::stoul(tok.substr(colon + 1), nullptr, 16));
            serial_events.push_back({block, static_cast<uint8_t>(v & 0xFFu)});
        }
    }
    if (!serial_stream_spec.empty()) {
        std::stringstream ss(serial_stream_spec);
        std::string tok;
        while (std::getline(ss, tok, ';')) {
            if (tok.empty()) continue;
            const size_t colon = tok.find(':');
            if (colon == std::string::npos) continue;
            const std::string time_tok = tok.substr(0, colon);
            uint32_t start = 0u;
            uint32_t stride = 1u;
            const size_t at = time_tok.find('@');
            if (at == std::string::npos) {
                start = static_cast<uint32_t>(std::stoul(time_tok));
            } else {
                start = static_cast<uint32_t>(std::stoul(time_tok.substr(0, at)));
                stride = static_cast<uint32_t>(std::stoul(time_tok.substr(at + 1)));
                if (stride == 0u) stride = 1u;
            }
            std::stringstream bs(tok.substr(colon + 1));
            std::string byte_tok;
            uint32_t idx = 0u;
            while (std::getline(bs, byte_tok, '-')) {
                if (byte_tok.empty()) continue;
                const unsigned v = static_cast<unsigned>(std::stoul(byte_tok, nullptr, 16));
                uart_events.push_back({start + (idx * stride), static_cast<uint8_t>(v & 0xFFu)});
                ++idx;
            }
        }
    }
    std::vector<float> tape_audio;
    uint32_t tape_start_block = 0u;
    if (!tape_spec.empty()) {
        const size_t colon = tape_spec.find(':');
        if (colon != std::string::npos) {
            tape_start_block = static_cast<uint32_t>(std::stoul(tape_spec.substr(0, colon)));
            auto emit_bit = [&tape_audio](int bit) {
                const int n = bit != 0 ? 24 : 10;
                for (int i = 0; i < n; ++i) tape_audio.push_back(-1.0f);
                for (int i = 0; i < n; ++i) tape_audio.push_back(1.0f);
            };
            for (int i = 0; i < 48; ++i) tape_audio.push_back(1.0f);
            std::stringstream bs(tape_spec.substr(colon + 1));
            std::string byte_tok;
            while (std::getline(bs, byte_tok, '-')) {
                if (byte_tok.empty()) continue;
                const unsigned v = static_cast<unsigned>(std::stoul(byte_tok, nullptr, 16));
                for (int bit = 7; bit >= 0; --bit) {
                    emit_bit((v >> bit) & 1);
                }
            }
            for (int i = 0; i < 48; ++i) tape_audio.push_back(1.0f);
            std::cout << "tape audio samples=" << tape_audio.size()
                      << " start=" << tape_start_block << "\n";
        }
    }
    // Tape input from a raw float32 file (e.g. a captured MMT8_PROBE_TAPE_OUT), from block N.
    if (const char *tin = std::getenv("MMT8_PROBE_TAPE_IN")) {
        if (FILE *f = std::fopen(tin, "rb")) {
            float v;
            tape_audio.clear();
            while (std::fread(&v, sizeof v, 1, f) == 1) tape_audio.push_back(v);
            std::fclose(f);
            const char *blk = std::getenv("MMT8_PROBE_TAPE_IN_BLOCK");
            tape_start_block = blk ? static_cast<uint32_t>(std::stoul(blk)) : 0u;
            std::cout << "tape audio samples=" << tape_audio.size() << " start=" << tape_start_block << "\n";
        }
    }
    FILE *tape_out = nullptr;
    if (const char *tout = std::getenv("MMT8_PROBE_TAPE_OUT")) tape_out = std::fopen(tout, "wb");
    if (!ffxx_spec.empty()) {
        std::stringstream ss(ffxx_spec);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (tok.empty()) continue;
            const size_t colon = tok.find(':');
            const size_t eq = tok.find('=');
            if (colon == std::string::npos || eq == std::string::npos || eq <= colon) continue;
            const uint32_t block = static_cast<uint32_t>(std::stoul(tok.substr(0, colon)));
            const uint16_t addr = static_cast<uint16_t>(std::stoul(tok.substr(colon + 1, eq - (colon + 1)), nullptr, 16));
            const uint8_t val = static_cast<uint8_t>(std::stoul(tok.substr(eq + 1), nullptr, 16) & 0xFFu);
            ffxx_events.push_back({block, {addr, val}});
        }
    }
    if (!transport_spec.empty()) {
        std::stringstream ss(transport_spec);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (tok.empty()) continue;
            const size_t colon = tok.find(':');
            if (colon == std::string::npos) continue;
            const uint32_t block = static_cast<uint32_t>(std::stoul(tok.substr(0, colon)));
            std::string payload = tok.substr(colon + 1);
            std::replace(payload.begin(), payload.end(), '-', ' ');
            std::stringstream ps(payload);
            int play = 0;
            int rec = 0;
            double tempo = 120.0;
            ps >> play >> rec >> tempo;
            mmt_emu::HostTransportState t{};
            t.playing = (play != 0);
            t.recording = (rec != 0);
            t.tempo_bpm = tempo;
            transport_events.push_back({block, t});
        }
    }
    if (!sfr_spec.empty()) {
        std::stringstream ss(sfr_spec);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (tok.empty()) continue;
            const size_t colon = tok.find(':');
            const size_t eq = tok.find('=');
            if (colon == std::string::npos || eq == std::string::npos || eq <= colon) continue;
            const uint32_t block = static_cast<uint32_t>(std::stoul(tok.substr(0, colon)));
            const uint8_t addr = static_cast<uint8_t>(std::stoul(tok.substr(colon + 1, eq - (colon + 1)), nullptr, 16) & 0xFFu);
            const uint8_t val = static_cast<uint8_t>(std::stoul(tok.substr(eq + 1), nullptr, 16) & 0xFFu);
            sfr_events.push_back({block, {addr, val}});
        }
    }
    if (!xram_spec.empty()) {
        std::stringstream ss(xram_spec);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (tok.empty()) continue;
            const size_t colon = tok.find(':');
            const size_t eq = tok.find('=');
            if (colon == std::string::npos || eq == std::string::npos || eq <= colon) continue;
            const uint32_t block = static_cast<uint32_t>(std::stoul(tok.substr(0, colon)));
            const uint16_t addr = static_cast<uint16_t>(std::stoul(tok.substr(colon + 1, eq - (colon + 1)), nullptr, 16) & 0xFFFFu);
            const uint8_t val = static_cast<uint8_t>(std::stoul(tok.substr(eq + 1), nullptr, 16) & 0xFFu);
            xram_events.push_back({block, {addr, val}});
        }
    }
    if (!iram_spec.empty()) {
        std::stringstream ss(iram_spec);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (tok.empty()) continue;
            const size_t colon = tok.find(':');
            const size_t eq = tok.find('=');
            if (colon == std::string::npos || eq == std::string::npos || eq <= colon) continue;
            const uint32_t block = static_cast<uint32_t>(std::stoul(tok.substr(0, colon)));
            const uint8_t addr = static_cast<uint8_t>(std::stoul(tok.substr(colon + 1, eq - (colon + 1)), nullptr, 16) & 0xFFu);
            const uint8_t val = static_cast<uint8_t>(std::stoul(tok.substr(eq + 1), nullptr, 16) & 0xFFu);
            iram_events.push_back({block, {addr, val}});
        }
    }
    if (!pc_spec.empty()) {
        std::stringstream ss(pc_spec);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (tok.empty()) continue;
            const size_t colon = tok.find(':');
            if (colon == std::string::npos) continue;
            const uint32_t block = static_cast<uint32_t>(std::stoul(tok.substr(0, colon)));
            const uint16_t pc = static_cast<uint16_t>(std::stoul(tok.substr(colon + 1), nullptr, 16) & 0xFFFFu);
            pc_events.push_back({block, pc});
        }
    }
    if (!xpeek_spec.empty()) {
        std::stringstream ss(xpeek_spec);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (tok.empty()) continue;
            const uint16_t addr = static_cast<uint16_t>(std::stoul(tok, nullptr, 16) & 0xFFFFu);
            xpeek_addrs.push_back(addr);
        }
    }
    if (!ipeek_spec.empty()) {
        std::stringstream ss(ipeek_spec);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (tok.empty()) continue;
            const uint8_t addr = static_cast<uint8_t>(std::stoul(tok, nullptr, 16) & 0x7Fu);
            ipeek_addrs.push_back(addr);
        }
    }

    mmt_emu::IPluginRuntime* runtime = mmt8_clap_wrapper_create();
    if (runtime == nullptr) {
        std::cerr << "create failed\n";
        return 1;
    }
    if (!runtime->load_rom(rom_path)) {
        std::cerr << "load_rom failed: " << rom_path << "\n";
        mmt8_clap_wrapper_destroy(runtime);
        return 1;
    }
    const bool panel_write_log = std::getenv("MMT8_PROBE_PANEL_WRITES") != nullptr;
    mmt8_clap_wrapper_debug_enable_panel_write_log(runtime, panel_write_log);
    // MMT8_PROBE_PC_WATCH=hex,hex,...: log "pchit" with the machine cycle whenever one of these PCs executes.
    if (const char *watch = std::getenv("MMT8_PROBE_PC_WATCH")) {
        std::stringstream ss(watch);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (!tok.empty()) mmt8_clap_wrapper_debug_watch_pc(runtime, static_cast<uint16_t>(std::stoul(tok, nullptr, 16)));
        }
    }
    if (!reset_pc_hex.empty()) {
        const uint16_t pc = static_cast<uint16_t>(std::stoul(reset_pc_hex, nullptr, 16));
        const bool ok = mmt8_clap_wrapper_debug_set_pc(runtime, pc);
        std::cout << "inject reset_pc=0x" << std::hex << static_cast<unsigned>(pc) << std::dec
                  << " ok=" << (ok ? 1 : 0) << "\n";
    }

    runtime->set_sample_rate(48000.0);
    runtime->set_process_mode(mmt_emu::ProcessMode::Realtime);
    mmt_emu::HostTransportState t{};
    t.playing = false;
    t.tempo_bpm = 120.0;
    runtime->set_transport(t);

    uint64_t prev_xw = 0;
    uint64_t prev_cmd = 0;
    uint64_t prev_data = 0;
    uint64_t prev_04ee = 0;
    uint64_t prev_ff1b = 0;
    uint64_t prev_ff1c = 0;
    uint64_t prev_serial_reads = 0;
    uint64_t prev_serial_irq_asserts = 0;
    uint64_t prev_serial_irq_clears = 0;
    uint64_t prev_xreads = 0;
    uint64_t prev_ff06_reads = 0;
    uint64_t prev_isr_int0_hits = 0;
    uint64_t prev_isr_timer0_hits = 0;
    uint64_t prev_isr_int1_hits = 0;
    uint64_t prev_isr_timer1_hits = 0;
    uint64_t prev_isr_serial_hits = 0;
    uint64_t prev_isr_timer2_hits = 0;
    uint32_t prev_phase = 0;
    bool printed_lcd = false;
    bool prev_panel_playing = false;
    bool prev_panel_record_armed = false;
    uint8_t prev_panel_ff02 = 0u;
    uint8_t prev_panel_ff04 = 0u;
    uint8_t prev_panel_p3 = 0u;
    uint8_t prev_panel_p3_min = 0xffu;
    uint8_t prev_panel_p3_max = 0x00u;
    uint8_t prev_track_latch = 0u;
    uint8_t prev_button_latch = 0u;
    uint8_t prev_track_observed = 0u;
    uint8_t prev_button_observed = 0u;
    std::vector<bool> hold_active(48u, false);
    int32_t prev_scan_bank = -2;
    std::array<uint8_t, 6> prev_edge_shadow{};
    std::array<uint8_t, 6> prev_raw_shadow{};
    std::vector<uint8_t> prev_xpeek_values(xpeek_addrs.size(), 0u);
    std::vector<bool> prev_xpeek_valid(xpeek_addrs.size(), false);
    std::vector<uint8_t> prev_ipeek_values(ipeek_addrs.size(), 0u);
    std::vector<bool> prev_ipeek_valid(ipeek_addrs.size(), false);

    for (uint32_t b = 0; b < max_blocks; ++b) {
        /* OR all ranges for the same button so scenarios can use disjoint holds (e.g. two PAGE taps). */
        for (uint32_t button = 0; button < 48u; ++button) {
            bool want = false;
            for (const auto &entry : hold_ranges) {
                if (entry.button == button && b >= entry.start && b <= entry.end) {
                    want = true;
                    break;
                }
            }
            if (hold_active[button] == want) {
                continue;
            }
            hold_active[button] = want;
            runtime->panel_set_held(button, want);
            std::cout << "inject block=" << b << " hold button=" << button << " state=" << (want ? 1 : 0) << "\n";
        }
        for (const auto &entry : presses) {
            if (entry.first == b) {
                runtime->panel_press(entry.second);
                std::cout << "inject block=" << b << " button=" << entry.second << "\n";
            }
        }
        for (const auto &entry : midi_events) {
            if (entry.first == b) {
                mmt_emu::MidiMessage msg{};
                msg.data = entry.second;
                runtime->push_midi_in(msg);
                std::cout << "inject block=" << b << " midi=";
                for (size_t i = 0; i < entry.second.size(); ++i) {
                    if (i != 0) std::cout << "-";
                    std::cout << std::hex << static_cast<unsigned>(entry.second[i]) << std::dec;
                }
                std::cout << "\n";
            }
        }
        for (const auto &entry : sync_events) {
            if (entry.first == b) {
                mmt_emu::MidiMessage msg{};
                msg.data = {entry.second};
                runtime->push_midi_in(msg);
                std::cout << "inject block=" << b << " sync=0x" << std::hex << static_cast<unsigned>(entry.second) << std::dec << "\n";
            }
        }
        for (const auto &entry : serial_events) {
            if (entry.first == b) {
                const bool ok = mmt8_clap_wrapper_debug_serial_rx(runtime, entry.second);
                std::cout << "inject block=" << b << " serial=0x" << std::hex << static_cast<unsigned>(entry.second)
                          << std::dec << " ok=" << (ok ? 1 : 0) << "\n";
            }
        }
        for (const auto &entry : uart_events) {
            if (entry.first == b) {
                const bool ok = mmt8_clap_wrapper_debug_uart_rx(runtime, entry.second);
                std::cout << "inject block=" << b << " uart=0x" << std::hex << static_cast<unsigned>(entry.second)
                          << std::dec << " ok=" << (ok ? 1 : 0) << "\n";
            }
        }
        for (const auto &entry : ffxx_events) {
            if (entry.first == b) {
                const bool ok = mmt8_clap_wrapper_debug_set_ffxx(runtime, entry.second.first, entry.second.second);
                std::cout << "inject block=" << b << " ffxx=0x" << std::hex
                          << static_cast<unsigned>(entry.second.first) << "=0x"
                          << static_cast<unsigned>(entry.second.second) << std::dec
                          << " ok=" << (ok ? 1 : 0) << "\n";
            }
        }
        for (const auto &entry : transport_events) {
            if (entry.first == b) {
                runtime->set_transport(entry.second);
                std::cout << "inject block=" << b
                          << " transport play=" << (entry.second.playing ? 1 : 0)
                          << " rec=" << (entry.second.recording ? 1 : 0)
                          << " tempo=" << entry.second.tempo_bpm << "\n";
            }
        }
        for (const auto &entry : sfr_events) {
            if (entry.first == b) {
                const bool ok = mmt8_clap_wrapper_debug_set_sfr(runtime, entry.second.first, entry.second.second);
                std::cout << "inject block=" << b
                          << " sfr=0x" << std::hex << static_cast<unsigned>(entry.second.first)
                          << "=0x" << static_cast<unsigned>(entry.second.second) << std::dec
                          << " ok=" << (ok ? 1 : 0) << "\n";
            }
        }
        for (const auto &entry : iram_events) {
            if (entry.first == b) {
                const bool ok = mmt8_clap_wrapper_debug_set_iram(runtime, entry.second.first, entry.second.second);
                std::cout << "inject block=" << b
                          << " iram=0x" << std::hex << static_cast<unsigned>(entry.second.first)
                          << "=0x" << static_cast<unsigned>(entry.second.second) << std::dec
                          << " ok=" << (ok ? 1 : 0) << "\n";
            }
        }
        for (const auto &entry : xram_events) {
            if (entry.first == b) {
                const bool ok = mmt8_clap_wrapper_debug_set_xram(runtime, entry.second.first, entry.second.second);
                std::cout << "inject block=" << b
                          << " xram=0x" << std::hex << static_cast<unsigned>(entry.second.first)
                          << "=0x" << static_cast<unsigned>(entry.second.second) << std::dec
                          << " ok=" << (ok ? 1 : 0) << "\n";
            }
        }
        for (const auto &entry : pc_events) {
            if (entry.first == b) {
                const bool ok = mmt8_clap_wrapper_debug_set_pc(runtime, entry.second);
                std::cout << "inject block=" << b << " pc=0x" << std::hex
                          << static_cast<unsigned>(entry.second) << std::dec
                          << " ok=" << (ok ? 1 : 0) << "\n";
            }
        }
        if (!tape_audio.empty() && b >= tape_start_block) {
            const uint32_t base = (b - tape_start_block) * frames;
            if (base < tape_audio.size()) {
                const uint32_t avail = static_cast<uint32_t>(tape_audio.size() - base);
                const uint32_t n = avail < frames ? avail : frames;
                runtime->set_audio_inputs(tape_audio.data() + base, tape_audio.data() + base, n);
            } else {
                runtime->set_audio_inputs(nullptr, nullptr, 0u);
            }
        }
        runtime->process_block(frames);
        {
            uint64_t hit_cycles[256];
            uint16_t hit_pcs[256];
            uint8_t hit_sps[256];
            uint32_t n;
            while ((n = mmt8_clap_wrapper_debug_drain_pc_hits(runtime, hit_cycles, hit_pcs, hit_sps, 256u)) != 0u) {
                for (uint32_t i = 0; i < n; ++i) {
                    std::cout << "pchit block=" << b << " cycle=" << hit_cycles[i] << " pc=0x" << std::hex
                              << hit_pcs[i] << " sp=0x" << static_cast<unsigned>(hit_sps[i]) << std::dec << "\n";
                }
            }
        }
        const auto midi_out = runtime->drain_midi_out();
        const auto rt = runtime->telemetry();
        for (size_t i = 0; i < xpeek_addrs.size(); ++i) {
            uint8_t value = 0u;
            if (!mmt8_clap_wrapper_debug_peek_xram(runtime, xpeek_addrs[i], &value)) {
                continue;
            }
            if (!prev_xpeek_valid[i] || prev_xpeek_values[i] != value) {
                std::cout << "xpeek block=" << b
                          << " addr=0x" << std::hex << static_cast<unsigned>(xpeek_addrs[i])
                          << " val=0x" << static_cast<unsigned>(value) << std::dec << "\n";
                prev_xpeek_values[i] = value;
                prev_xpeek_valid[i] = true;
            }
        }
        for (size_t i = 0; i < ipeek_addrs.size(); ++i) {
            uint8_t value = 0u;
            if (!mmt8_clap_wrapper_debug_peek_iram(runtime, ipeek_addrs[i], &value)) {
                continue;
            }
            if (!prev_ipeek_valid[i] || prev_ipeek_values[i] != value) {
                std::cout << "ipeek block=" << b
                          << " addr=0x" << std::hex << static_cast<unsigned>(ipeek_addrs[i])
                          << " val=0x" << static_cast<unsigned>(value) << std::dec << "\n";
                prev_ipeek_values[i] = value;
                prev_ipeek_valid[i] = true;
            }
        }

        if (rt.click_edges_last_block > 0u) {
            std::cout << "click_edges block=" << b << " n=" << rt.click_edges_last_block << "\n";
        }

        /* Click output check: when record-armed + playing, report left channel (ROM-driven click) */
        {
            std::vector<float> left_buf(frames, 0.0f);
            std::vector<float> right_buf(frames, 0.0f);
            runtime->copy_audio_outputs(left_buf.data(), right_buf.data(), frames);
            if (tape_out) std::fwrite(right_buf.data(), sizeof(float), frames, tape_out);
            if (rt.panel_playing && rt.panel_record_armed) {
                float max_left = 0.0f;
                for (uint32_t i = 0; i < frames; ++i) {
                    const float a = std::fabs(left_buf[i]);
                    if (a > max_left) max_left = a;
                }
                if (max_left > 1e-6f) {
                    std::cout << "click_out block=" << b << " max_left=" << max_left << " (record+play, ROM click active)\n";
                }
            }
        }

        const bool counters_changed =
            (rt.xdata_writes != prev_xw) || (rt.lcd_cmd_writes != prev_cmd) || (rt.lcd_data_writes != prev_data);
        const bool callback_changed =
            (rt.xram_04ee_writes != prev_04ee) || (rt.ff1b_writes != prev_ff1b) || (rt.ff1c_writes != prev_ff1c);
        const bool serial_changed = (rt.serial_rx_reads != prev_serial_reads);
        const bool serial_irq_changed =
            (rt.serial_irq_asserts != prev_serial_irq_asserts) ||
            (rt.serial_irq_clears != prev_serial_irq_clears);
        const bool read_changed = (rt.xdata_reads != prev_xreads) || (rt.ff06_reads != prev_ff06_reads);
        const bool isr_changed =
            (rt.isr_int0_hits != prev_isr_int0_hits) ||
            (rt.isr_timer0_hits != prev_isr_timer0_hits) ||
            (rt.isr_int1_hits != prev_isr_int1_hits) ||
            (rt.isr_timer1_hits != prev_isr_timer1_hits) ||
            (rt.isr_serial_hits != prev_isr_serial_hits) ||
            (rt.isr_timer2_hits != prev_isr_timer2_hits);
        const bool phase_changed = (rt.boot_phase != prev_phase);
        const bool panel_state_changed =
            (rt.panel_playing != prev_panel_playing) ||
            (rt.panel_record_armed != prev_panel_record_armed);
        const bool panel_output_changed =
            (rt.panel_ff02 != prev_panel_ff02) ||
            (rt.panel_ff04 != prev_panel_ff04) ||
            (rt.panel_p3 != prev_panel_p3) ||
            (rt.panel_p3_min != prev_panel_p3_min) ||
            (rt.panel_p3_max != prev_panel_p3_max) ||
            (rt.panel_track_led_latch != prev_track_latch) ||
            (rt.panel_button_led_latch != prev_button_latch) ||
            (rt.panel_track_led_observed != prev_track_observed) ||
            (rt.panel_button_led_observed != prev_button_observed);
        const bool scan_changed = (rt.panel_scan_bank != prev_scan_bank) ||
                                  (rt.panel_edge_shadow != prev_edge_shadow) ||
                                  (rt.panel_raw_shadow != prev_raw_shadow);
        const bool lcd_non_blank =
            (rt.lcd_line1.find_first_not_of(' ') != std::string::npos) ||
            (rt.lcd_line2.find_first_not_of(' ') != std::string::npos);

        if (counters_changed || callback_changed || serial_changed || serial_irq_changed ||
            read_changed || isr_changed || phase_changed || panel_state_changed ||
            panel_output_changed || scan_changed || (lcd_non_blank && !printed_lcd)) {
            std::cout << "block=" << b
                      << " cpu=" << rt.cpu_cycles
                      << " pc=0x" << std::hex << static_cast<unsigned>(rt.pc) << std::dec
                      << " phase=" << rt.boot_phase
                      << " play=" << (rt.panel_playing ? 1 : 0)
                      << " recarm=" << (rt.panel_record_armed ? 1 : 0)
                      << " xw=" << rt.xdata_writes
                      << " cmd=" << rt.lcd_cmd_writes
                      << " data=" << rt.lcd_data_writes
                      << std::dec
                      << " 04ee=" << rt.xram_04ee_writes
                      << " ff1b=" << rt.ff1b_writes
                      << " ff1c=" << rt.ff1c_writes
                      << " srx=" << rt.serial_rx_reads
                      << " srxq=" << rt.serial_rx_queue_depth
                      << " sira=" << rt.serial_irq_asserts
                      << " sirc=" << rt.serial_irq_clears
                      << " xr=" << rt.xdata_reads
                      << " ff06r=" << rt.ff06_reads
                      << " i0=" << rt.isr_int0_hits
                      << " t0=" << rt.isr_timer0_hits
                      << " i1=" << rt.isr_int1_hits
                      << " t1=" << rt.isr_timer1_hits
                      << " isrser=" << rt.isr_serial_hits
                      << " t2=" << rt.isr_timer2_hits
                      << " psinj=" << (rt.panel_shadow_injection_enabled ? 1 : 0)
                      << " ff02=0x" << std::hex << static_cast<unsigned>(rt.panel_ff02)
                      << " ff04=0x" << static_cast<unsigned>(rt.panel_ff04)
                      << " p3=0x" << static_cast<unsigned>(rt.panel_p3)
                      << " p3min=0x" << static_cast<unsigned>(rt.panel_p3_min)
                      << " p3max=0x" << static_cast<unsigned>(rt.panel_p3_max)
                      << " track=0x" << static_cast<unsigned>(rt.panel_track_led_latch)
                      << " buttons=0x" << static_cast<unsigned>(rt.panel_button_led_latch)
                      << " track_obs=0x" << static_cast<unsigned>(rt.panel_track_led_observed)
                      << " button_obs=0x" << static_cast<unsigned>(rt.panel_button_led_observed)
                      << std::dec
                      << " scan=" << rt.panel_scan_bank
                      << " lxra=0x" << std::hex << static_cast<unsigned>(rt.last_xread_addr)
                      << " lxrpc=0x" << static_cast<unsigned>(rt.last_xread_pc) << std::dec
                      << " last=[0x" << std::hex << static_cast<unsigned>(rt.last_xdata_addr)
                      << " <- 0x" << static_cast<unsigned>(rt.last_xdata_value)
                      << " @pc=0x" << static_cast<unsigned>(rt.last_xdata_pc) << std::dec << "]\n";
            std::cout << "panel edge=";
            for (size_t i = 0; i < rt.panel_edge_shadow.size(); ++i) {
                if (i != 0) std::cout << ",";
                std::cout << "0x" << std::hex << static_cast<unsigned>(rt.panel_edge_shadow[i]) << std::dec;
            }
            std::cout << " raw=";
            for (size_t i = 0; i < rt.panel_raw_shadow.size(); ++i) {
                if (i != 0) std::cout << ",";
                std::cout << "0x" << std::hex << static_cast<unsigned>(rt.panel_raw_shadow[i]) << std::dec;
            }
            std::cout << "\n";
            std::cout << "lcd1='" << rt.lcd_line1 << "' lcd2='" << rt.lcd_line2 << "'\n";
        }
        {
            static int prev_cursor = -1;
            const int cursor = (rt.lcd_display << 8) | rt.lcd_addr;
            if (cursor != prev_cursor) {
                prev_cursor = cursor;
                std::cout << "lcd_cursor block=" << b << " addr=0x" << std::hex
                          << static_cast<unsigned>(rt.lcd_addr) << " display=" << static_cast<unsigned>(rt.lcd_display)
                          << std::dec << "\n";
            }
        }
        if (!midi_out.empty()) {
            std::cout << "midi_out block=" << b << " bytes=";
            bool first = true;
            for (const auto& msg : midi_out) {
                if (!first) std::cout << ";";
                first = false;
                for (size_t i = 0; i < msg.data.size(); ++i) {
                    if (i != 0) std::cout << "-";
                    std::cout << std::hex << static_cast<unsigned>(msg.data[i]) << std::dec;
                }
            }
            std::cout << "\n";
        }
        if (panel_write_log) {
            uint32_t w[4096];
            const uint32_t count = mmt8_clap_wrapper_debug_drain_panel_writes(runtime, w, 4096u);
            for (uint32_t i = 0; i < count; ++i) {
                std::cout << "panel_w block=" << b << " ff0" << std::hex << (w[i] >> 28)
                          << "=0x" << (w[i] & 0xFFu) << " pc=0x" << ((w[i] >> 8) & 0xFFFFu) << std::dec << "\n";
            }
        }
        {
            uint8_t serial_bytes[1024];
            const uint32_t count = mmt8_clap_wrapper_debug_drain_serial_tx(runtime, serial_bytes, 1024u);
            if (count != 0u) {
                std::cout << "serial_tx block=" << b << " bytes=";
                for (uint32_t i = 0; i < count; ++i) {
                    if (i != 0u) std::cout << "-";
                    std::cout << std::hex << static_cast<unsigned>(serial_bytes[i]) << std::dec;
                }
                std::cout << "\n";
            }
        }

        prev_xw = rt.xdata_writes;
        prev_cmd = rt.lcd_cmd_writes;
        prev_data = rt.lcd_data_writes;
        prev_04ee = rt.xram_04ee_writes;
        prev_ff1b = rt.ff1b_writes;
        prev_ff1c = rt.ff1c_writes;
        prev_serial_reads = rt.serial_rx_reads;
        prev_serial_irq_asserts = rt.serial_irq_asserts;
        prev_serial_irq_clears = rt.serial_irq_clears;
        prev_xreads = rt.xdata_reads;
        prev_ff06_reads = rt.ff06_reads;
        prev_isr_int0_hits = rt.isr_int0_hits;
        prev_isr_timer0_hits = rt.isr_timer0_hits;
        prev_isr_int1_hits = rt.isr_int1_hits;
        prev_isr_timer1_hits = rt.isr_timer1_hits;
        prev_isr_serial_hits = rt.isr_serial_hits;
        prev_isr_timer2_hits = rt.isr_timer2_hits;
        prev_phase = rt.boot_phase;
        prev_panel_playing = rt.panel_playing;
        prev_panel_record_armed = rt.panel_record_armed;
        prev_panel_ff02 = rt.panel_ff02;
        prev_panel_ff04 = rt.panel_ff04;
        prev_panel_p3 = rt.panel_p3;
        prev_panel_p3_min = rt.panel_p3_min;
        prev_panel_p3_max = rt.panel_p3_max;
        prev_track_latch = rt.panel_track_led_latch;
        prev_button_latch = rt.panel_button_led_latch;
        prev_track_observed = rt.panel_track_led_observed;
        prev_button_observed = rt.panel_button_led_observed;
        prev_scan_bank = rt.panel_scan_bank;
        prev_edge_shadow = rt.panel_edge_shadow;
        prev_raw_shadow = rt.panel_raw_shadow;
        printed_lcd = printed_lcd || lcd_non_blank;
    }

    if (tape_out) std::fclose(tape_out);
    if (const char *dump_path = std::getenv("MMT8_PROBE_XRAM_DUMP")) {
        if (FILE *f = std::fopen(dump_path, "wb")) {
            // Stop below the 0xFFxx MMIO page: reads there have side effects.
            for (uint32_t a = 0; a < 0xFF00u; ++a) {
                uint8_t v = 0;
                mmt8_clap_wrapper_debug_peek_xram(runtime, static_cast<uint16_t>(a), &v);
                std::fputc(v, f);
            }
            std::fclose(f);
        }
    }

    const auto final = runtime->telemetry();
    std::cout << "final sp_max=0x" << std::hex << static_cast<unsigned>(mmt8_clap_wrapper_debug_sp_max(runtime))
              << std::dec << "\n";
    std::cout << "final cpu=" << final.cpu_cycles
              << " pc=0x" << std::hex << static_cast<unsigned>(final.pc) << std::dec
              << " xw=" << final.xdata_writes
              << " cmd=" << final.lcd_cmd_writes
              << " data=" << final.lcd_data_writes
              << std::dec
              << " 04ee=" << final.xram_04ee_writes
              << " ff1b=" << final.ff1b_writes
              << " ff1c=" << final.ff1c_writes
              << " srx=" << final.serial_rx_reads
              << " srxq=" << final.serial_rx_queue_depth
              << " sira=" << final.serial_irq_asserts
              << " sirc=" << final.serial_irq_clears
              << " xr=" << final.xdata_reads
              << " ff06r=" << final.ff06_reads
              << " i0=" << final.isr_int0_hits
              << " t0=" << final.isr_timer0_hits
              << " i1=" << final.isr_int1_hits
              << " t1=" << final.isr_timer1_hits
              << " isrser=" << final.isr_serial_hits
              << " t2=" << final.isr_timer2_hits
              << " psinj=" << (final.panel_shadow_injection_enabled ? 1 : 0)
              << " ff02=0x" << std::hex << static_cast<unsigned>(final.panel_ff02)
              << " ff04=0x" << static_cast<unsigned>(final.panel_ff04)
              << " p3=0x" << static_cast<unsigned>(final.panel_p3)
              << " p3min=0x" << static_cast<unsigned>(final.panel_p3_min)
              << " p3max=0x" << static_cast<unsigned>(final.panel_p3_max)
              << " track=0x" << static_cast<unsigned>(final.panel_track_led_latch)
              << " buttons=0x" << static_cast<unsigned>(final.panel_button_led_latch)
              << " track_obs=0x" << static_cast<unsigned>(final.panel_track_led_observed)
              << " button_obs=0x" << static_cast<unsigned>(final.panel_button_led_observed)
              << " lxra=0x" << std::hex << static_cast<unsigned>(final.last_xread_addr)
              << " lxrpc=0x" << static_cast<unsigned>(final.last_xread_pc) << std::dec << "\n";
    std::cout << "final lcd1='" << final.lcd_line1 << "' lcd2='" << final.lcd_line2 << "'\n";

    mmt8_clap_wrapper_destroy(runtime);
    return 0;
}
