/* JAM-8 entry point: timer0 runs at 96 ticks per beat of the internal tempo; the main loop
 * plays the ticks, scans the keys every 4 ms, and spends the rest on the generator. */
#include "jam.h"

void midi_isr(void) __interrupt(4);

volatile __data uint8_t t0_ticks;
static volatile __data uint8_t reload_hi, reload_lo;
__xdata uint16_t tick_cycles;
static __xdata uint16_t ui_acc;

#define SCAN_CYCLES 4000u

void timer0_isr(void) __interrupt(1)
{
    TH0 = reload_hi;
    TL0 = reload_lo;
    ++t0_ticks;
}

/* 6250000 / tempo10 machine cycles per tick. */
void timer_set_tempo(uint16_t tempo10)
{
    uint16_t r;
    if (tempo10 < 400)
        tempo10 = 400;
    if (tempo10 > 2500)
        tempo10 = 2500;
    tick_cycles = (uint16_t)(6250000UL / tempo10);
    r = (uint16_t)(0u - tick_cycles + 2u);
    EA = 0;
    reload_hi = (uint8_t)(r >> 8);
    reload_lo = (uint8_t)r;
    EA = 1;
}

uint16_t tempo_get(void)
{
    return ((uint16_t)cfg.tempo_hi << 8) | cfg.tempo_lo;
}

void tempo_set(uint16_t t10)
{
    if (t10 < 400)
        t10 = 400;
    if (t10 > 2500)
        t10 = 2500;
    cfg.tempo_hi = (uint8_t)(t10 >> 8);
    cfg.tempo_lo = (uint8_t)t10;
    timer_set_tempo(t10);
}

/* The work area (0x0200-0x07FF) is battery-backed like the scenes: start it from zero. */
unsigned char _sdcc_external_startup(void)
{
    __xdata uint8_t *p = (__xdata uint8_t *)0x0200;
    uint8_t i = 0;
    do {
        p[0] = 0;
        p[0x100] = 0;
        p[0x200] = 0;
        p[0x300] = 0;
        p[0x400] = 0;
        p[0x500] = 0;
        ++p;
    } while (++i);
    return 0;
}

static const char __code splash[] = "JAM-8 LOOP ROM  GENERATIVE V1.0 ";
static const char __code initmsg[] = "MEMORY CLEARED  FACTORY SCENES  ";

static void wait_ticks(uint16_t n)
{
    while (n) {
        if (t0_ticks) {
            --t0_ticks;
            --n;
        }
    }
}

void main(void)
{
    uint8_t factory;

    TMOD = 0x21; /* timer1 8-bit auto-reload (MIDI baud), timer0 16-bit */
    TH1 = TL1 = 0xFF;
    TR1 = 1;
    midi_init();
    timer_set_tempo(1200);
    TH0 = reload_hi;
    TL0 = reload_lo;
    TR0 = 1;
    ET0 = 1;
    EA = 1;

    /* ERASE held at power-on clears memory and loads the factory scenes. */
    factory = row_read(0xFE) == 0x04 || !mem_signed();
    keys_init();
    lcd_init();
    lcd_text(0, factory ? initmsg : splash, 32);
    lcd_flush(32);
    if (factory)
        scene_factory();
    else
        mem_sanitize();
    harm_init();
    engine_init();
    scene_select(cfg.scene);
    timer_set_tempo(tempo_get());
    wait_ticks(200);
    ui_init();

    for (;;) {
        clock_poll();
        while (t0_ticks) {
            --t0_ticks;
            if (!cfg.sync && playing)
                ++tp_ticks;
            ui_acc += tick_cycles;
        }
        if (tp_ticks) {
            --tp_ticks;
            engine_tick();
            continue;
        }
        if (ui_acc >= SCAN_CYCLES) {
            ui_acc -= SCAN_CYCLES;
            if (ui_acc > 4 * SCAN_CYCLES)
                ui_acc = 0;
            keys_scan();
            ui_scan();
            if (!(scan_n & 3))
                ui_leds();
            continue;
        }
        gen_slice();
        engine_slice();
        if (dump_run)
            dump_slice();
        if (ui_dirty) {
            ui_dirty = 0;
            ui_render();
        }
        lcd_flush(4);
    }
}
