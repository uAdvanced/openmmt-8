/* 2x16 LCD: screens render into lcd[]; lcd_flush() writes changed cells, a few per call. */
#include "jam.h"

__xdata char lcd[32];
static __xdata char shown[32];

static void lcd_wait(void)
{
    uint8_t i = 20;
    while (--i)
        ;
}

/* About n * 0.52 ms; the module is never polled for busy. Naked, so the compiler assumes
 * it leaves every register alone. */
static void lcd_long(uint8_t n) __naked
{
    n;
    __asm
        push    ar6
        push    ar7
        mov     r6, dpl
00001$:
        mov     r7, #0
00002$:
        djnz    r7, 00002$
        djnz    r6, 00001$
        pop     ar7
        pop     ar6
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
    static const uint8_t __code init[] = {0x38, 0x38, 0x38, 0x06, 0x0C};
    uint8_t i;
    lcd_long(100); /* the module powers up slower than the CPU leaves reset */
    lcd_cmd(0x38);
    lcd_long(15);
    for (i = 0; i < sizeof init; ++i) {
        lcd_cmd(init[i]);
        lcd_long(1);
    }
    lcd_cmd(0x01);
    lcd_long(6);
    /* Nothing on the glass is trusted: the first flush writes every cell. */
    for (i = 0; i < 32; ++i) {
        lcd[i] = ' ';
        shown[i] = 0;
    }
}

/* Writes at most `budget` changed cells; returns how many are still different. */
uint8_t lcd_flush(uint8_t budget)
{
    uint8_t i, at = 0xFF, left = 0;
    char c;
    for (i = 0; i < 32; ++i) {
        c = lcd[i];
        if (c == shown[i])
            continue;
        if (!budget) {
            ++left;
            continue;
        }
        --budget;
        if (at != i)
            lcd_cmd((uint8_t)(((i & 16) ? 0xC0 : 0x80) | (i & 15)));
        LCD_DATA = c;
        lcd_wait();
        shown[i] = c;
        at = (uint8_t)(i + 1);
        if (at == 16)
            at = 0xFF;
    }
    return left;
}

void lcd_text(uint8_t pos, const char __code *s, uint8_t n)
{
    __xdata char *d = lcd + pos;
    while (n--)
        *d++ = *s++;
}

static const uint16_t __code pow10[] = {10000, 1000, 100, 10, 1};

/* The low `width` decimal digits of v, leading zeros as spaces except the last digit. */
void lcd_num(uint8_t pos, uint16_t v, uint8_t width)
{
    __xdata char *d = lcd + pos + width - 5;
    uint8_t i, lead = 1;
    char c;
    for (i = 0; i < 5; ++i, ++d) {
        for (c = '0'; v >= pow10[i]; ++c)
            v -= pow10[i];
        if (c != '0' || i == 4)
            lead = 0;
        if (i + width >= 5)
            *d = lead ? ' ' : c;
    }
}

void lcd_fill(uint8_t pos, uint8_t n)
{
    __xdata char *d = lcd + pos;
    while (n--)
        *d++ = ' ';
}
