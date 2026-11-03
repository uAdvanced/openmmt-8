#pragma once

#include <cstdint>

namespace mmt8 {

/* Front-panel keys with their key-matrix ids (row * 8 + column) and their LEDs.
 * Geometry is in design units of a kPanelW x kPanelH panel. */

constexpr float kPanelW = 1000.0f;
constexpr float kPanelH = 440.0f;

enum class Led : uint8_t { None, Mode, Track };
enum class Cap : uint8_t { Function, Keypad, Track, Transport, Record };

struct PanelKey {
    const char *label;
    const char *label2;  // second label line, or nullptr
    uint8_t id;          // key-matrix id
    Led led;
    uint8_t led_bit;     // FF04 bit for Led::Mode, track index for Led::Track
    Cap cap;
    float x, y, w, h;
    const char *hint;    // computer-keyboard shortcut shown in the tooltip, or nullptr
};

constexpr uint8_t kKeyFootswitch = 19;

/* FF04 mode-LED bits, active low. */
enum : uint8_t { kLedPlay = 0, kLedRec = 1, kLedPart = 2, kLedEdit = 3, kLedSong = 4, kLedEcho = 5, kLedLoop = 6 };

namespace layout {
constexpr float FX = 425, FW = 88, FH = 40, FGX = 100, FY = 62, FGY = 58;    // function grid
constexpr float RX = 745, RW = 108, RGX = 118;                              // right block
constexpr float KX = 32, KW = 64, KH = 40, KGX = 72;                        // keypad
constexpr float TX = 425, TW = 58, TGX = 68.5f, TY = 300;                   // track row
constexpr float PX = 425, PW = 100, PGX = 110.5f, PY = 372, PH = 44;        // transport row
}  // namespace layout

// clang-format off
constexpr PanelKey kPanelKeys[] = {
    {"QUANT",  nullptr, 43, Led::None, 0, Cap::Function, layout::FX + 0 * layout::FGX, layout::FY + 0 * layout::FGY, layout::FW, layout::FH, nullptr},
    {"LENGTH", nullptr, 44, Led::None, 0, Cap::Function, layout::FX + 1 * layout::FGX, layout::FY + 0 * layout::FGY, layout::FW, layout::FH, nullptr},
    {"PART",   nullptr, 45, Led::Mode, kLedPart, Cap::Function, layout::FX + 2 * layout::FGX, layout::FY + 0 * layout::FGY, layout::FW, layout::FH, nullptr},
    {"COPY",   nullptr, 6,  Led::None, 0, Cap::Function, layout::FX + 0 * layout::FGX, layout::FY + 1 * layout::FGY, layout::FW, layout::FH, nullptr},
    {"NAME",   nullptr, 47, Led::None, 0, Cap::Function, layout::FX + 1 * layout::FGX, layout::FY + 1 * layout::FGY, layout::FW, layout::FH, nullptr},
    {"EDIT",   nullptr, 46, Led::Mode, kLedEdit, Cap::Function, layout::FX + 2 * layout::FGX, layout::FY + 1 * layout::FGY, layout::FW, layout::FH, nullptr},
    {"TRANS",  nullptr, 3,  Led::None, 0, Cap::Function, layout::FX + 0 * layout::FGX, layout::FY + 2 * layout::FGY, layout::FW, layout::FH, nullptr},
    {"MERGE",  nullptr, 39, Led::None, 0, Cap::Function, layout::FX + 1 * layout::FGX, layout::FY + 2 * layout::FGY, layout::FW, layout::FH, nullptr},
    {"SONG",   nullptr, 38, Led::Mode, kLedSong, Cap::Function, layout::FX + 2 * layout::FGX, layout::FY + 2 * layout::FGY, layout::FW, layout::FH, nullptr},
    {"ERASE",  nullptr, 2,  Led::None, 0, Cap::Function, layout::FX + 0 * layout::FGX, layout::FY + 3 * layout::FGY, layout::FW, layout::FH, nullptr},
    {"TAPE",   nullptr, 31, Led::None, 0, Cap::Function, layout::FX + 1 * layout::FGX, layout::FY + 3 * layout::FGY, layout::FW, layout::FH, nullptr},
    {"MIDI",   "CHAN",  30, Led::None, 0, Cap::Function, layout::FX + 2 * layout::FGX, layout::FY + 3 * layout::FGY, layout::FW, layout::FH, nullptr},

    {"LOOP",   nullptr, 42, Led::Mode, kLedLoop, Cap::Function, layout::RX + 0 * layout::RGX, layout::FY + 0 * layout::FGY, layout::RW, layout::FH, nullptr},
    {"CLOCK",  nullptr, 32, Led::None, 0, Cap::Function, layout::RX + 1 * layout::RGX, layout::FY + 0 * layout::FGY, layout::RW, layout::FH, nullptr},
    {"MIDI",   "ECHO",  41, Led::Mode, kLedEcho, Cap::Function, layout::RX + 0 * layout::RGX, layout::FY + 1 * layout::FGY, layout::RW, layout::FH, nullptr},
    {"CLICK",  nullptr, 24, Led::None, 0, Cap::Function, layout::RX + 1 * layout::RGX, layout::FY + 1 * layout::FGY, layout::RW, layout::FH, nullptr},
    {"MIDI",   "FILTER",40, Led::None, 0, Cap::Function, layout::RX + 0 * layout::RGX, layout::FY + 2 * layout::FGY, layout::RW, layout::FH, nullptr},
    {"TEMPO",  nullptr, 16, Led::None, 0, Cap::Function, layout::RX + 1 * layout::RGX, layout::FY + 2 * layout::FGY, layout::RW, layout::FH, nullptr},

    {"PAGE",   "\xE2\x96\xBC", 23, Led::None, 0, Cap::Keypad, layout::KX + 0 * 90, 196, 82, layout::KH, "Page Down"},
    {"PAGE",   "\xE2\x96\xB2", 22, Led::None, 0, Cap::Keypad, layout::KX + 1 * 90, 196, 82, layout::KH, "Page Up"},
    {"\xE2\x88\x92", nullptr, 17, Led::None, 0, Cap::Keypad, layout::KX + 2 * 90, 196, 82, layout::KH, "-"},
    {"+",      nullptr, 18, Led::None, 0, Cap::Keypad, layout::KX + 3 * 90, 196, 82, layout::KH, "+"},

    {"1", nullptr, 33, Led::None, 0, Cap::Keypad, layout::KX + 0 * layout::KGX, 262, layout::KW, layout::KH, "1"},
    {"2", nullptr, 34, Led::None, 0, Cap::Keypad, layout::KX + 1 * layout::KGX, 262, layout::KW, layout::KH, "2"},
    {"3", nullptr, 35, Led::None, 0, Cap::Keypad, layout::KX + 2 * layout::KGX, 262, layout::KW, layout::KH, "3"},
    {"4", nullptr, 36, Led::None, 0, Cap::Keypad, layout::KX + 3 * layout::KGX, 262, layout::KW, layout::KH, "4"},
    {"5", nullptr, 37, Led::None, 0, Cap::Keypad, layout::KX + 4 * layout::KGX, 262, layout::KW, layout::KH, "5"},
    {"6", nullptr, 25, Led::None, 0, Cap::Keypad, layout::KX + 0 * layout::KGX, 312, layout::KW, layout::KH, "6"},
    {"7", nullptr, 26, Led::None, 0, Cap::Keypad, layout::KX + 1 * layout::KGX, 312, layout::KW, layout::KH, "7"},
    {"8", nullptr, 27, Led::None, 0, Cap::Keypad, layout::KX + 2 * layout::KGX, 312, layout::KW, layout::KH, "8"},
    {"9", nullptr, 28, Led::None, 0, Cap::Keypad, layout::KX + 3 * layout::KGX, 312, layout::KW, layout::KH, "9"},
    {"0", nullptr, 29, Led::None, 0, Cap::Keypad, layout::KX + 4 * layout::KGX, 312, layout::KW, layout::KH, "0"},

    {"1", nullptr, 8,  Led::Track, 0, Cap::Track, layout::TX + 0 * layout::TGX, layout::TY, layout::TW, layout::KH, "F1"},
    {"2", nullptr, 9,  Led::Track, 1, Cap::Track, layout::TX + 1 * layout::TGX, layout::TY, layout::TW, layout::KH, "F2"},
    {"3", nullptr, 10, Led::Track, 2, Cap::Track, layout::TX + 2 * layout::TGX, layout::TY, layout::TW, layout::KH, "F3"},
    {"4", nullptr, 11, Led::Track, 3, Cap::Track, layout::TX + 3 * layout::TGX, layout::TY, layout::TW, layout::KH, "F4"},
    {"5", nullptr, 12, Led::Track, 4, Cap::Track, layout::TX + 4 * layout::TGX, layout::TY, layout::TW, layout::KH, "F5"},
    {"6", nullptr, 13, Led::Track, 5, Cap::Track, layout::TX + 5 * layout::TGX, layout::TY, layout::TW, layout::KH, "F6"},
    {"7", nullptr, 14, Led::Track, 6, Cap::Track, layout::TX + 6 * layout::TGX, layout::TY, layout::TW, layout::KH, "F7"},
    {"8", nullptr, 15, Led::Track, 7, Cap::Track, layout::TX + 7 * layout::TGX, layout::TY, layout::TW, layout::KH, "F8"},

    {"\xE2\x97\x80\xE2\x97\x80", "REWIND",   0, Led::None, 0, Cap::Transport, layout::PX + 0 * layout::PGX, layout::PY, layout::PW, layout::PH, nullptr},
    {"\xE2\x96\xB6\xE2\x96\xB6", "FAST FWD", 1, Led::None, 0, Cap::Transport, layout::PX + 1 * layout::PGX, layout::PY, layout::PW, layout::PH, nullptr},
    {"\xE2\x96\xB6", "PLAY",                 4, Led::Mode, kLedPlay, Cap::Transport, layout::PX + 2 * layout::PGX, layout::PY, layout::PW, layout::PH, nullptr},
    {"\xE2\x96\xA0", "STOP/CONT",            5, Led::None, 0, Cap::Transport, layout::PX + 3 * layout::PGX, layout::PY, layout::PW, layout::PH, nullptr},
    {"\xE2\x97\x8F", "RECORD",               7, Led::Mode, kLedRec, Cap::Record, layout::PX + 4 * layout::PGX, layout::PY, layout::PW, layout::PH, nullptr},
};
// clang-format on

constexpr int kPanelKeyCount = static_cast<int>(sizeof(kPanelKeys) / sizeof(kPanelKeys[0]));

}  // namespace mmt8
