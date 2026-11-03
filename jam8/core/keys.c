/* Key matrix: FF06 bits 0..5 select a row (active low), P1 returns its columns (active low).
 * The footswitch on P3.3 reads as key 19; its polarity is taken from its state at power-up.
 * Scanned every 4 ms from the main loop; a change counts once two scans agree. Held + and -
 * repeat after 400 ms, every 80 ms, then every 32 ms. */
#include "jam.h"

#define REPEAT_FIRST 100u
#define REPEAT_NEXT 20u
#define REPEAT_FAST 8u

static __xdata uint8_t down[6], last[6];
static __xdata uint8_t rep_n, rep_count;
__xdata uint16_t scan_n;
static __bit foot_inv;

/* Keys down (active high) in the rows selected by sel. */
uint8_t row_read(uint8_t sel) __naked
{
    sel;
    __asm
        mov     a, dpl
        mov     dptr, #0xff06
        movx    @dptr, a
        acall   00001$
        acall   00001$
        mov     a, _P1
        cpl     a
        push    acc
        mov     a, #0xff
        movx    @dptr, a
        pop     dpl
00001$:
        ret
    __endasm;
}

void keys_init(void)
{
    uint8_t i;
    for (i = 0; i < 6; ++i)
        down[i] = last[i] = 0;
    foot_inv = !(P3 & 0x08);
}

uint8_t key_held(uint8_t k)
{
    return (down[k >> 3] >> (k & 7)) & 1;
}

void keys_scan(void)
{
    static __xdata uint8_t r, x, d, bit, k;
    uint8_t sel = 0xFE;
    ++scan_n;
    for (r = 0; r < 6; ++r, sel = (uint8_t)((sel << 1) | 1)) {
        x = row_read(sel);
        if (r == 2) {
            x &= (uint8_t)~0x08;
            if (!(P3 & 0x08) != foot_inv)
                x |= 0x08;
        }
        if (x != last[r]) {
            last[r] = x;
            continue;
        }
        d = x ^ down[r];
        if (!d)
            continue;
        down[r] = x;
        for (bit = 1, k = (uint8_t)(r << 3); bit; bit <<= 1, ++k)
            if (d & bit) {
                if (k == K_PLUS || k == K_MINUS) {
                    rep_n = REPEAT_FIRST;
                    rep_count = 0;
                }
                ui_key(k, (x & bit) != 0);
            }
    }
    x = down[2] & 0x06;
    if (x == 0x02 || x == 0x04) {
        if (!--rep_n) {
            rep_n = rep_count < 10 ? REPEAT_NEXT : REPEAT_FAST;
            if (rep_count < 10)
                ++rep_count;
            ui_key(x == 0x04 ? K_PLUS : K_MINUS, 1);
        }
    }
}
