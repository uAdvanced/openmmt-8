/* OpenMMT-8 entry point: timer0 runs at 96 ticks per beat of the current tempo. */
#include "core.h"

void midi_isr(void) __interrupt(4);

volatile __data uint8_t g_ticks;
volatile __xdata uint8_t t_count;
static volatile __data uint8_t reload_hi, reload_lo;
static __xdata uint16_t tick_cycles;
/* Main-loop passes with no tape edge before auto-start playback stops.
 * The original counts 20 wraps of an 8-bit poll; this constant is that
 * wait measured on this loop. */
#ifdef OPENMMT8_TAPE
#define SYNC_GAP 1438u
static __data uint16_t sync_n;
#endif

/* Cycles by which the first timer0 period is lengthened so ticks keep the same phase
 * against the reset as on the original unit. */
#ifndef T0_PHASE
#define T0_PHASE 7590
#endif

void timer0_isr(void) __interrupt(1)
{
    TH0 = reload_hi;
    TL0 = reload_lo;
    ++g_ticks;
}

#ifdef OPENMMT8_TAPE
extern __data uint8_t tape_sh;

/* One tape bit per overflow: the carry just shifted out of tape_sh becomes P3.4. */
void tape_bit_isr(void) __interrupt(3) __naked
{
    __asm
        push    acc
        push    psw
        mov     a, _tape_sh
        clr     c
        rrc     a
        mov     _tape_sh, a
        mov     a, #0xff
        rlc     a
        swap    a
        mov     _P3, a
        pop     psw
        pop     acc
        reti
    __endasm;
}
#endif

#ifdef OPENMMT8_TAPE
/* Tape sync in: one falling edge on P3.2 is one tick, the same counter timer0 uses. */
void tape_clk_isr(void) __interrupt(0) __naked
{
    __asm
        inc     _g_ticks
        reti
    __endasm;
}
#endif

/* 625000 / bpm, the tick period. In assembly so the image does not carry __divulong.
 * +2 on the reload: it runs later after overflow than on the original (vector ljmp). */
void timer_set_tempo(uint8_t bpm) __naked
{
    bpm;
    __asm
        mov     r2, dpl
        mov     r5, #0x09
        mov     r6, #0x89
        mov     r7, #0x68
        clr     a
        mov     r0, a
        mov     r3, a
        mov     r4, a
        mov     r1, #24
00001$:
        clr     c
        mov     a, r7
        rlc     a
        mov     r7, a
        mov     a, r6
        rlc     a
        mov     r6, a
        mov     a, r5
        rlc     a
        mov     r5, a
        mov     a, r4
        rlc     a
        mov     r4, a
        mov     a, r3
        rlc     a
        mov     r3, a
        clr     c
        mov     a, r4
        subb    a, r2
        mov     a, r3
        subb    a, r0
        jc      00002$
        clr     c
        mov     a, r4
        subb    a, r2
        mov     r4, a
        mov     a, r3
        subb    a, r0
        mov     r3, a
        inc     r7
00002$:
        djnz    r1, 00001$
        mov     dptr, #_tick_cycles
        mov     a, r7
        movx    @dptr, a
        inc     dptr
        mov     a, r6
        movx    @dptr, a
        mov     a, #0x02
        clr     c
        subb    a, r7
        mov     r4, a
        clr     a
        subb    a, r6
        clr     _EA
        mov     _reload_hi, a
        mov     _reload_lo, r4
        setb    _EA
        ret
    __endasm;
}

/* The work variables (0x0200-0x03FF) are battery-backed like the song memory, and a
 * sysex load restarts through the reset vector: start them from zero. */
unsigned char _sdcc_external_startup(void)
{
    xptr p = (xptr)0x0200;
    uint8_t i = 0;
    do {
        p[0] = 0;
        p[256] = 0;
        ++p;
    } while (++i);
    return 0;
}

static const char __code splash[] = "*  OPENMMT-8   ** VERSION 1.11 *";

void main(void)
{
    uint16_t n;

    TMOD = 0x21; /* timer1 8-bit auto-reload (MIDI baud), timer0 16-bit */
    TH1 = TL1 = 0xFF;
    TR1 = 1;
    midi_init();
    timer_set_tempo(120);
    {
        uint16_t first = (uint16_t)(((reload_hi << 8) | reload_lo) - T0_PHASE);
        TH0 = (uint8_t)(first >> 8);
        TL0 = (uint8_t)first;
    }
    TR0 = 1;
#ifdef OPENMMT8_TAPE
    IT0 = 1; /* tape sync in is falling-edge */
#endif
    ET0 = 1;
    EA = 1;

    /* Within the wait below, so the tick phase does not depend on the time they take.
     * Held at power-on: ERASE with PAGE UP and PAGE DOWN clears battery-backed memory,
     * ERASE alone resets the settings. */
    if (row_read(0xFE) == 0x04) {
        uint8_t r = row_read(0xFB);
        if (r == 0xC0)
            cfg.magic = 0;
        else if (!r)
            cfg.magic = CFG_SETTINGS;
    }
    seq_init();
    keys_init();
    for (n = 11; n; --n) {
        while (!g_ticks)
            ;
        --g_ticks;
    }
    /* The screen keeps what it showed (a sysex load restarts here) until the splash. */
    lcd_init();
    lcd_text(0, splash, 32);
    lcd_cursor = LCD_UL;
    lcd_flush();
    ui_init();
    ui_leds();
    for (n = 303; n; --n) {
        while (!g_ticks)
            ;
        --g_ticks;
    }
    timer_set_tempo(cfg.tempo);
    clock_apply();

    for (;;) {
#ifdef OPENMMT8_TAPE
        if (tape_clk) {
            if (tp_state == TP_STOP) {
                if (g_ticks && cfg.auto_start)
                    ui_key_down(K_PLAY);
            } else if (cfg.auto_start && ++sync_n == SYNC_GAP)
                ui_key_down(K_STOP);
        }
#endif
        if (g_ticks) {
            --g_ticks;
#ifdef OPENMMT8_TAPE
            sync_n = 0;
#endif
            ++t_count;
            seq_tick();
            keys_tick(tick_cycles);
        }
        while (keys_scan()) {
            keys_flush();
            if (tp_state == TP_STOP && ui_dirty) {
                ui_dirty = 0;
                ui_render();
                keys_drawn(lcd_flush());
            }
        }
        seq_midi_in();
        if (ui_dirty) {
            ui_dirty = 0;
            ui_render();
            keys_drawn(lcd_flush());
        }
#ifdef OPENMMT8_TAPE
        if (tape_run && !tape_aborted)
            ui_tape();
#endif
    }
}
