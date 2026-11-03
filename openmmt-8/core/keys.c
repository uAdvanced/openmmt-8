/* Key matrix: FF06 bits 0..5 select a row (active low), P1 returns its columns (active low).
 * The footswitch on P3.3 reads as key 19; its polarity is taken from its state at power-up.
 * Passes of the original unit's main loop are modelled from timer0: when stopped, the shown
 * screen's pass (see ui_cadence) on a grid restarted SCAN_STOP_PHASE after playback stops,
 * the screen's drawing pass when a new one is drawn, SCAN_CELL_CYCLES longer per cell
 * otherwise rewritten, and some operations lengthen it further; SCAN_CYCLES_PLAY while
 * playing and SCAN_CYCLES_REC while recording. A change is acted on at once, in matrix
 * order, and the matrix is not sampled again for a stopped pass, so contact bounce is not
 * seen; a track key release counts only if it lasts RELEASE_CONFIRM. A row walk reaching
 * three or more keys' worth of activity stops there (ghosting). Held +/- repeat after
 * REPEAT_FIRST repeat periods, then every
 * REPEAT_NEXT, timed from timer0. Held FF or REW step the located beat after LOCATE_FIRST
 * repeat periods, then after LOCATE_NEXT periods, one fewer each step down to 1, then every
 * LOCATE_MIN; the count pauses while both are held. */
#include "core.h"

/* Repeat period in machine cycles: the original unit's scan cadence, about 7860 cycles
 * when stopped and 4870 while running. */
#define REPEAT_CYCLES_STOP 7861u
#define REPEAT_CYCLES_RUN 4867u
#define RELEASE_CONFIRM 2000u
#define REPEAT_FIRST 0x80
#define REPEAT_NEXT 0x14
#define LOCATE_FIRST 80
#define LOCATE_NEXT 39
#define LOCATE_MIN 6

#define SCAN_CYCLES_PLAY 6080u
#define SCAN_CYCLES_REC 17335u
#define SCAN_REC_TICK 1167u
#define SCAN_HELD_CYCLES 1440u
#define SCAN_PHASE 3300u
#define SCAN_STOP_PHASE 4808u
#define SCAN_REC_STOP_PHASE 8300u
#define SCAN_SENT_PHASE 13200u
/* Lengthening of a stopped pass per LCD cell it rewrites. */
#define SCAN_CELL_CYCLES 79u

/* Dispatched state; the sample waiting to be acted on. */
static __xdata __at(KEYS_RAM) struct {
    uint8_t down[6], b_state[6];
} kr;
#define down kr.down
#define b_state kr.b_state
/* Machine-cycle clock: cycles of the ticks handled so far plus the running timer0 count. */
static __xdata __at(KEYS_RAM + sizeof kr) struct {
    uint16_t clk, t_len, scan_at, last_at, act_at, repeat_acc, loc_acc, stop_at;
    uint8_t repeat_n, loc_n, loc_d, rescan, r, bit, k, d, late, rec_stop, conf;
} kv;
#define clk kv.clk
#define t_len kv.t_len
#define scan_at kv.scan_at
#define act_at kv.act_at
#define repeat_acc kv.repeat_acc
#define loc_acc kv.loc_acc
#define repeat_n kv.repeat_n
#define loc_n kv.loc_n
#define loc_d kv.loc_d
#define rescan kv.rescan
static __bit foot_inv, foot_init, foot;

static __bit pending, any_down, was_run, stop_grid;
#define late kv.late
#define rec_stop kv.rec_stop
#define conf kv.conf

void keys_init(void)
{
    uint8_t i;
    for (i = 0; i < sizeof kr + sizeof kv; ++i)
        ((__xdata uint8_t *)&kr)[i] = 0;
    scan_at = SCAN_PHASE;
    repeat_n = REPEAT_FIRST;
}

/* After a bulk send, whose ticks are dropped: close a batch on the next scan and act on it
 * at once, so keys changed meanwhile take effect right away; the pass grid restarts
 * SCAN_SENT_PHASE after the last byte went out. */
void keys_rescan(uint16_t sent)
{
    for (; g_ticks; --g_ticks)
        sent -= t_len;
    kv.stop_at = sent + SCAN_SENT_PHASE;
    rescan = 1;
    repeat_acc = 0;
}

/* Work the original unit does within the current pass delays the next sample. */
void keys_pass_extend(uint16_t cycles)
{
    if (tp_state == TP_STOP)
        scan_at += cycles;
}

void keys_drawn(uint8_t cells)
{
    uint16_t d = ui_draw_cycles();
    if (d && stop_grid && tp_state == TP_STOP)
        scan_at = kv.last_at + d;
    else
        keys_pass_extend(cells * SCAN_CELL_CYCLES);
}

/* A take was stored: the stopped grid runs from the end of the store, not from the screen
 * drawn after it. */
void keys_rec_stopped(void)
{
    rec_stop = 1;
    kv.stop_at = cycles_now() + SCAN_REC_STOP_PHASE;
}

/* After a long operation inside a pass: the next pass starts now. */
void keys_restart(void)
{
    rescan = 2;
}

/* Machine cycles, modulo 2^16, counting ticks not yet handled. */
uint16_t cycles_now(void)
{
    uint8_t hi, lo, n;
    do {
        hi = TH0;
        lo = TL0;
        n = g_ticks;
    } while (hi != TH0);
    return clk + (uint16_t)(((hi << 8) | lo) + t_len) + n * t_len;
}

/* Waits until n cycles after the stopped key action being handled was due. */
void keys_act_wait(uint16_t n)
{
    cycles_wait(act_at, n);
}

void cycles_wait(uint16_t from, uint16_t n)
{
    while ((uint16_t)(cycles_now() - from) < n)
        ;
}

uint8_t key_held(uint8_t k)
{
    return (down[k >> 3] >> (k & 7)) & 1;
}

/* Called from the main loop, whose stack depth leaves room for the deepest key action. */
#define f_r kv.r
#define f_bit kv.bit
#define f_k kv.k
#define f_d kv.d

void keys_flush(void)
{
    pending = 0;
    for (f_r = 0; f_r < 6; ++f_r) {
        f_d = b_state[f_r] ^ down[f_r];
        if (!f_d)
            continue;
        for (f_bit = 1, f_k = (uint8_t)(f_r << 3); f_bit; f_bit <<= 1, ++f_k) {
            if (!(f_d & f_bit))
                continue;
            if (b_state[f_r] & f_bit)
                ui_key_down(f_k);
            else
                ui_key_up(f_k);
        }
        down[f_r] = b_state[f_r];
    }
}

/* One pass of the original unit's main loop: once per sample when stopped, once per
 * REPEAT_CYCLES_RUN while running. */
static void pass(void)
{
    ui_pass();
    if (b_state[2] & 0x06) {
        if (!--repeat_n) {
            repeat_n = REPEAT_NEXT;
            ui_key_down((b_state[2] & 0x04) ? K_PLUS : K_MINUS);
        }
    } else if (!(down[0] & 0x03)) {
        repeat_n = REPEAT_FIRST;
    }
}

static uint16_t rec_pass(void)
{
    uint16_t n = rb_end - rb_start;
    return SCAN_CYCLES_REC + held_n * SCAN_HELD_CYCLES + n + n + n;
}

/* Keys down (active high) in the rows selected by sel. The lines settle about 8 cycles
 * before P1 is read (10 on the original), or a key held in one row shows in the next. */
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

/* Samples the rows into b_state and returns the rows changed. The footswitch (P3.3, low when
 * pressed unless it was at power-up) is row 2 bit 3. A row walk reaching three keys' worth of
 * activity keeps the rest as they were (r4 bit 7). A track key release (row 1) seen with conf
 * clear is kept down and sets conf; one seen with conf set is taken and sets f_d. The caller
 * keeps values in r4..r7. kr and kv share page 0x05: kv.d is kv+0x17, conf kv+0x1A. */
static uint8_t rows_sample(void) __naked
{
    __asm
        push    ar4
        push    ar5
        push    ar6
        mov     c,_P3_3
        cpl     c
        jb      _foot_init,00007$
        setb    _foot_init
        mov     _foot_inv,c
00007$:
        jnb     _foot_inv,00008$
        cpl     c
00008$:
        mov     _foot,c
        mov     r6,#0xfe
        mov     r5,#0
        mov     r4,#0
        mov     r3,#0
        mov     r2,#0
        mov     dptr,#(_kv + 0x1a)
        movx    a,@dptr
        mov     b,a
00001$:
        mov     r1,#0
        mov     a,r4
        jb      acc.7,00003$
        jnb     _any_down,00003$
        mov     dpl,r6
        lcall   _row_read
        mov     a,dpl
        jz      00003$
        mov     r1,a
        dec     a
        anl     a,r1
        jz      00002$
        inc     r4
00002$:
        inc     r4
        mov     a,r4
        add     a,#0xfd
        jnc     00003$
        mov     r4,#0x80
00003$:
        cjne    r5,#2,00004$
        mov     a,r1
        mov     c,_foot
        mov     acc.3,c
        mov     r1,a
00004$:
        mov     a,r5
        add     a,#_kr
        mov     dpl,a
        mov     dph,#(_kr >> 8)
        movx    a,@dptr
        mov     r0,a
        mov     a,r4
        jnb     acc.7,00006$
        mov     a,r0
        mov     r1,a
00006$:
        cjne    r5,#1,00005$
        mov     a,r1
        cpl     a
        anl     a,r0
        jz      00005$
        mov     r2,#1
        mov     a,b
        jnz     00005$
        mov     a,r1
        orl     a,r0
        mov     r1,a
00005$:
        mov     a,dpl
        add     a,#6
        mov     dpl,a
        mov     a,r1
        movx    @dptr,a
        xrl     a,r0
        orl     a,r3
        mov     r3,a
        inc     r5
        mov     a,r6
        setb    c
        rlc     a
        mov     r6,a
        cjne    r5,#6,00001$
        mov     a,r2
        anl     a,b
        mov     dptr,#(_kv + 0x17)
        movx    @dptr,a
        mov     a,b
        cpl     a
        anl     a,r2
        mov     dptr,#(_kv + 0x1a)
        movx    @dptr,a
        mov     dpl,r3
        pop     ar6
        pop     ar5
        pop     ar4
        ret
    __endasm;
}

uint8_t keys_scan(void)
{
    uint8_t d;
    __xdata uint16_t now;

#ifdef OPENMMT8_TAPE
    if (tape_run) {
        KEY_ROWS = 0x7E;
        if (!tape_aborted && !(P1 & 0x20)) {
            tape_aborted = 1;
            ui_dirty = 1;
            clock_apply();
        }
        return 0;
    }
#endif
    if (late && !pending) {
        late = 0;
        pass();
    }
    if (g_ticks && !rescan && tp_state != TP_STOP)
        return 0;
    now = cycles_now();
    if (rescan)
        scan_at = rescan == 1 ? kv.stop_at : now;
    if (pending && (int16_t)(now - act_at) >= 0)
        return 1;
    if (tp_state != TP_STOP)
        was_run = 1;
    else if (was_run) {
        was_run = 0;
        scan_at = rec_stop ? kv.stop_at : now + SCAN_STOP_PHASE;
        rec_stop = 0;
    }
    stop_grid = tp_state == TP_STOP;
    /* Contacts bounce for milliseconds: after a change the matrix is left alone for a pass
     * of the original, which samples it once per pass. */
    if ((int16_t)(now - scan_at) >= 0) {
        do {
            kv.last_at = scan_at;
            scan_at += tp_state == TP_STOP ? ui_cadence(any_down)
                       : tp_recording ? rec_pass() : SCAN_CYCLES_PLAY;
        } while ((int16_t)(now - scan_at) >= 0);
        if (tp_state == TP_STOP) {
            if (rescan == 2)
                late = 1;
            else
                pass();
        }
    }
    rescan = 0;
    any_down = row_read(0x80);
    if (pending || (uint16_t)(now - act_at) < REPEAT_CYCLES_STOP)
        return 0;
    act_at = now - REPEAT_CYCLES_STOP;

    /* A contact can drop out while held, and a track key toggles on each press: its release
     * counts, as due when first seen, only if it still reads released RELEASE_CONFIRM later,
     * so like the original a short dropout is not seen. Other keys act on release, at once. */
    d = rows_sample();
    if (conf)
        act_at = now - (REPEAT_CYCLES_STOP - RELEASE_CONFIRM);
    if (!d)
        return 0;
    pending = 1;
    act_at = now;
    if (f_d)
        act_at -= RELEASE_CONFIRM;
    return 1;
}

void keys_locate_start(void)
{
    loc_n = LOCATE_FIRST;
    loc_d = LOCATE_NEXT;
    loc_acc = 0;
}

/* Called once per timer0 tick with the tick length in machine cycles. */
void keys_tick(uint16_t cycles)
{
    uint16_t period = tp_state == TP_STOP ? REPEAT_CYCLES_STOP : REPEAT_CYCLES_RUN;
    uint8_t fr = down[0] & 0x03;
    clk += cycles;
    t_len = cycles;
    if (tp_recording)
        scan_at += SCAN_REC_TICK;
    if (fr == 0x01 || fr == 0x02) {
        for (loc_acc += cycles; loc_acc >= period; loc_acc -= period) {
            if (!--loc_n) {
                loc_n = loc_d;
                loc_d = loc_d > 1 ? loc_d - 1 : LOCATE_MIN;
                ui_locate_step(fr == 0x02);
            }
        }
    }
    if (tp_state != TP_STOP || (fn == K_CLICK && (b_state[2] & 0x06)))
        for (repeat_acc += cycles; repeat_acc >= period; repeat_acc -= period)
            pass();
}
