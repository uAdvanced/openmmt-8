/* 2x16 LCD: screens render into lcd[]; lcd_flush() writes only changed cells. */
#include "core.h"

__xdata char lcd[32];
__xdata uint8_t lcd_cursor;
static __xdata char shown[32];
static __xdata uint8_t shown_cursor;

static void lcd_wait(void)
{
    uint8_t i = 20;
    while (--i)
        ;
}

/* About n * 0.52 ms. The module is never polled for busy, so the first function set and
 * the clear wait at least as long as the original's fixed waits (7.3 ms and 2.4 ms). */
static void lcd_long(uint8_t n) __naked
{
    n;
    __asm
        mov     r6, dpl
00001$:
        mov     r7, #0
00002$:
        djnz    r7, 00002$
        djnz    r6, 00001$
        ret
    __endasm;
}

static void lcd_cmd(uint8_t c)
{
    LCD_CMD = c;
    lcd_wait();
}

void lcd_init(void)
{
    static const uint8_t __code init[] = {0x38, 0x38, 0x38, 0x06, 0x0E};
    uint8_t i;
    lcd_cmd(0x38);
    lcd_long(15);
    for (i = 0; i < sizeof init; ++i)
        lcd_cmd(init[i]);
    lcd_cmd(0x01);
    lcd_long(2);
    for (i = 0; i < 32; ++i)
        shown[i] = ' ';
    shown_cursor = 0xFF;
}

static uint8_t addr(uint8_t pos)
{
    return (uint8_t)(((pos & 16) ? 0xC0 : 0x80) | (pos & 15));
}

#ifdef OPENMMT8_TAPE
void lcd_hw_cell(uint8_t pos)
{
    if (lcd[pos] == shown[pos])
        return;
    lcd_cmd(addr(pos));
    LCD_DATA = lcd[pos];
    shown[pos] = lcd[pos];
    shown_cursor = 0xFF;
}
#endif

uint8_t lcd_flush(void)
{
    uint8_t i, at = 0xFF, n = 0;
    for (i = 0; i < 32; ++i) {
        char c = lcd[i];
        if (c == shown[i])
            continue;
        if (at != i)
            lcd_cmd(addr(i));
        LCD_DATA = c;
        shown[i] = c;
        ++n;
        at = i + 1;
        if (at == 16)
            at = 0xFF;
        shown_cursor = 0xFF;
    }
    if (shown_cursor != lcd_cursor) {
        i = lcd_cursor;
        shown_cursor = i;
#ifndef OPENMMT8_TAPE
        lcd_cmd(0x0C | ((i >> 5) & 3));
#endif
        lcd_cmd(addr(i));
    }
    return n;
}

void lcd_text(uint8_t pos, const char __code *s, uint8_t n)
{
    __xdata char *d = lcd + pos;
    while (n--)
        *d++ = *s++;
}

static const uint16_t __code pow10[] = {10000, 1000, 100, 10, 1};

/* The low `width` decimal digits of v; with LCD_ENTRY in width, the keypad digits being
 * entered instead, if any. No helper calls, so the arguments overlay. */
/* uint8 values stay a one-byte argument; lcd_num's second argument is 16-bit. */
void lcd_num8(uint8_t pos, uint8_t v, uint8_t width)
{
    lcd_num(pos, v, width);
}

void lcd_num(uint8_t pos, uint16_t v, uint8_t width)
{
    __xdata char *d;
    uint8_t i;
    char c;
    if (width & LCD_ENTRY) {
        width &= ~LCD_ENTRY;
        if (ent_n) {
            for (i = 0, d = lcd + pos; i < width; ++i)
                *d++ = i < ent_n ? ent_c[i] : ' ';
            return;
        }
    }
    d = lcd + pos + width - 5;
    for (i = 0; i < 5; ++i, ++d) {
        for (c = '0'; v >= pow10[i]; ++c)
            v -= pow10[i];
        if (i + width >= 5)
            *d = c;
    }
}

void lcd_fill(uint8_t pos, uint8_t n)
{
    __xdata char *d = lcd + pos;
    while (n--)
        *d++ = ' ';
}
