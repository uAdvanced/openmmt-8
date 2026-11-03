/* MIDI UART (31250 baud): 256-byte receive and transmit rings serviced by the serial ISR. */
#include "core.h"

static __xdata __at(0x0000) uint8_t rxq[256];
static __xdata __at(0x0100) uint8_t txq[256];
static volatile __data uint8_t rx_w, rx_r, tx_w, tx_r;
static volatile __bit tx_busy;
volatile __xdata uint8_t rx_t, rx_t_msg;

void midi_init(void)
{
    rx_w = rx_r = tx_w = tx_r = 0;
    rx_t_msg = 0;
    tx_busy = 0;
    SCON = 0x50;
    ES = 1;
}

/* In assembly: the next byte must reach SBUF 21 cycles after TI, as close as the original
 * unit's 20 (one byte per 340 cycles), which compiled C (36) misses. A byte comes from
 * the transmit ring through movx @R0 with P2 as the page; nothing else uses P2. Only
 * bank 0 is in use, so R0 is IRAM 0x00. */
void midi_isr(void) __interrupt(4) __naked
{
    __asm
        push    psw
        push    acc
        jbc     _TI, 00010$
00001$:
        jbc     _RI, 00020$
00002$:
        pop     acc
        pop     psw
        reti
00010$:
        mov     a, _tx_r
        cjne    a, _tx_w, 00011$
        clr     _tx_busy
        sjmp    00001$
00011$:
        push    0x00
        mov     r0, a
        mov     _P2, #(_txq >> 8)
        movx    a, @r0
        mov     _SBUF, a
        inc     _tx_r
        pop     0x00
        sjmp    00001$
00020$:
        push    dpl
        push    dph
        mov     dpl, _rx_w
        mov     dph, #(_rxq >> 8)
        mov     a, _SBUF
        movx    @dptr, a
        inc     _rx_w
        mov     dptr, #_t_count
        movx    a, @dptr
        mov     dptr, #_rx_t
        movx    @dptr, a
        pop     dph
        pop     dpl
        sjmp    00002$
    __endasm;
}

void midi_tx(uint8_t b)
{
    while ((uint8_t)(tx_w + 1) == tx_r)
        ;
    txq[tx_w] = b;
    ES = 0;
    ++tx_w;
    if (!tx_busy) {
        tx_busy = 1;
        SBUF = txq[tx_r++];
    }
    ES = 1;
}

/* Wait until the last queued byte has started. */
void midi_drain(void)
{
    while (tx_busy)
        ;
}

uint8_t midi_rx_ready(void)
{
    return rx_r != rx_w;
}

uint8_t midi_rx(void)
{
    return rxq[rx_r++];
}
