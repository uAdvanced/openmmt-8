/* MIDI in: clock and transport when synced to MIDI clock (one F8 is 4 ticks), sysex dump
 * loads, the CC knob, capture, MIDI thru to the focused lane, and control-channel notes
 * for the harmony bus. */
#include "jam.h"

__xdata uint8_t tp_ticks;
static __xdata uint8_t status, d0, nd, need;
static __bit in_sysex;

static void realtime(uint8_t b)
{
    if (!cfg.sync)
        return;
    switch (b) {
    case 0xF8:
        if (playing)
            tp_ticks += 4;
        break;
    case 0xFA:
        engine_start(0);
        break;
    case 0xFB:
        engine_start(1);
        break;
    case 0xFC:
        if (playing)
            engine_stop();
        break;
    }
}

/* Channel messages played into the box go out on the focused lane's channel. */
void clock_thru(uint8_t st, uint8_t a, uint8_t b, uint8_t len)
{
    midi_tx((uint8_t)((st & 0xF0) | (HDR(focus)[H_CHAN] & 15)));
    midi_tx(a);
    if (len > 1)
        midi_tx(b);
}

static void message(uint8_t st, uint8_t a, uint8_t b)
{
    uint8_t type = st & 0xF0, ch = (uint8_t)((st & 15) + 1);
    if (type == 0x90 && !b)
        type = 0x80;
    if (type == 0xB0 && cfg.knob && a == cfg.knob) {
        ui_cc(a, b);
        return;
    }
    /* The control channel only steers the harmony: it is not captured or passed thru. */
    if (cfg.ctrl_ch && ch == cfg.ctrl_ch) {
        if (type == 0x90) {
            harm_ctrl_note(a);
            ui_dirty = 1;
        } else if (type == 0x80) {
            harm_ctrl_off(a);
        }
        return;
    }
    if (type == 0x80)
        engine_note_off(a);
    if (type == 0x90 && engine_note_in(a, b)) {
        if (cfg.thru)
            clock_thru(st, a, b, 2);
        return;
    }
    if (cfg.thru)
        clock_thru(st, a, b, need);
}

void clock_poll(void)
{
    uint8_t b, n;
    for (n = 0; n < 16 && midi_rx_ready(); ++n) {
        b = midi_rx();
        if (b >= 0xF8) {
            realtime(b);
            continue;
        }
        if (in_sysex) {
            if (b < 0x80 || b == 0xF7) {
                dump_rx(b);
                if (b == 0xF7)
                    in_sysex = 0;
                continue;
            }
            in_sysex = 0;
            dump_rx_state = 0;
        }
        if (b == 0xF0) {
            in_sysex = 1;
            dump_rx(b);
            continue;
        }
        if (b >= 0x80) {
            status = b < 0xF0 ? b : 0;
            nd = 0;
            need = (b & 0xE0) == 0xC0 ? 1 : 2;
            continue;
        }
        if (!status)
            continue;
        if (!nd) {
            d0 = b;
            nd = 1;
            if (need == 1) {
                message(status, d0, 0);
                nd = 0;
            }
        } else {
            message(status, d0, b);
            nd = 0;
        }
    }
}
