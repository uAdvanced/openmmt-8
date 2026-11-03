/* Sequence memory and transport.
 *
 * Memory model (battery-backed, same layout as the original unit):
 *   0x0400  part table: 100 big-endian record pointers, hi byte 0 = part absent
 *   0x0502  song table: 100 big-endian record pointers
 *   0x0600  heap of part/song records up to cfg.heap_end; free space up to 0xFF00
 *
 * Part record:
 *   +0x00 size (LE)          +0x02 8 track offsets (LE, from record start)
 *   +0x12 length, BCD beats+1 (0 = no length yet)
 *   +0x14 8 track MIDI channels (0 = unchanged)   +0x1C 14-char name
 *   Offsets and channels are indexed by slot = 7 - track.
 *   +0x2A track event lists, each closed by an end event
 * Event (7 bytes): b0 = 0x80|note (or 0x80|controller, FA program, FB aftertouch,
 *   FC pitch bend, FD system exclusive), b1..b2 time (LE ticks), b3 velocity (bit7 set =
 *   non-note event), b4 channel, b5..b6 duration (BE). End of track: b0 = 0x80, b4 bit7.
 * System exclusive data is packed three bytes per cell: FD t t d|80 d 02 d, continued by
 *   5-byte cells 7D d|80 d 02 d; a data byte of 0x80 in the second/third slot ends it.
 */
#include <stddef.h>
#include "core.h"

#define P_EMPTY_SIZE (P_DATA + 8 * EV)
/* Naked, and they touch none of r0-r7: the compiler does not save those around the call. */
static uint16_t rd16(xptr p) __naked
{
    p;
    __asm
        movx    a, @dptr
        push    acc
        inc     dptr
        movx    a, @dptr
        mov     dph, a
        pop     acc
        mov     dpl, a
        ret
    __endasm;
}

static void wr16(xptr p, uint16_t v) __naked
{
    p; v;
    __asm
        mov     a, _wr16_PARM_2
        movx    @dptr, a
        inc     dptr
        mov     a, (_wr16_PARM_2 + 1)
        movx    @dptr, a
        ret
    __endasm;
}

static xptr tab_get(uint16_t slot) __naked
{
    slot;
    __asm
        movx    a, @dptr
        jz      00001$
        push    acc
        inc     dptr
        movx    a, @dptr
        mov     dpl, a
        pop     acc
        mov     dph, a
        ret
00001$:
        mov     dptr, #0
        ret
    __endasm;
}

static void tab_set(uint16_t slot, uint16_t v) __naked
{
    slot; v;
    __asm
        mov     a, (_tab_set_PARM_2 + 1)
        movx    @dptr, a
        inc     dptr
        mov     a, _tab_set_PARM_2
        movx    @dptr, a
        ret
    __endasm;
}

xptr part_rec(uint8_t n)
{
    return tab_get(PART_TABLE + 2u * n);
}

xptr song_rec(uint8_t n)
{
    return tab_get(SONG_TABLE + 2u * n);
}

uint16_t heap_free(void)
{
    return HEAP_TOP - cfg.heap_end;
}

void mem_move(uint16_t dst, uint16_t src, uint16_t n)
{
    xptr d, s;
    if (!n || dst == src)
        return;
    if (dst < src) {
        d = (xptr)dst;
        s = (xptr)src;
        do
            *d++ = *s++;
        while (--n);
    } else {
        d = (xptr)(dst + n);
        s = (xptr)(src + n);
        do
            *--d = *--s;
        while (--n);
    }
}

/* Shift table pointers at or above `at` by `delta` (records after an insert/delete point). */
static void tab_fix(uint16_t at, int16_t delta)
{
    xptr t = (xptr)PART_TABLE;
    uint16_t v;
    do {
        if (t == (xptr)(PART_TABLE + 200u))
            t = (xptr)SONG_TABLE;
        if (t[0]) {
            v = (t[0] << 8) | t[1];
            if (v >= at) {
                v += (uint16_t)delta;
                t[0] = (uint8_t)(v >> 8);
                t[1] = (uint8_t)v;
            }
        }
        t += 2;
    } while (t != (xptr)(SONG_TABLE + 200u));
}

static void heap_insert(uint16_t at, uint16_t n)
{
    mem_move(at + n, at, cfg.heap_end - at);
    cfg.heap_end += n;
    tab_fix(at, (int16_t)n);
}

static void heap_delete(uint16_t at, uint16_t n)
{
    mem_move(at, at + n, cfg.heap_end - at - n);
    cfg.heap_end -= n;
    tab_fix(at + n, -(int16_t)n);
}

static uint16_t bcd16(uint16_t v) __naked
{
    v;
    __asm
        push    psw
        push    b
        mov     b, #0
00001$:
        clr     c
        mov     a, dpl
        subb    a, #100
        push    acc
        mov     a, dph
        subb    a, #0
        jc      00002$
        mov     dph, a
        pop     acc
        mov     dpl, a
        inc     b
        sjmp    00001$
00002$:
        pop     acc
        clr     a
00003$:
        push    acc
        clr     c
        mov     a, dpl
        subb    a, #10
        jc      00004$
        mov     dpl, a
        pop     acc
        inc     a
        sjmp    00003$
00004$:
        pop     acc
        swap    a
        orl     a, dpl
        mov     dpl, a
        mov     dph, b
        pop     b
        pop     psw
        ret
    __endasm;
}

/* Leaf (no calls): it sits at the bottom of the deepest main loop paths.
 * Hundreds*100 + tens*10 + ones, then one less. Registers stay put. */
uint16_t part_len(xptr p) __naked
{
    p;
    __asm
        push    psw
        push    b
        mov     a, #0x12
        add     a, dpl
        mov     dpl, a
        clr     a
        addc    a, dph
        mov     dph, a
        movx    a, @dptr
        mov     b, a
        inc     dptr
        movx    a, @dptr
        orl     a, b
        jnz     00001$
        mov     dptr, #0
        sjmp    00009$
00001$:
        movx    a, @dptr
        mov     dptr, #0
        jz      00003$
00002$:
        push    acc
        mov     a, dpl
        add     a, #100
        mov     dpl, a
        mov     a, dph
        addc    a, #0
        mov     dph, a
        pop     acc
        add     a, #0xff
        jnz     00002$
00003$:
        mov     a, b
        swap    a
        anl     a, #0x0f
        jz      00005$
00004$:
        push    acc
        mov     a, dpl
        add     a, #10
        mov     dpl, a
        mov     a, dph
        addc    a, #0
        mov     dph, a
        pop     acc
        add     a, #0xff
        jnz     00004$
00005$:
        mov     a, b
        anl     a, #0x0f
        add     a, dpl
        mov     dpl, a
        clr     a
        addc    a, dph
        mov     dph, a
        mov     a, dpl
        add     a, #0xff
        mov     dpl, a
        mov     a, dph
        addc    a, #0xff
        mov     dph, a
00009$:
        pop     b
        pop     psw
        ret
    __endasm;
}

xptr cur_part(void)
{
    return part_rec(cfg.part);
}

/* Beats of the selected part, 0 when it is empty or has no length. */
uint16_t cur_len(void)
{
    xptr p = cur_part();
    return p ? part_len(p) : 0;
}

const uint8_t __code bit_tab[8] = {1, 2, 4, 8, 16, 32, 64, 128};

/* Track slots are stored in reverse order: track 1 is slot 7.
 * The slot is a little-endian offset from the part. Registers stay put. */
xptr track(xptr p, uint8_t t) __naked
{
    p; t;
    __asm
        push    psw
        push    b
        push    ar0
        push    ar1
        mov     r0, dpl
        mov     r1, dph
        mov     a, #7
        clr     c
        subb    a, _track_PARM_2
        rl      a
        add     a, #2
        add     a, r0
        mov     dpl, a
        clr     a
        addc    a, r1
        mov     dph, a
        movx    a, @dptr
        mov     b, a
        inc     dptr
        movx    a, @dptr
        xch     a, b
        add     a, r0
        mov     dpl, a
        mov     a, b
        addc    a, r1
        mov     dph, a
        pop     ar1
        pop     ar0
        pop     b
        pop     psw
        ret
    __endasm;
}

uint8_t ev_end(xptr e) __naked
{
    e;
    __asm
        movx    a, @dptr
        cjne    a, #0x80, 00001$
        inc     dptr
        inc     dptr
        inc     dptr
        inc     dptr
        movx    a, @dptr
        jnb     acc.7, 00001$
        mov     dpl, #1
        ret
00001$:
        mov     dpl, #0
        ret
    __endasm;
}

/* ev_end of a pointer stored in xdata. The load repeats at every call site. */
static uint8_t end_at(__xdata uint8_t *slot) __naked
{
    slot;
    __asm
        push    ar5
        push    ar6
        push    ar7
        movx    a, @dptr
        mov     r6, a
        inc     dptr
        movx    a, @dptr
        mov     r7, a
        mov     dpl, r6
        mov     dph, r7
        lcall   _ev_end
        pop     ar7
        pop     ar6
        pop     ar5
        ret
    __endasm;
}

/* An event is 7 bytes: code (bit 7 set), time, then a 4-byte body; one at the time of the
 * event before it is 5 bytes: code with bit 7 clear, then the body. */
xptr ev_next(xptr e) __naked
{
    e;
    __asm
        movx    a, @dptr
        jnb     acc.7, 00001$
        mov     a, #7
        sjmp    00002$
00001$:
        mov     a, #5
00002$:
        add     a, dpl
        mov     dpl, a
        clr     a
        addc    a, dph
        mov     dph, a
        ret
    __endasm;
}

/* The first event of the track at e not before tick `at` (or its end). */
static xptr ev_seek(xptr e, uint16_t at)
{
    while (!ev_end(e) && (!(e[0] & 0x80) || rd16(e + 1) < at))
        e = ev_next(e);
    return e;
}

xptr ev_body(xptr e) __naked
{
    e;
    __asm
        movx    a, @dptr
        jnb     acc.7, 00001$
        mov     a, #3
        sjmp    00002$
00001$:
        mov     a, #1
00002$:
        add     a, dpl
        mov     dpl, a
        clr     a
        addc    a, dph
        mov     dph, a
        ret
    __endasm;
}

static xptr track_end(xptr e)
{
    while (!ev_end(e))
        e = ev_next(e);
    return e;
}

void part_set_len(xptr rec, uint16_t beats)
{
    static __xdata xptr p;
    static __xdata uint16_t ticks;
    static __xdata uint8_t t;
    p = rec;
    ticks = beats * TICKS_PER_BEAT;
    wr16(p + P_LEN, bcd16(beats + 1u));
    for (t = 0; t < 8; ++t)
        wr16(track_end(track(p, t)) + 1, ticks);
}

static const char __code no_part_name[] = " no name yet  ";

/* Blank part: size, eight track offsets, the name, eight end events. Zeros stay zero. */
static void fill_empty(xptr p) __naked
{
    p;
    __asm
        ; part_create keeps the record address in r3:r4 across this call.
        push    ar3
        push    ar4
        mov     r0, dpl
        mov     r1, dph
        mov     r2, #0x62
        clr     a
00001$:
        movx    @dptr, a
        inc     dptr
        djnz    r2, 00001$
        mov     dpl, r0
        mov     dph, r1
        mov     a, #0x62
        movx    @dptr, a
        inc     dptr
        inc     dptr
        mov     r3, #0x2a
        mov     r2, #8
00002$:
        mov     a, r3
        movx    @dptr, a
        inc     dptr
        inc     dptr
        mov     a, #7
        add     a, r3
        mov     r3, a
        djnz    r2, 00002$
        mov     a, #10
        add     a, dpl
        mov     dpl, a
        clr     a
        addc    a, dph
        mov     dph, a
        mov     r4, dpl
        mov     r5, dph
        mov     r2, #14
        mov     r3, #0
00003$:
        mov     a, r3
        mov     dptr, #_no_part_name
        movc    a, @a+dptr
        mov     dpl, r4
        mov     dph, r5
        movx    @dptr, a
        inc     r4
        cjne    r4, #0, 00004$
        inc     r5
00004$:
        inc     r3
        djnz    r2, 00003$
        mov     dpl, r4
        mov     dph, r5
        mov     r2, #8
00005$:
        mov     a, #0x80
        movx    @dptr, a
        inc     dptr
        mov     a, #0xff
        movx    @dptr, a
        inc     dptr
        movx    @dptr, a
        inc     dptr
        inc     dptr
        mov     a, #0x80
        movx    @dptr, a
        inc     dptr
        inc     dptr
        inc     dptr
        djnz    r2, 00005$
        pop     ar4
        pop     ar3
        ret
    __endasm;
}

static void part_create(uint8_t n)
{
    uint16_t at = cfg.heap_end;
    xptr p = (xptr)at;

    cfg.heap_end += P_EMPTY_SIZE;
    fill_empty(p);
    p = (xptr)(PART_TABLE + 2u * n);
    p[0] = (uint8_t)(at >> 8);
    p[1] = (uint8_t)at;
}

static void part_drop(uint8_t n)
{
    xptr p = part_rec(n);
    if (!p)
        return;
    ((xptr)(PART_TABLE + 2u * n))[0] = 0;
    heap_delete((uint16_t)p, rd16(p + P_SIZE));
}

/* STOP/CONTINUE: song position (16ths) to resume from, and the notes sounding at STOP
 * (kept in the transport's note-on table with the remaining duration). */
__xdata uint16_t stop_spp;
static __xdata uint8_t stop_on_n;

/* The original keeps the part last selected or recorded open in its memory; a part-wide
 * ERASE first closes it, which takes a moment before the message shows. */
__bit part_open;

void part_select(uint8_t n)
{
    part_open = 1;
    stop_spp = 0;
    stop_on_n = 0;
    xptr p = cur_part();
    if (p && !part_len(p))
        part_drop(cfg.part);
    cfg.part = n;
    if (!part_rec(n))
        part_create(n);
}

/* Leaving part mode releases the current part's record if it was never used. */
void part_release(void)
{
    xptr p = cur_part();
    if (p && !part_len(p))
        part_drop(cfg.part);
}

static const char __code no_song_name[] = " no name yet  ";

static xptr song_create(uint8_t n)
{
    uint16_t at = cfg.heap_end;
    xptr s = (xptr)at;
    uint8_t i;

    cfg.heap_end += S_STEPS + 1;
    wr16(s, S_STEPS + 1);
    s[S_TEMPO] = 120;
    for (i = 0; i < 14; ++i)
        s[S_NAME + i] = no_song_name[i];
    s[S_STEPS] = 0xFF;
    tab_set(SONG_TABLE + 2u * n, at);
    return s;
}

uint8_t song_steps(xptr s)
{
    uint8_t n = 0;
    if (!s)
        return 0;
    for (s += S_STEPS; !(*s & 0x80); s += 2)
        ++n;
    return n;
}

/* Insert `part` (all tracks on) before step `i` of song n, creating the song if needed. */
void song_insert_step(uint8_t n, uint8_t i, uint8_t part)
{
    xptr s = song_rec(n);
    uint16_t at;
    if (!s)
        s = song_create(n);
    at = (uint16_t)s + S_STEPS + 2u * i;
    heap_insert(at, 2);
    s = song_rec(n);
    wr16(s, rd16(s) + 2);
    ((xptr)at)[0] = part;
    ((xptr)at)[1] = 0xFF;
}

static void song_drop_empty(void)
{
    xptr s = song_rec(cfg.song);
    if (s && !song_steps(s)) {
        ((xptr)(SONG_TABLE + 2u * cfg.song))[0] = 0;
        heap_delete((uint16_t)s, rd16(s));
    }
}

/* The selected song always has a record; one left without steps is released. */
void song_select(uint8_t n)
{
    xptr s;
    song_drop_empty();
    cfg.song = n;
    s = song_rec(n);
    if (!s) {
        song_create(n);
        return;
    }
    cfg.tempo = s[S_TEMPO];
    timer_set_tempo(cfg.tempo);
}

void song_release(void)
{
    song_drop_empty();
}

/* COPY in song mode: an absent destination becomes a copy of the source record, otherwise
 * the source steps are appended to it. */
uint8_t song_copy(void)
{
    __xdata uint16_t n, at;
    __xdata xptr d;
    n = 2u * song_steps(song_rec(op.src));
    if (!n)
        return 1;
    d = song_rec(op.dst);
    at = d ? n : n + S_STEPS + 1u;
    if (heap_free() < at)
        return 0;
    if (!d) {
        tab_set(SONG_TABLE + 2u * op.dst, cfg.heap_end);
        n = (uint16_t)song_rec(op.src);
        mem_move(cfg.heap_end, n, at);
        cfg.heap_end += at;
        return 1;
    }
    at = (uint16_t)d + rd16(d) - 1u;
    heap_insert(at, n);
    d = song_rec(op.dst);
    wr16(d, rd16(d) + n);
    mem_move(at, (uint16_t)song_rec(op.src) + S_STEPS, n);
    return 1;
}

void song_erase(uint8_t n)
{
    xptr s = song_rec(n);
    uint16_t k = 2u * song_steps(s);
    if (!k)
        return;
    heap_delete((uint16_t)s + S_STEPS, k);
    wr16(s, rd16(s) - k);
}

void song_erase_step(uint8_t n, uint8_t i)
{
    xptr s = song_rec(n);
    heap_delete((uint16_t)s + S_STEPS + 2u * i, 2);
    wr16(s, rd16(s) - 2);
}

#ifdef OPENMMT8_TAPE
void tape_heap_reset(void) __naked
{
    __asm
        mov     a, #0
        mov     dptr, #0x0400
        mov     r7, #200
thr0$:
        movx    @dptr, a
        inc     dptr
        djnz    r7, thr0$
        mov     dptr, #0x0502
        mov     r7, #200
thr1$:
        movx    @dptr, a
        inc     dptr
        djnz    r7, thr1$
        mov     dptr, #0x04e7
        movx    @dptr, a
        inc     dptr
        mov     a, #6
        movx    @dptr, a
        ret
    __endasm;
}
#endif

/* Without the magic, all memory starts over. CFG_SETTINGS (ERASE held at power-on) resets
 * only the settings, keeping the parts, songs and the heap end. */
/* Zeroes PART_TABLE up to HEAP_BASE and sets cfg.heap_end to HEAP_BASE. In assembly: SDCC
 * reverses the equivalent C loop and clears 0x0000-0x01FF instead. */
static void work_clear(void) __naked
{
    __asm
        mov     dptr, #0x0400
00001$:
        clr     a
        movx    @dptr, a
        inc     dptr
        mov     a, dph
        cjne    a, #0x06, 00001$
        mov     dptr, #0x04e8
        movx    @dptr, a
        ret
    __endasm;
}

static const cfg_t __code cfg_default = {
    .magic = CFG_MAGIC, .tempo = 120, .click_val = 1, .rec_click = 1, .countdown = 4,
    .quant_val = 5, .filt = {1, 1, 0, 1, 1, 0}, .clock_out = 1, .auto_start = 1
};

void seq_init(void)
{
    uint8_t m = cfg.magic, i;
    if (m == CFG_MAGIC)
        return;
    if (m != CFG_SETTINGS) {
        work_clear();
        part_create(0);
    }
    for (i = 0; i < offsetof(cfg_t, heap_end); ++i)
        ((xptr)&cfg)[i] = ((const __code uint8_t *)&cfg_default)[i];
#ifdef OPENMMT8_EDITX
    for (i = 0; i < sizeof(edopt_t); ++i)
        ((xptr)&edopt)[i] = 0;
#endif
}

/* ---- transport ---- */

/* MIDI real time out, sent only with MIDI CLOCK OUT enabled. */
static void rt_tx(uint8_t b)
{
    if (cfg.clock_out)
        midi_tx(b);
}

/* Continue requested by a received MIDI CONTINUE; a song position was received since. */
static __xdata uint8_t sync_cont, sync_spp;
static __xdata uint16_t stop_tick; /* position playback stopped at */

__data uint8_t tp_state;
__bit tp_recording;
__bit rec_screen;
__bit rec_armed;
__xdata uint8_t rec_track;
__xdata uint8_t count_left;
__xdata uint16_t tp_pos;
__xdata uint16_t tp_beat;
__xdata uint8_t next_part;
__xdata uint8_t mute;
__xdata uint8_t play_part;
__xdata uint8_t song_step;
__xdata uint8_t count_ticks;
static __xdata uint8_t clk_n, clk_per;
static const uint8_t __code click_ticks[10] = { 192, 96, 64, 48, 32, 24, 16, 12, 8, 6 };

/* 16-bit remainder modulo 96, in place of __moduint. */
uint8_t mod96(uint16_t n) __naked
{
    n;
    __asm
        mov     r3, dpl
        mov     r4, dph
        mov     r2, #0
        mov     r1, #16
00001$:
        clr     c
        mov     a, r3
        rlc     a
        mov     r3, a
        mov     a, r4
        rlc     a
        mov     r4, a
        mov     a, r2
        rlc     a
        mov     r2, a
        clr     c
        mov     a, r2
        subb    a, #96
        jc      00002$
        mov     r2, a
00002$:
        djnz    r1, 00001$
        mov     dpl, r2
        ret
    __endasm;
}
#define q_grid click_ticks

static void click(void)
{
    P3_5 = !P3_5;
    P3_5 = !P3_5;
}
static __xdata uint16_t part_ticks; /* 0 = part has no length yet */
static __xdata xptr trk_at[8];
static __xdata uint8_t trk_chan[8];
static __xdata uint8_t end_event[EV] = {0x80, 0xFF, 0xFF, 0, 0x80, 0, 0};

#define MAX_ON 32
static __xdata uint8_t on_n;
static __xdata uint8_t on_st[MAX_ON], on_note[MAX_ON], on_vel[MAX_ON];
static __xdata uint16_t on_off[MAX_ON];

static void part_start(uint8_t n)
{
    xptr p = part_rec(n);
    uint8_t t;
    play_part = n;
    tp_pos = 0;
    part_ticks = 0;
    for (t = 0; t < 8; ++t) {
        trk_at[t] = end_event;
        trk_chan[t] = 0;
    }
    if (!p)
        return;
    part_ticks = part_len(p) * TICKS_PER_BEAT;
    for (t = 0; t < 8; ++t) {
        trk_at[t] = track(p, t);
        trk_chan[t] = p[P_CHAN + 7 - t];
    }
}


/* ---- record capture ----
 * Events are appended at the heap end while recording and moved into the
 * record track when recording ends. */

#define MAX_HELD 16
__xdata uint16_t rb_start, rb_end;
__xdata uint8_t held_n;
static __xdata uint16_t held_at[MAX_HELD];

/* Held notes while recording share memory with the scratch of the part operations, which
 * only run while stopped. */
static __xdata union {
    struct {
        uint16_t t0[MAX_HELD], s0[MAX_HELD];
    } rec;
    struct {
        xptr p, w_at, q_p, qn_p, qo_p;
        uint16_t dt, at, w_t, w_last, q_t, qn_t, qo_t, q_ns, q_nd;
        uint16_t n[8];
        uint8_t t, sel;
    } op;
    struct {
#ifdef OPENMMT8_EDITX
        uint8_t op[48], ev[EV], u[EV]; /* past the part operation scratch */
#else
        uint8_t op[48], ev[EV]; /* past the part operation scratch */
#endif
    } ed;
} scr;
#define held_t0 scr.rec.t0
#define held_s0 scr.rec.s0
#define HELD_T0_MASK 0x3FFFu
#define HELD_CI 0x8000u
#define HELD_PUNCH 0x4000u
static __xdata uint8_t rec_ci_play_notes;
#define o_p scr.op.p
#define o_t scr.op.t

/* track(o_p, o_t) repeats a long load. One function is shorter. */
static xptr o_track(void)
{
    return track(o_p, o_t);
}

static void use_part(uint8_t n)
{
    o_p = part_rec(n);
}
#define o_sel scr.op.sel
#define o_dt scr.op.dt
#define o_at scr.op.at
#define w_at scr.op.w_at
#define w_t scr.op.w_t
#define w_last scr.op.w_last
#define q_p scr.op.q_p
#define qn_p scr.op.qn_p
#define qo_p scr.op.qo_p
#define q_t scr.op.q_t
#define qn_t scr.op.qn_t
#define qo_t scr.op.qo_t
#define q_ns scr.op.q_ns
#define q_nd scr.op.q_nd

/* While a system exclusive stream is open across a tick, the original's once-per-tick
 * record pass handles only the stream, up to the first tick after it closes: held notes
 * do not age on those ticks and other messages wait for the next full pass. */
#define MAX_DEFER 8
static __bit sx_hold, sx_seen;
static __xdata uint16_t rec_stall;
static __xdata uint8_t defer_n;
static __xdata uint8_t defer_st[MAX_DEFER], defer_a[MAX_DEFER], defer_b[MAX_DEFER];

/* Track t of part p grew by `delta` bytes: the size and every later slot's offset move. */
static void slots_shift(xptr p, uint8_t t, int16_t delta)
{
    uint8_t s = 8u - t;
    xptr q = p + P_SIZE;
    uint16_t v;
    for (;;) {
        v = (uint16_t)(q[0] | (q[1] << 8)) + (uint16_t)delta;
        q[0] = (uint8_t)v;
        q[1] = (uint8_t)(v >> 8);
        if (s == 8)
            return;
        q = p + P_OFF + 2u * s++;
    }
}

/* Set by RECORD on a first pass: capture stops, the length is still set at STOP. */
static __xdata uint8_t rec_off;

/* Capture opens with the last count-in beat: what arrives then is recorded at time 0. */
static __bit ci_cap;
static __bit rec_ci_take;
static __bit rx_st_new; /* set when the current MIDI message began with a status byte */
static __xdata xptr rec_at; /* where a punched-in take goes in the record track */
static __bit rec_punch;

static uint16_t rec_now(void);

static void rec_open(void)
{
    rb_start = rb_end = cfg.heap_end;
    held_n = 0;
    rec_stall = 0;
    defer_n = 0;
    sx_hold = 0;
    rec_off = 0;
    ci_cap = 1;
#ifdef OPENMMT8_EDITX
    ed_undo_ok = 0; /* the held notes overwrite the undo copy */
#endif
}

/* Recording starts with the part: like tape, the take erases the record track only up to
 * where it ends (see rec_commit), and the track is silent meanwhile. */
static void rec_begin(void)
{
    uint8_t i;
    rec_ci_take = ci_cap;
    if (!ci_cap)
        rec_open();
    ci_cap = 0;
    if (rec_ci_take) {
        uint16_t t0 = rec_now();
        rec_ci_play_notes = 0;
        for (i = 0; i < held_n; ++i) {
            held_t0[i] = t0 | (held_t0[i] & (HELD_CI | HELD_PUNCH));
            held_s0[i] = rec_stall;
        }
    }
    trk_at[rec_track] = end_event;
    tp_recording = 1;
    rec_screen = 1;
}

/* Record time: the tick being played. */
static uint16_t rec_now(void)
{
    return tp_pos ? tp_pos - 1 : 0;
}

/* Record time of received MIDI: the tick the last byte arrived in. */
static uint16_t rx_now(void)
{
    uint8_t lag = t_count - rx_t;
    return tp_pos > lag ? tp_pos - 1 - lag : 0;
}

/* Event time stamp: first data byte of the message (running status). */
static uint16_t rx_stamp(void)
{
    uint8_t lag = t_count - rx_t_msg;
    return tp_pos > lag ? tp_pos - 1 - lag : 0;
}

/* Written event time: count-in takes adjust vs rx_stamp(); held notes still use rx_now(). */
static uint16_t rec_stamp(void)
{
    uint16_t t = rx_stamp();
    if (!tp_recording || !rec_ci_take)
        return t;
    /* Notes past the first beat keep this return: it is the hot path, and an
     * earlier cfg.loop test shifts echo's event order. */
    if (!stop_tick && t > TICKS_PER_BEAT)
        return t - 1u;
    if (stop_tick && t <= TICKS_PER_BEAT)
        return cfg.loop ? t : (uint16_t)(t - 1u);
    if (!stop_tick && cfg.loop && t)
        return t - 1u;
    return t;
}

static void mem_rev(uint16_t lo, uint16_t hi)
{
    xptr a = (xptr)lo, b = (xptr)hi;
    uint8_t c;
    while (a < --b) {
        c = *a;
        *a++ = *b;
        *b = c;
    }
}

/* Duration of held note i released at `now`: a note from the count-in counts from tick 1,
 * and one released there lasts a tick. */
static uint16_t held_dur(uint8_t i, uint16_t now)
{
    uint16_t d = now - (held_t0[i] & HELD_T0_MASK) - (rec_stall - held_s0[i]);
    return d ? d + 1u : 1u;
}

static void held_put(xptr e, uint8_t i, uint16_t now)
{
    uint16_t d = held_dur(i, now);
    if (d > 1u && !(held_t0[i] & HELD_PUNCH) && (tp_recording || d <= 26u)) {
        if (held_t0[i] & HELD_CI) {
            --d;
            if (rec_ci_take && d > 25u)
                --d;
        } else if (!rec_ci_take)
            --d;
        else if (d == 27u)
            --d;
    }
    if ((held_t0[i] & HELD_PUNCH) && rec_off == 1u)
        d += 3u;
    else if (rec_off == 2u && d > 26u)
        d += 2u;
    else if (rec_stall != held_s0[i])
        d += 2u;
    e[5] = (uint8_t)(d >> 8);
    e[6] = (uint8_t)d;
}

static void rec_close_held(void)
{
    while (held_n) {
        xptr e = (xptr)held_at[--held_n];
        held_put(e, held_n, rec_now());
    }
}

/* Punch out: store the captured events as the record track. */
/* The record track of p grew by d bytes at `at`: its slots and the play pointers move. */
static void trk_grow(xptr p, uint16_t at, int16_t d)
{
    uint8_t t;
    slots_shift(p, rec_track, d);
    for (t = 0; t < 8; ++t)
        if (trk_at[t] != end_event && (uint16_t)trk_at[t] >= at)
            trk_at[t] += d;
}

static void rec_commit(void)
{
    __xdata xptr p = cur_part();
    __xdata uint16_t at, n, beats = 0, lim;

    if (!tp_recording)
        return;
    ui_led_rec_off();
    tp_recording = 0;
    part_open = 1;
    rec_close_held();
    if (!part_ticks) {
        beats = (tp_pos + TICKS_PER_BEAT / 2 - 1) / TICKS_PER_BEAT;
        if (!beats)
            beats = 1;
        lim = beats * TICKS_PER_BEAT;
        for (at = rb_start; at < rb_end && (!(*(xptr)at & 0x80) || rd16((xptr)at + 1) < lim);
             at = (uint16_t)ev_next((xptr)at))
            ;
        rb_end = at;
    }
    n = rb_end - rb_start;
    at = (uint16_t)track(p, rec_track);
    if (rec_punch) {
        rec_punch = 0;
        at = (uint16_t)rec_at;
    }
    if (n) {
        mem_rev(at, rb_end);
        mem_rev(at, at + n);
        mem_rev(at + n, rb_end);
        cfg.heap_end = rb_end;
        tab_fix(at, (int16_t)n);
        trk_grow(p, at, (int16_t)n);
    }
    /* The old events up to where the take ended are erased; the rest play on. */
    at += n;
    n = (uint16_t)ev_seek((xptr)at, tp_pos) - at;
    if (n) {
        heap_delete(at, n);
        trk_grow(p, at, -(int16_t)n);
    }
    trk_at[rec_track] = (xptr)at;
    if (beats) {
        part_set_len(p, beats);
        part_ticks = beats * TICKS_PER_BEAT;
    }
    ui_dirty = 1;
}

static __xdata uint8_t rec_b5;

static void rec_put(uint8_t b0, uint8_t b3, uint8_t ch, uint8_t b6)
{
    xptr e = (xptr)rb_end;
    if (rb_end <= HEAP_TOP - 2 * EV) {
        e[0] = b0;
        wr16(e + 1, rec_stamp());
        e[3] = b3;
        e[4] = ch;
        e[5] = rec_b5;
        e[6] = b6;
        rb_end += EV;
    }
    rec_b5 = 0;
}

/* System exclusive capture: cell slot of the next data byte (0 = new cell). */
static __bit sx_rec, sx_first;
static __xdata uint8_t sx_slot;

static void rec_sysex_byte(uint8_t d)
{
    xptr w = (xptr)rb_end;
    if (rb_end > HEAP_TOP - 3 * EV)
        return;
    sx_seen = 1;
    if (!sx_slot) {
        if (sx_first) {
            sx_first = 0;
            rx_t_msg = rx_t;
            w[0] = 0xFD;
            {
                uint16_t t = rec_stamp();
                if (t)
                    --t;
                wr16(w + 1, t);
            }
            w += 3;
        } else {
            *w++ = 0x7D;
        }
        *w++ = d | 0x80;
        sx_slot = 1;
    } else if (sx_slot == 1) {
        *w++ = d;
        *w++ = 0x02;
        sx_slot = 2;
    } else {
        *w++ = d;
        sx_slot = 0;
    }
    rb_end = (uint16_t)w;
}

static void rec_sysex_end(void)
{
    xptr w = (xptr)rb_end;
    sx_rec = 0;
    if (sx_slot == 1) {
        w[0] = 0x80;
        w[1] = 0x01;
        w[2] = 0;
        rb_end += 3;
    } else if (sx_slot == 2) {
        w[0] = 0x80;
        rb_end += 1;
    }
    sx_slot = 0;
}

static void rec_event(uint8_t st, uint8_t a, uint8_t b)
{
    uint8_t ch = st & 15, i;
    if (!(tp_recording || ci_cap) || rec_off || (cfg.filt_chan && ch != cfg.filt_chan - 1))
        return;
    switch (st & 0xF0) {
    case 0x90:
        if (b) {
            if (!cfg.filt[0])
                return;
            if (held_n < MAX_HELD) {
                held_at[held_n] = rb_end;
                held_s0[held_n] = rec_stall;
                held_t0[held_n] = (uint16_t)((rx_st_new ? rx_now() : rx_stamp()) + !tp_recording)
                    | (!tp_recording ? HELD_CI : 0);
                if (tp_recording && rec_ci_take && ++rec_ci_play_notes > 1u)
                    held_t0[held_n] |= HELD_PUNCH;
                ++held_n;
            }
            rec_put(0x80 | a, b, ch, 0);
            return;
        }
        /* fall through: note-on with velocity 0 */
    case 0x80:
        for (i = 0; i < held_n; ++i) {
            __xdata xptr e = (xptr)held_at[i];
            if (e[0] == (0x80 | a) && e[4] == ch) {
                held_put(e, i, rx_now());
                --held_n;
                held_at[i] = held_at[held_n];
                held_t0[i] = held_t0[held_n];
                held_s0[i] = held_s0[held_n];
                return;
            }
        }
        return;
    case 0xB0:
        /* channel mode messages (122 and up) are not recorded */
        if (cfg.filt[3] && a < 0x7A)
            rec_put(0x80 | a, 0x80 | b, ch, 0);
        return;
    case 0xC0:
        if (cfg.filt[4])
            rec_put(0xFA, 0x80 | a, ch, 0);
        return;
    case 0xD0:
        if (cfg.filt[2])
            rec_put(0xFB, 0x80 | a, ch, 0);
        return;
    case 0xE0:
        if (cfg.filt[1]) {
            rec_b5 = a;
            rec_put(0xFC, 0x80, ch, b);
        }
        return;
    }
}

/* The per-tick record pass. */
static void rec_flush(void)
{
    uint8_t i;
    if (sx_hold) {
        ++rec_stall;
        if (!sx_rec)
            sx_hold = 0;
    } else {
        for (i = 0; i < defer_n; ++i)
            rec_event(defer_st[i], defer_a[i], defer_b[i]);
        defer_n = 0;
        sx_hold = sx_seen && sx_rec;
    }
    sx_seen = 0;
}

/* ---- COPY / ERASE ----
 * Operands come from `op` (set by the panel). op.sel is the ERASE/COPY page: 0 all data,
 * 1 notes, 2 pitch bend, 3 aftertouch, 4 controllers, 5 program changes, 6 system
 * exclusive, 7..22 MIDI channel 1..16. */
__xdata op_t op;

/* Event class, in the order of the COPY/ERASE pages: 1 note, 2 pitch bend, 3 aftertouch,
 * 4 controller, 5 program change, 6 system exclusive. */
uint8_t ev_kind(xptr e)
{
    if (!(e[e[0] & 0x80 ? 3 : 1] & 0x80))
        return 1;
    switch (e[0] | 0x80) {
    case 0xFD:
        return 6;
    case 0xFC:
        return 2;
    case 0xFB:
        return 3;
    case 0xFA:
        return 5;
    }
    return 4;
}

/* Event writer: event e at w_at with time w_t, in the short form after an event of the
 * same time (w_last). Naked: r6:r7 stay put. w_at is scr+0x02, w_t scr+0x0E, w_last scr+0x10. */
static void ev_put(xptr e) __naked
{
    e;
    __asm
        mov     r2, dpl
        mov     r3, dph
        movx    a, @dptr
        mov     r4, a
        mov     dptr, #(_scr + 0x000e)
        movx    a, @dptr
        mov     r5, a
        inc     dptr
        movx    a, @dptr
        mov     b, a
        inc     dptr
        movx    a, @dptr
        xrl     a, r5
        jnz     00001$
        inc     dptr
        movx    a, @dptr
        xrl     a, b
        jnz     00001$
        lcall   00010$
        mov     a, r4
        anl     a, #0x7f
        movx    @dptr, a
        inc     dptr
        sjmp    00002$
00001$:
        mov     dptr, #(_scr + 0x0010)
        mov     a, r5
        movx    @dptr, a
        inc     dptr
        mov     a, b
        movx    @dptr, a
        lcall   00010$
        mov     a, r4
        orl     a, #0x80
        movx    @dptr, a
        inc     dptr
        mov     a, r5
        movx    @dptr, a
        inc     dptr
        mov     a, b
        movx    @dptr, a
        inc     dptr
00002$:
        mov     r0, dpl
        mov     r1, dph
        mov     a, r4
        rl      a
        anl     a, #1
        rl      a
        inc     a
        add     a, r2
        mov     r2, a
        clr     a
        addc    a, r3
        mov     r3, a
        mov     r5, #4
00003$:
        mov     dpl, r2
        mov     dph, r3
        movx    a, @dptr
        inc     dptr
        mov     r2, dpl
        mov     r3, dph
        mov     dpl, r0
        mov     dph, r1
        movx    @dptr, a
        inc     dptr
        mov     r0, dpl
        mov     r1, dph
        djnz    r5, 00003$
        mov     dptr, #(_scr + 0x0002)
        mov     a, r0
        movx    @dptr, a
        inc     dptr
        mov     a, r1
        movx    @dptr, a
        ret
00010$:
        mov     dptr, #(_scr + 0x0002)
        movx    a, @dptr
        mov     r0, a
        inc     dptr
        movx    a, @dptr
        mov     dph, a
        mov     dpl, r0
        ret
    __endasm;
}

/* Walk track o_t of part o_p, writing from w_at on the events o_sel picks (0 all, 1-6 an
 * ev_kind() class, 7 + n the events on MIDI channel n + 1) when o_keep, at their time +
 * o_dt, else the others. Returns the track end, 0 when memory is full.
 * o_sel O_RESIZE picks the events at or past tick o_at after the move; a note kept that
 * would sound past o_at ends there.
 * Naked: r6:r7 is the event. o_dt is scr+0x0A, o_at scr+0x0C, o_sel scr+0x2D. */
static __bit o_keep;
static __xdata xptr walk_e;
#define O_RESIZE 0xFF

static xptr track_walk(void) __naked
{
    __asm
        mov     dptr, #(_scr + 0x0010)
        mov     a, #0xff
        movx    @dptr, a
        inc     dptr
        movx    @dptr, a
        lcall   _o_track
00001$:
        mov     r6, dpl
        mov     r7, dph
        lcall   _ev_end
        mov     a, dpl
        mov     dpl, r6
        mov     dph, r7
        jz      00002$
        ret
00002$:
        movx    a, @dptr
        jnb     acc.7, 00003$
        inc     dptr
        lcall   _rd16
        mov     r4, dpl
        mov     r5, dph
        mov     dptr, #(_scr + 0x000a)
        movx    a, @dptr
        add     a, r4
        mov     r4, a
        inc     dptr
        movx    a, @dptr
        addc    a, r5
        mov     dptr, #(_scr + 0x000f)
        movx    @dptr, a
        mov     dptr, #(_scr + 0x000e)
        mov     a, r4
        movx    @dptr, a
00003$:
        mov     dptr, #(_scr + 0x002d)
        movx    a, @dptr
        jz      00020$
        cjne    a, #O_RESIZE, 00010$
        mov     dptr, #(_scr + 0x000e)
        movx    a, @dptr
        mov     r4, a
        inc     dptr
        movx    a, @dptr
        mov     r5, a
        mov     dptr, #(_scr + 0x000c)
        movx    a, @dptr
        clr     c
        subb    a, r4
        mov     r4, a
        inc     dptr
        movx    a, @dptr
        subb    a, r5
        mov     r5, a
        jc      00020$
        orl     a, r4
        jz      00020$
        mov     dpl, r6
        mov     dph, r7
        lcall   _ev_body
        movx    a, @dptr
        jb      acc.7, 00021$
        inc     dptr
        inc     dptr
        mov     r0, dpl
        mov     r1, dph
        movx    a, @dptr
        mov     r3, a
        inc     dptr
        movx    a, @dptr
        mov     r2, a
        clr     c
        mov     a, r4
        subb    a, r2
        mov     a, r5
        subb    a, r3
        jnc     00021$
        mov     a, r4
        movx    @dptr, a
        mov     dpl, r0
        mov     dph, r1
        mov     a, r5
        movx    @dptr, a
        sjmp    00021$
00010$:
        mov     dptr, #_walk_e
        mov     a, r6
        movx    @dptr, a
        inc     dptr
        mov     a, r7
        movx    @dptr, a
        mov     dpl, r6
        mov     dph, r7
        lcall   _ev_kind
        mov     r2, dpl
        mov     dptr, #_walk_e
        movx    a, @dptr
        mov     r6, a
        inc     dptr
        movx    a, @dptr
        mov     r7, a
        mov     dptr, #(_scr + 0x002d)
        movx    a, @dptr
        add     a, #0xf9
        jc      00011$
        add     a, #7
        xrl     a, r2
        jz      00020$
        sjmp    00021$
00011$:
        mov     r3, a
        cjne    r2, #6, 00012$
        sjmp    00021$
00012$:
        mov     dpl, r6
        mov     dph, r7
        lcall   _ev_body
        inc     dptr
        movx    a, @dptr
        anl     a, #0x0f
        xrl     a, r3
        jnz     00021$
00020$:
        jnb     _o_keep, 00030$
        mov     dptr, #(_scr + 0x0002)
        movx    a, @dptr
        mov     r4, a
        inc     dptr
        movx    a, @dptr
        mov     r5, a
        clr     c
        mov     a, #0xf9
        subb    a, r4
        mov     a, #0xfe
        subb    a, r5
        jnc     00022$
        mov     dptr, #0
        ret
00021$:
        jb      _o_keep, 00030$
00022$:
        mov     dpl, r6
        mov     dph, r7
        lcall   _ev_put
00030$:
        mov     dpl, r6
        mov     dph, r7
        lcall   _ev_next
        ljmp    00001$
    __endasm;
}

/* Stage the selected events of track o_t of part o_p at o_at, their times moved by o_dt.
 * Returns the byte count, 0xFFFF if memory is full. */
static uint16_t stage_track(void)
{
    o_keep = 1;
    w_at = (xptr)o_at;
    if (!track_walk())
        return 0xFFFF;
    return (uint16_t)w_at - o_at;
}

/* Move the n bytes staged at the heap end to the end of track o_t of part o_p. The
 * caller fixes the record table once for the whole operation. */
/* Scratch of the copy path, kept out of registers so deep calls push nothing. */
static __xdata __at(SCRATCH_RAM) struct {
    uint16_t slen, dlen, total, c, n, at;
    xptr p;
    uint8_t t, hi;
} cs;

static void track_append(uint16_t len)
{
#define n cs.n
#define at cs.at
    if (!(n = len))
        return;
    at = (uint16_t)track_end(o_track());
    mem_rev(at, cfg.heap_end + n);
    mem_rev(at, at + n);
    mem_rev(at + n, cfg.heap_end + n);
    cfg.heap_end += n;
    slots_shift(o_p, o_t, (int16_t)n);
#undef n
#undef at
}

/* Remove the selected events of track o_t of part o_p. */
static void track_erase(void)
{
    __xdata uint16_t d;
    o_keep = 0;
    w_at = o_track();
    d = (uint16_t)(track_walk() - w_at);
    if (d) {
        heap_delete((uint16_t)w_at, d);
        slots_shift(o_p, o_t, -(int16_t)d);
    }
}

/* Replace track o_t of part o_p with the events staged from the heap end up to w_at. */
static void track_replace(void)
{
    cs.total = (uint16_t)w_at - cfg.heap_end;
    o_at = cfg.heap_end;
    o_sel = 0;
    o_dt = 0;
    track_erase();
    mem_move(cfg.heap_end, o_at, cs.total);
    cs.c = (uint16_t)o_p + rd16(o_p + P_SIZE);
    track_append(cs.total);
    tab_fix(cs.c, (int16_t)cs.total);
}

/* LENGTH with RECORD on the current part: `len` is the new length in beats, bit 15 set
 * when the part top moves. Events at or past a new end, or before a new start, are
 * deleted; a note that would sound past the end is cut there. Other durations stay. */
static uint16_t end_ticks(void)
{
    return rd16(track_end(o_track()) + 1);
}

void part_resize(uint16_t len)
{
    o_at = len;
    o_p = cur_part();
    o_t = 0;
    o_dt = end_ticks();
    part_set_len(o_p, len & 0x7FFF);
    o_dt = o_at & 0x8000 ? end_ticks() - o_dt : 0;
    o_at = end_ticks();
    o_sel = O_RESIZE;
    for (o_t = 0; o_t < 8; ++o_t)
        track_erase();
}

/* MERGE: track op.src_t of part op.src into track op.dst_t, in time order (the first
 * track's events first at equal times). Each track's MIDI channel assignment is written
 * into its events and the merged track's becomes UNCHANGED. Returns 0 when memory is full.
 * Naked: only ui.c calls it. r0:r1 and r2:r3 are the two events, r4 and r5 their channels.
 * Times sit at scr+0x12 and scr+0x14; ev_put's scratch is stashed over the quantize slots. */
uint8_t part_merge(void) __naked
{
    __asm
        mov     dptr, #_op
        movx    a, @dptr
        mov     dpl, a
        lcall   _use_part
        mov     dptr, #_scr
        movx    a, @dptr
        mov     r6, a
        inc     dptr
        movx    a, @dptr
        mov     r7, a
        mov     dptr, #(_op + 0x0001)
        movx    a, @dptr
        mov     _track_PARM_2, a
        mov     dpl, r6
        mov     dph, r7
        lcall   _track
        mov     r0, dpl
        mov     r1, dph
        mov     dptr, #_scr
        movx    a, @dptr
        mov     r6, a
        inc     dptr
        movx    a, @dptr
        mov     r7, a
        mov     dptr, #(_op + 0x0003)
        movx    a, @dptr
        mov     _track_PARM_2, a
        mov     dpl, r6
        mov     dph, r7
        lcall   _track
        mov     r2, dpl
        mov     r3, dph
        mov     dptr, #(_op + 0x0001)
        movx    a, @dptr
        mov     b, a
        mov     a, #0x1b
        clr     c
        subb    a, b
        add     a, r6
        mov     dpl, a
        clr     a
        addc    a, r7
        mov     dph, a
        movx    a, @dptr
        mov     r4, a
        mov     dptr, #(_op + 0x0003)
        movx    a, @dptr
        mov     b, a
        mov     a, #0x1b
        clr     c
        subb    a, b
        add     a, r6
        mov     dpl, a
        clr     a
        addc    a, r7
        mov     dph, a
        movx    a, @dptr
        mov     r5, a
        mov     dptr, #0x04e7
        movx    a, @dptr
        mov     r6, a
        inc     dptr
        movx    a, @dptr
        mov     r7, a
        mov     dptr, #(_scr + 0x0002)
        mov     a, r6
        movx    @dptr, a
        inc     dptr
        mov     a, r7
        movx    @dptr, a
        mov     dptr, #(_scr + 0x0010)
        mov     a, #0xff
        movx    @dptr, a
        inc     dptr
        movx    @dptr, a
        mov     dptr, #(_scr + 0x0012)
        clr     a
        movx    @dptr, a
        inc     dptr
        movx    @dptr, a
        inc     dptr
        movx    @dptr, a
        inc     dptr
        movx    @dptr, a
00001$:
        mov     dpl, r0
        mov     dph, r1
        movx    a, @dptr
        jnb     acc.7, 00002$
        inc     dptr
        lcall   _rd16
        mov     r6, dpl
        mov     r7, dph
        mov     dptr, #(_scr + 0x0012)
        mov     a, r6
        movx    @dptr, a
        inc     dptr
        mov     a, r7
        movx    @dptr, a
00002$:
        mov     dpl, r2
        mov     dph, r3
        movx    a, @dptr
        jnb     acc.7, 00003$
        inc     dptr
        lcall   _rd16
        mov     r6, dpl
        mov     r7, dph
        mov     dptr, #(_scr + 0x0014)
        mov     a, r6
        movx    @dptr, a
        inc     dptr
        mov     a, r7
        movx    @dptr, a
00003$:
        mov     dpl, r0
        mov     dph, r1
        lcall   _ev_end
        mov     a, dpl
        jnz     00004$
        mov     dpl, r2
        mov     dph, r3
        lcall   _ev_end
        mov     a, dpl
        jnz     00005$
        mov     dptr, #(_scr + 0x0012)
        movx    a, @dptr
        mov     r6, a
        inc     dptr
        movx    a, @dptr
        mov     r7, a
        mov     dptr, #(_scr + 0x0014)
        movx    a, @dptr
        clr     c
        subb    a, r6
        inc     dptr
        movx    a, @dptr
        subb    a, r7
        jc      00004$
00005$:
        mov     dptr, #(_scr + 0x0012)
        lcall   00020$
        mov     a, r0
        mov     r6, a
        mov     a, r1
        mov     r7, a
        mov     dpl, r0
        mov     dph, r1
        lcall   _ev_next
        mov     r0, dpl
        mov     r1, dph
        mov     a, r4
        sjmp    00006$
00004$:
        mov     dpl, r2
        mov     dph, r3
        lcall   _ev_end
        mov     a, dpl
        jz      00012$
        ljmp    00007$
00012$:
        mov     dptr, #(_scr + 0x0014)
        lcall   00020$
        mov     a, r2
        mov     r6, a
        mov     a, r3
        mov     r7, a
        mov     dpl, r2
        mov     dph, r3
        lcall   _ev_next
        mov     r2, dpl
        mov     r3, dph
        mov     a, r5
00006$:
        mov     dptr, #(_scr + 0x001a)
        movx    @dptr, a
        mov     dptr, #(_scr + 0x0016)
        mov     a, r6
        movx    @dptr, a
        inc     dptr
        mov     a, r7
        movx    @dptr, a
        mov     dptr, #(_scr + 0x0002)
        movx    a, @dptr
        mov     r6, a
        inc     dptr
        movx    a, @dptr
        mov     r7, a
        clr     c
        mov     a, #0xf9
        subb    a, r6
        mov     a, #0xfe
        subb    a, r7
        jnc     00008$
        mov     dpl, #0
        ret
00008$:
        mov     dptr, #(_scr + 0x0004)
        mov     a, r0
        movx    @dptr, a
        inc     dptr
        mov     a, r1
        movx    @dptr, a
        inc     dptr
        mov     a, r2
        movx    @dptr, a
        inc     dptr
        mov     a, r3
        movx    @dptr, a
        inc     dptr
        mov     a, r4
        movx    @dptr, a
        inc     dptr
        mov     a, r5
        movx    @dptr, a
        mov     dptr, #(_scr + 0x0016)
        movx    a, @dptr
        mov     r6, a
        inc     dptr
        movx    a, @dptr
        mov     dph, a
        mov     dpl, r6
        lcall   _ev_put
        mov     dptr, #(_scr + 0x0004)
        movx    a, @dptr
        mov     r0, a
        inc     dptr
        movx    a, @dptr
        mov     r1, a
        inc     dptr
        movx    a, @dptr
        mov     r2, a
        inc     dptr
        movx    a, @dptr
        mov     r3, a
        inc     dptr
        movx    a, @dptr
        mov     r4, a
        inc     dptr
        movx    a, @dptr
        mov     r5, a
        mov     dptr, #(_scr + 0x001a)
        movx    a, @dptr
        jnz     00013$
        ljmp    00001$
00013$:
        mov     r6, a
        mov     dptr, #(_scr + 0x0016)
        movx    a, @dptr
        mov     r2, a
        inc     dptr
        movx    a, @dptr
        mov     r3, a
        mov     dpl, r2
        mov     dph, r3
        movx    a, @dptr
        orl     a, #0x80
        cjne    a, #0xfd, 00009$
        mov     dpl, r2
        mov     dph, r3
        lcall   _ev_body
        movx    a, @dptr
        jb      acc.7, 00010$
00009$:
        mov     dptr, #(_scr + 0x0002)
        movx    a, @dptr
        mov     r2, a
        inc     dptr
        movx    a, @dptr
        mov     r3, a
        mov     a, r2
        add     a, #0xfd
        mov     r2, a
        mov     a, r3
        addc    a, #0xff
        mov     r3, a
        mov     dpl, r2
        mov     dph, r3
        movx    a, @dptr
        anl     a, #0xf0
        mov     b, a
        mov     a, r6
        dec     a
        anl     a, #0x0f
        orl     a, b
        movx    @dptr, a
00010$:
        mov     dptr, #(_scr + 0x0006)
        movx    a, @dptr
        mov     r2, a
        inc     dptr
        movx    a, @dptr
        mov     r3, a
        ljmp    00001$
00007$:
        mov     dptr, #(_op + 0x0003)
        movx    a, @dptr
        mov     dptr, #(_scr + 0x002c)
        movx    @dptr, a
        lcall   _track_replace
        mov     dptr, #(_op + 0x0003)
        movx    a, @dptr
        mov     b, a
        mov     a, #0x1b
        clr     c
        subb    a, b
        mov     r6, a
        mov     dptr, #_scr
        movx    a, @dptr
        mov     r4, a
        inc     dptr
        movx    a, @dptr
        mov     r5, a
        mov     a, r4
        add     a, r6
        mov     dpl, a
        mov     a, r5
        addc    a, #0
        mov     dph, a
        clr     a
        movx    @dptr, a
        mov     dpl, #1
        ret
00020$:
        movx    a, @dptr
        mov     r6, a
        inc     dptr
        movx    a, @dptr
        mov     r7, a
        mov     dptr, #(_scr + 0x000e)
        mov     a, r6
        movx    @dptr, a
        inc     dptr
        mov     a, r7
        movx    @dptr, a
        ret
    __endasm;
}

/* ---- part EDIT ----
 * The editor's event is ed_off bytes into track ed_trk of the current part. A system
 * exclusive message, its first cell and the 7D cells continuing it, is one event. The
 * walks use the quantize scratch. */
__bit ed_on;
__xdata uint8_t ed_trk;
__xdata uint16_t ed_off;

xptr ed_next(xptr e)
{
    uint8_t sx = ev_sysex(e), end;
    for (;;) {
        end = (e[0] & 0x80) ? e[4] | e[6] : e[2] | e[4];
        e = ev_next(e);
        if (!sx || (end & 0x80) || e[0] != 0x7D)
            return e;
    }
}

xptr ed_base(void)
{
    return track(cur_part(), ed_trk);
}

/* The editor's event, moved to the event boundary at or after ed_off; ew.time is its
 * time. With `back`, the editor first moves to the event before (stays on the first). */
xptr ed_event(uint8_t back)
{
    ++back;
    while (back--) {
        q_p = qo_p = qn_p = ed_base();
        for (;;) {
            if (qn_p[0] & 0x80)
                ew.time = rd16(qn_p + 1);
            if (end_at((__xdata uint8_t *)&qn_p) || (uint16_t)(qn_p - q_p) >= ed_off)
                break;
            qn_p = ed_next(qo_p = qn_p);
        }
        ed_off = (uint16_t)((back ? qo_p : qn_p) - q_p);
    }
    return qn_p;
}

/* Stage the cells from qn_p up to e at time w_t. */
static uint8_t ed_put(xptr e)
{
    for (; qn_p != e; qn_p = ev_next(qn_p)) {
        if ((uint16_t)w_at > HEAP_TOP - EV)
            return 0;
        ev_put(qn_p);
    }
    return 1;
}

/* Rebuild the edit track with the event at src (0 = none) placed at time ew.time before
 * the events at that time or later, and without the editor's event unless src is the
 * insert buffer; the editor moves to the placed event. Returns 0 when memory is full. */
uint8_t ed_rebuild(xptr src)
{
    q_ns = src == scr.ed.ev ? 0xFFFF : ed_off;
    o_p = cur_part();
    qo_p = q_p = track(o_p, ed_trk);
    w_at = (xptr)cfg.heap_end;
    w_last = 0xFFFF;
    for (;;) {
        if (qo_p[0] & 0x80)
            q_t = rd16(qo_p + 1);
        if (src && (end_at((__xdata uint8_t *)&qo_p) || q_t >= ew.time)) {
            ed_off = (uint16_t)w_at - cfg.heap_end;
            w_t = ew.time;
            qn_p = src;
            if (!ed_put(ed_next(src)))
                return 0;
            src = 0;
        }
        if (end_at((__xdata uint8_t *)&qo_p))
            break;
        qn_p = qo_p;
        qo_p = ed_next(qo_p);
        w_t = q_t;
        if ((uint16_t)(qn_p - q_p) != q_ns && !ed_put(qo_p))
            return 0;
    }
    o_t = ed_trk;
    track_replace();
    return 1;
}

/* Inserted events: C3 velocity 64 one beat, pitch bend centre, aftertouch, controller 1,
 * program change, all on channel 1. */
static const uint8_t __code ins_ev[5][EV] = {
    {0xBC, 0, 0, 0x40, 0, 0, TICKS_PER_BEAT}, {0xFC, 0, 0, 0x80, 0, 0, 0x40},
    {0xFB, 0, 0, 0x80, 0, 0, 0}, {0x81, 0, 0, 0x80, 0, 0, 0}, {0xFA, 0, 0, 0x80, 0, 0, 0},
};

uint8_t ed_insert(uint8_t kind)
{
    uint8_t i;
    for (i = 0; i < EV; ++i)
        scr.ed.ev[i] = ins_ev[kind][i];
    return ed_rebuild(scr.ed.ev);
}

#ifdef OPENMMT8_EDITX
/* The editor to the first event not before tick t. */
void ed_seek(uint16_t t)
{
    xptr b = ed_base();
    ed_off = (uint16_t)(ev_seek(b, t) - b);
}

uint8_t ed_grid(void)
{
    return click_ticks[cfg.quant_val];
}

/* Undo: the event as it was (long form) in scr.ed.u, its track, and where it is now. */
__bit ed_undo_ok;
static __xdata uint8_t u_trk;
static __xdata uint16_t u_off;

/* The editor's event ew.e at ew.time into the insert buffer, in long form. */
static void ed_copy(void)
{
    scr.ed.ev[0] = ew.e[0] | 0x80;
    wr16(scr.ed.ev + 1, ew.time);
    mem_move((uint16_t)(scr.ed.ev + 3), (uint16_t)ev_body(ew.e), 4);
}

#define u_keep() mem_move((uint16_t)scr.ed.u, (uint16_t)scr.ed.ev, EV)

void ed_snap(void)
{
    ed_copy();
    u_keep();
    u_trk = ed_trk;
    ed_undo_ok = !ev_sysex(ew.e);
}

void ed_mark(void)
{
    u_off = ed_off;
}

/* Put the kept event back; the replaced one is kept, so undo again redoes. */
uint8_t ed_undo(void)
{
    ed_trk = u_trk;
    ed_off = u_off;
    ew.e = ed_event(0);
    ed_copy();
    ew.time = rd16(scr.ed.u + 1);
    if (!ed_rebuild(scr.ed.u))
        return 0;
    u_keep();
    u_off = ed_off;
    return 1;
}
#endif

/* An emptied part: no length, every track END one beat before the start. */
static void part_blank(void)
{
    wr16(o_p + P_LEN, 0);
    for (o_t = 0; o_t < 8; ++o_t)
        wr16(o_track() + 1, (uint16_t)-TICKS_PER_BEAT);
}

/* ERASE in part mode: tracks in op.mask (0 = all) of part op.src, selected data only.
 * Erasing all data of the whole part leaves the empty part record. */
void part_erase(void)
{
    use_part(op.src);
    if (!o_p)
        return;
    o_sel = op.sel;
    o_dt = 0;
    for (o_t = 0; o_t < 8; ++o_t)
        if (!op.mask || ((op.mask >> o_t) & 1))
            track_erase();
    if (!op.mask && !op.sel)
        part_blank();
}

/* TRANSPOSE: notes of the tracks in op.mask of part op.src move by (int8_t)op.sel
 * semitones, folding back by octaves into 0..127.
 * Naked: only ui.c calls it, so that caller saves registers. ev_end/ev_next/ev_body
 * touch none of r0-r7. r2:r3 is the event, r7 the track, r4 the original status. */
void part_transpose(void) __naked
{
    __asm
        mov     dptr, #_op
        movx    a, @dptr
        mov     dpl, a
        lcall   _use_part
        mov     dptr, #_scr
        movx    a, @dptr
        mov     r2, a
        inc     dptr
        movx    a, @dptr
        orl     a, r2
        jnz     00001$
        ret
00001$:
        mov     r7, #0
00002$:
        mov     dptr, #(_scr + 0x002c)
        mov     a, r7
        movx    @dptr, a
        mov     a, r7
        mov     dptr, #_bit_tab
        movc    a, @a+dptr
        mov     r6, a
        mov     dptr, #(_op + 0x0005)
        movx    a, @dptr
        anl     a, r6
        jz      00008$
        lcall   _o_track
        mov     r2, dpl
        mov     r3, dph
        mov     dptr, #(_scr + 0x002c)
        movx    a, @dptr
        mov     r7, a
00003$:
        mov     dpl, r2
        mov     dph, r3
        lcall   _ev_end
        mov     a, dpl
        jnz     00008$
        mov     dpl, r2
        mov     dph, r3
        lcall   _ev_body
        movx    a, @dptr
        jb      acc.7, 00007$
        mov     dpl, r2
        mov     dph, r3
        movx    a, @dptr
        mov     r4, a
        anl     a, #0x7F
        mov     r0, a
        mov     r1, #0
        mov     dptr, #(_op + 0x0004)
        movx    a, @dptr
        mov     r6, a
        mov     c, acc.7
        subb    a, acc
        mov     r5, a
        mov     a, r6
        add     a, r0
        mov     r0, a
        mov     a, r5
        addc    a, r1
        mov     r1, a
00004$:
        mov     a, r1
        jb      acc.7, 00005$
        jnz     00006$
        mov     a, r0
        jnb     acc.7, 00009$
00006$:
        mov     a, r0
        add     a, #0xF4
        mov     r0, a
        mov     a, r1
        addc    a, #0xFF
        mov     r1, a
        sjmp    00004$
00005$:
        mov     a, r0
        add     a, #12
        mov     r0, a
        mov     a, r1
        addc    a, #0
        mov     r1, a
        sjmp    00004$
00009$:
        mov     a, r4
        anl     a, #0x80
        orl     a, r0
        mov     dpl, r2
        mov     dph, r3
        movx    @dptr, a
00007$:
        mov     dpl, r2
        mov     dph, r3
        lcall   _ev_next
        mov     r2, dpl
        mov     r3, dph
        sjmp    00003$
00008$:
        inc     r7
        cjne    r7, #0x08, 00010$
        sjmp    00011$
00010$:
        ljmp    00002$
00011$:
        mov     dptr, #(_scr + 0x002c)
        mov     a, r7
        movx    @dptr, a
        ret
    __endasm;
}

/* QUANTIZE: note events of the tracks in op.mask of part op.src move to the nearest step
 * of grid op.sel (index into the 1/2 .. 1/64 choices) in mode op.dst (start, start & end,
 * end, keep duration). Notes landing on the part end are dropped; moved notes go before
 * other events of the same time. Returns 0 when memory is full, 2 when note starts of a
 * non-empty track were processed, else 1. */
static __bit q_note;

static void q_seek(void)
{
    for (; !end_at((__xdata uint8_t *)&q_p); q_p = ev_next(q_p)) {
        if (q_p[0] & 0x80)
            q_t = rd16(q_p + 1);
        if (!(ev_body(q_p)[0] & 0x80) == q_note)
            return;
    }
}

static uint16_t q_round(uint16_t t)
{
    uint8_t q = q_grid[op.sel];
    return (t + (q >> 1)) / q * q;
}

static void q_next_note(void)
{
    __xdata xptr b;
    __xdata uint16_t end;
    q_note = 1;
    q_seek();
    qn_p = q_p;
    qn_t = q_t;
    if (end_at((__xdata uint8_t *)&q_p))
        return;
    b = ev_body(q_p);
    q_nd = (b[2] << 8) | b[3];
    end = qn_t + q_nd;
    q_ns = op.dst == 2 ? qn_t : q_round(qn_t);
    if (op.dst == 1 || op.dst == 2)
        q_nd = q_round(end) - q_ns;
    else if (op.dst == 0)
        q_nd = end - q_ns;
    if ((int16_t)q_nd <= 0)
        q_nd = 1;
}

static void q_start(void)
{
    q_p = o_track();
    q_t = 0;
}

static void q_next_other(void)
{
    q_note = 0;
    q_seek();
    qo_p = q_p;
    qo_t = q_t;
}

uint8_t part_quantize(void)
{
    __xdata uint8_t r = 1;
    use_part(op.src);
    if (!o_p)
        return 1;
    for (o_t = 0; o_t < 8; ++o_t) {
        if (!((op.mask >> o_t) & 1) || ev_end(o_track()))
            continue;
        if (op.dst != 2)
            r = 2;
        q_start();
        q_next_note();
        q_start();
        q_next_other();
        w_at = (xptr)cfg.heap_end;
        w_last = 0xFFFF;
        for (;;) {
            if ((uint16_t)w_at > HEAP_TOP - EV)
                return 0;
            if (!end_at((__xdata uint8_t *)&qn_p) && (end_at((__xdata uint8_t *)&qo_p) || q_ns <= qo_t)) {
                if (q_ns < part_ticks || op.dst == 2) {
                    w_t = q_ns;
                    ev_put(qn_p);
                    w_at[-2] = (uint8_t)(q_nd >> 8);
                    w_at[-1] = (uint8_t)q_nd;
                }
                q_p = ev_next(qn_p);
                q_t = qn_t;
                q_next_note();
            } else if (!end_at((__xdata uint8_t *)&qo_p)) {
                w_t = qo_t;
                ev_put(qo_p);
                q_p = ev_next(qo_p);
                q_t = qo_t;
                q_next_other();
            } else {
                break;
            }
        }
        track_replace();
    }
    return r;
}

/* COPY in part mode. op.src_t = K_NONE copies the whole part op.src, appending it to
 * part op.dst (an empty destination becomes a copy); otherwise track op.src_t replaces
 * track op.dst_t of part op.dst. Returns 0 when memory is full. */
uint8_t part_copy(void)
{
#define n scr.op.n
#define slen cs.slen
#define dlen cs.dlen
#define total cs.total
#define c cs.c
#define hi cs.hi
    total = 0;
    hi = 8;

    use_part(op.src);
    if (!o_p)
        return 1;
    if (!(slen = part_len(o_p))) {
        use_part(op.dst);
        if (o_p && !part_len(o_p))
            part_blank();
        return 1;
    }
    if (!part_rec(op.dst))
        part_create(op.dst);
    use_part(op.src);
    dlen = part_len(part_rec(op.dst));
    o_dt = 0;
    cs.t = 0;
    if (op.src_t != K_NONE) {
        cs.t = op.src_t;
        hi = cs.t + 1;
    } else if (dlen) {
        o_dt = dlen * TICKS_PER_BEAT;
    }
    o_sel = op.sel;
    o_at = cfg.heap_end;
    for (; cs.t < hi; ++cs.t) {
        o_t = cs.t;
        c = stage_track();
        if (c == 0xFFFF)
            return 0;
        n[cs.t] = c;
        o_at += c;
        total += c;
    }
    use_part(op.dst);
    if (op.src_t == K_NONE) {
        c = (uint16_t)o_p + rd16(o_p + P_SIZE);
        for (cs.t = 0; cs.t < 8; ++cs.t) {
            o_t = cs.t;
            track_append(n[cs.t]);
        }
        tab_fix(c, (int16_t)total);
        part_set_len(o_p, dlen + slen);
        return 1;
    }
    o_t = op.dst_t;
    track_replace();
    if (!dlen)
        part_set_len(o_p, slen);
    return 1;
}
#undef n
#undef slen
#undef dlen
#undef total
#undef c
#undef hi

static void sx_close(void);

/* Load song step `song_step`; returns 0 past the last step. */
static uint8_t step_start(void)
{
    xptr s = song_rec(cfg.song);
    if (song_step >= song_steps(s))
        return 0;
    s += S_STEPS + 2u * song_step;
    mute = (uint8_t)~s[1];
    part_start(s[0]);
    return 1;
}

static void notes_off(void)
{
    uint8_t i;
    sx_close();
    for (i = 0; i < on_n; ++i) {
        midi_tx(on_st[i]);
        midi_tx(on_note[i]);
        midi_tx(0x40);
    }
    on_n = 0;
}

static void controllers_off(void)
{
    uint8_t ch;
    for (ch = 0; ch < 16; ++ch) {
        midi_tx(0xB0 | ch);
        midi_tx(0x40);
        midi_tx(0x00);
    }
}

void seq_play(void)
{
    uint8_t from_stop = tp_state == TP_STOP;
    if (!from_stop) {
        if (tp_recording) {
            rec_commit();
            rec_armed = 0;
        }
        notes_off();
        rt_tx(0xFC);
    }
    rec_screen = 0;
    stop_on_n = 0;
    if (cfg.mode_song) {
        edit_on = 0;
        song_step = 0;
        if (!step_start()) {
            rt_tx(0xFA);
            rt_tx(0xFC);
            return;
        }
    } else {
        part_start(cfg.part);
    }
    tp_beat = 0;
    rt_tx(0xFA);
    if (from_stop) {
        if (rec_armed)
            keys_act_wait((uint16_t)(PLAY_START_CYCLES - REC_START_EARLY + PLAY_REC_KEY_LAG));
        else
            keys_act_wait((uint16_t)(PLAY_START_CYCLES + PLAY_STOP_KEY_LAG));
    }
    g_ticks = 0;
    TF0 = 1; /* restart the tick period: the forced tick is tick 0 */
    clk_n = 1;
    if (rec_armed && cfg.countdown) {
        tp_state = TP_COUNTIN;
        count_left = cfg.countdown;
        count_ticks = 0xFF;
        if (count_left == 1)
            rec_open();
    } else {
        tp_state = TP_PLAY;
        if (rec_armed) {
            part_start(cfg.part);
            rec_begin();
        }
    }
    ui_dirty = 1;
}

/* RECORD pressed while a part with a length plays: the take starts here on the record
 * track, with no count-in, and the screen stays on the playing page. */
void seq_punch_in(void)
{
    if (!part_ticks || rec_track == K_NONE)
        return;
    rec_at = trk_at[rec_track];
    rec_punch = 1;
    rec_armed = 1;
    rec_begin();
    rec_screen = 0;
}

/* RECORD pressed while recording: punches out once the part has a length. */
void seq_punch_out(void)
{
    rec_armed = 0;
    rec_off = 1;
    if (!part_ticks) {
        rec_close_held();
        return;
    }
    rec_commit();
    rec_off = 0;
}

void seq_stop(void)
{
    uint8_t i;
    __xdata uint8_t was_rec;
    xptr p;
    if (tp_state == TP_STOP)
        return;
#ifdef OPENMMT8_TAPE
    P3_4 = 1;
#endif
    clock_apply();
    rec_screen = 0;
    rec_armed = 0;
    next_part = K_NONE;
    if (tp_state == TP_COUNTIN) {
        tp_state = TP_STOP;
        if (ci_cap && rb_end != rb_start) {
            tp_recording = 1;
            rec_commit();
        }
        ci_cap = 0;
        p = cur_part();
        if (p && !part_len(p))
            part_set_len(p, 1);
        rt_tx(0xFC);
        ui_dirty = 1;
        return;
    }
    was_rec = tp_recording;
    if (was_rec)
        rec_off = 2u;
    rec_commit();
    rec_off = 0;
    tp_state = TP_STOP;
    if (ed_on)
        ed_off = (uint16_t)(trk_at[ed_trk] - track(part_rec(play_part), ed_trk));
    stop_tick = tp_pos;
    stop_spp = was_rec ? 0 : rec_now() / 24u;
    for (i = 0; i < on_n; ++i)
        on_off[i] -= tp_pos;
    stop_on_n = on_n;
    notes_off();
    rt_tx(0xFC);
    controllers_off();
    ui_dirty = 1;
    if (cfg.mode_song)
        edit_on = 0;
    if (was_rec) {
        if (cfg.rec_click && tp_beat == 2u && cfg.click_val == 2u)
            click();
        keys_rec_stopped();
    }
}

/* Continue position from a located beat (1-based; 0 = before beat 1). */
void seq_locate(uint16_t beat)
{
    stop_spp = beat ? (beat - 1u) * 4u : 0;
    stop_on_n = 0;
}

/* STOP pressed while stopped: resume from the 16th note playback stopped in, sounding again
 * the notes that were held at STOP. */
static void continue_tx(void)
{
    if (cfg.clock_out && (!sync_cont || sync_spp)) {
        midi_tx(0xF2);
        midi_tx(stop_spp & 0x7F);
        midi_tx(stop_spp >> 7);
    }
    sync_cont = sync_spp = 0;
    rt_tx(0xFB);
}

void seq_continue(void)
{
    uint8_t t, i;
    if (cfg.mode_song) {
        if (!step_start()) {
            continue_tx();
            rt_tx(0xFC);
            return;
        }
    } else {
        part_start(cfg.part);
    }
    tp_pos = sync_cont && !sync_spp ? stop_tick : stop_spp * 24u;
    if (part_ticks && tp_pos >= part_ticks)
        tp_pos = stop_spp = 0;
    for (t = 0; t < 8; ++t) {
        trk_at[t] = ev_seek(trk_at[t], tp_pos);
    }
    for (i = 0; i < stop_on_n; ++i) {
        midi_tx(on_st[i] | 0x10);
        midi_tx(on_note[i]);
        midi_tx(on_vel[i]);
        on_off[i] += tp_pos;
    }
    on_n = stop_on_n;
    stop_on_n = 0;
    continue_tx();
    g_ticks = 0;
    TF0 = 1;
    tp_beat = tp_pos / TICKS_PER_BEAT;
    tp_state = TP_PLAY;
    ui_dirty = 1;
}

static __bit sx_open;

static void sx_close(void)
{
    if (sx_open) {
        sx_open = 0;
        midi_tx(0xF7);
    }
}

/* One system exclusive cell: F0 opens the message once; a 0x80 data slot ends it. */
static void emit_sysex(xptr e)
{
    uint8_t i;
    if (!sx_open) {
        sx_open = 1;
        midi_tx(0xF0);
    }
    if (e[0] & 0x80)
        e += 2;
    midi_tx(e[1] & 0x7F);
    for (i = 2; i < 5; i += 2) {
        if (e[i] & 0x80) {
            sx_close();
            return;
        }
        midi_tx(e[i]);
    }
}

static void emit(uint8_t t, xptr e)
{
    xptr b = ev_body(e);
    uint8_t ch = trk_chan[t] ? trk_chan[t] - 1 : (b[1] & 15);
    uint8_t c = e[0] | 0x80;
    if (!(b[0] & 0x80)) {
        sx_close();
        midi_tx(0x90 | ch);
        midi_tx(c & 0x7F);
        midi_tx(b[0]);
        if (on_n < MAX_ON) {
            on_st[on_n] = 0x80 | ch;
            on_note[on_n] = c & 0x7F;
            on_vel[on_n] = b[0];
            on_off[on_n] = tp_pos + ((b[2] << 8) | b[3]);
            ++on_n;
        }
        return;
    }
    if (c == 0xFD) {
        emit_sysex(e);
        return;
    }
    sx_close();
    switch (c) {
    case 0xFA:
        midi_tx(0xC0 | ch);
        midi_tx(b[0] & 0x7F);
        break;
    case 0xFB:
        midi_tx(0xD0 | ch);
        midi_tx(b[0] & 0x7F);
        break;
    case 0xFC:
        midi_tx(0xE0 | ch);
        midi_tx(b[2]);
        midi_tx(b[3]);
        break;
    default:
        midi_tx(0xB0 | ch);
        midi_tx(c & 0x7F);
        midi_tx(b[0] & 0x7F);
        break;
    }
}

/* Stop at the end of the part: CONTINUE then starts from the top. */
static void part_stop(void)
{
    seq_stop();
    ed_on = 0;
    stop_spp = 0;
    stop_on_n = 0;
}

/* End of the current part: queued part, loop, next song step or stop. */
static void part_end(void)
{
    notes_off();
    rec_screen = 0;
    if (tp_recording) {
        rec_commit();
        rec_armed = 0;
        if (cfg.loop) {
            part_start(play_part);
            return;
        }
        part_stop();
        return;
    }
    if (cfg.mode_song) {
        ++song_step;
        if (step_start())
            return;
        song_step = 0;
        if (cfg.loop) {
            song_step = 0;
            step_start();
            return;
        }
    } else if (next_part != K_NONE) {
        part_select(next_part);
        next_part = K_NONE;
        part_start(cfg.part);
        return;
    } else if (cfg.loop) {
        part_start(play_part);
        return;
    }
    part_stop();
}

/* A positive song shift plays the event sooner, and not before tick 0. */
static uint16_t apply_shift(uint8_t t, uint16_t w) __naked
{
    t; w;
    __asm
        push    ar5
        push    ar6
        push    ar7
        mov     r7, dpl
        mov     r5, _apply_shift_PARM_2
        mov     r6, (_apply_shift_PARM_2 + 1)
        mov     a, #0xdf
        add     a, r7
        mov     dpl, a
        clr     a
        addc    a, #0x04
        mov     dph, a
        movx    a, @dptr
        jnb     acc.7, 00001$
        cpl     a
        inc     a
        add     a, r5
        mov     dpl, a
        clr     a
        addc    a, r6
        mov     dph, a
        sjmp    00003$
00001$:
        mov     r7, a
        clr     c
        mov     a, r5
        subb    a, r7
        mov     dpl, a
        mov     a, r6
        subb    a, #0
        jnc     00002$
        mov     dpl, #0
        clr     a
00002$:
        mov     dph, a
00003$:
        pop     ar7
        pop     ar6
        pop     ar5
        ret
    __endasm;
}

void seq_tick(void)
{
    uint8_t t, i;
    xptr e;

    if (tp_state == TP_COUNTIN) {
        if (!++count_ticks) {
            ui_dirty = 1;
            if (cfg.rec_click)
                click();
        }
        if (count_ticks < click_ticks[cfg.click_val])
            return;
        count_ticks = 0;
        ui_dirty = 1;
        if (--count_left) {
            if (count_left == 1)
                rec_open();
            if (cfg.rec_click)
                click();
            return;
        }
        tp_state = TP_PLAY;
        part_start(cfg.part);
        rec_begin();
    }
    if (tp_state != TP_PLAY)
        return;
    if (tp_recording)
        rec_flush();
    if (part_ticks && tp_pos >= part_ticks) {
        part_end();
        if (tp_state != TP_PLAY)
            return;
    }
#ifdef OPENMMT8_TAPE
    /* Tape sync out is always active while playing: one high pulse per tick. */
    P3_4 = 1;
#endif
    if (!--clk_n) {
        if (!mod96(tp_pos))
            clk_per = click_ticks[cfg.click_val];
        clk_n = clk_per;
        if (rec_armed ? cfg.rec_click : cfg.play_click)
            click();
    }
    if (!(tp_pos & 3))
        rt_tx(0xF8);
    for (i = 0; i < on_n;) {
        if (on_off[i] == tp_pos) {
            midi_tx(on_st[i]);
            midi_tx(on_note[i]);
            midi_tx(0x40);
            --on_n;
            on_st[i] = on_st[on_n];
            on_note[i] = on_note[on_n];
            on_vel[i] = on_vel[on_n];
            on_off[i] = on_off[on_n];
        } else {
            ++i;
        }
    }
    for (t = 0; t < 8; ++t) {
        e = trk_at[t];
        while (!ev_end(e) && (!(e[0] & 0x80) ||
               (shift_live ? apply_shift(t, rd16(e + 1)) : rd16(e + 1)) <= tp_pos)) {
            if (ed_on && t == ed_trk && !(sx_open && ev_sysex(e))) {
                ed_off = (uint16_t)(e - track(part_rec(play_part), t));
                ui_dirty = 1;
            }
            if (!(mute & bit_tab[t]))
                emit(t, e);
            e = ev_next(e);
        }
        trk_at[t] = e;
    }
    if (!mod96(tp_pos)) {
        tp_beat = tp_pos / TICKS_PER_BEAT;
        if (key_held(K_CLICK) && tp_state == TP_PLAY && fn == K_NONE)
            ui_key_down(K_CLICK);
        else
            ui_dirty = 1;
    }
    ++tp_pos;
#ifdef OPENMMT8_TAPE
    P3_4 = 0;
#endif
}

static __xdata uint8_t rx_st, rx_d1, rx_d2, rx_n;

#ifdef OPENMMT8_EDITX
/* Step entry of the received note on at tick t, one quantize step long. */
uint8_t ed_step(uint16_t t)
{
    scr.ed.ev[0] = 0x80 | rx_d1;
    scr.ed.ev[3] = rx_d2;
    scr.ed.ev[4] = rx_st & 15;
    scr.ed.ev[5] = 0;
    scr.ed.ev[6] = ed_grid();
    ew.time = t;
    ed_undo_ok = 0;
    return ed_rebuild(scr.ed.ev);
}
#endif

/* Filter index per status nibble 8..E; poly pressure is never echoed. */
static const uint8_t __code echo_filt[7] = { 0, 0, 0xFF, 3, 4, 2, 1 };

static void echo(void)
{
    uint8_t st = rx_st, f = echo_filt[(st >> 4) - 8], c;
    if (!cfg.echo || f == 0xFF || !cfg.filt[f] || (cfg.filt_chan && (st & 15) != cfg.filt_chan - 1))
        return;
    if (rec_armed && (c = cur_part()[P_CHAN + 7 - rec_track]))
        st = (st & 0xF0) | (c - 1);
    midi_tx(st);
    midi_tx(rx_d1);
    if ((st & 0xE0) != 0xC0)
        midi_tx(rx_d2);
}

#ifdef OPENMMT8_TAPE
/* Tape sync takes the tick interrupt; every other source uses timer0. */
__bit tape_clk;

void clock_apply(void)
{
    tape_clk = cfg.clock_src == CLK_TAPE;
    if (tape_clk) {
        ET0 = 0;
        EX0 = 1;
        IE0 = 0;
        g_ticks = 0;
    } else {
        EX0 = 0;
        TF0 = 0;
        ET0 = 1;
    }
}
#else
void clock_apply(void)
{
    if (cfg.clock_src > CLK_INTERNAL)
        cfg.clock_src = CLK_INTERNAL;
    TF0 = 0;
    ET0 = 1;
}
#endif

/* MIDI START/CONTINUE/STOP act with MIDI & INTERNAL clock, or with INTERNAL ONLY and
 * AUTO START; with MIDI & INTERNAL, playback then runs on received clocks alone. */
static uint8_t rt_start(void)
{
    if (cfg.clock_src == CLK_MIDI) {
        ET0 = 0;
    } else if (cfg.clock_src != CLK_INTERNAL || !cfg.auto_start) {
        return 0;
    }
    g_ticks = 0;
    return 1;
}

static void realtime(uint8_t b)
{
    switch (b) {
    case 0xF8:
        if (cfg.clock_src == CLK_MIDI) {
            EA = 0;
            g_ticks += 4;
            EA = 1;
        }
        break;
    case 0xFA:
        if (tp_state == TP_STOP && rt_start())
            ui_key_down(K_PLAY);
        break;
    case 0xFB:
        if (tp_state == TP_STOP && rt_start()) {
            sync_cont = 1;
            seq_continue();
        }
        break;
    case 0xFC:
        if (tp_state != TP_STOP && (cfg.clock_src == CLK_MIDI ||
                                    (cfg.clock_src == CLK_INTERNAL && cfg.auto_start)))
            ui_key_down(K_STOP);
        break;
    }
}

/* ---- bulk sysex dump, OpenMMT-8 only ----
 * Header F0 7D 4F 4D 38 01, then 0x0400-0x05C9 and the heap through cfg.heap_end.
 * Bytes are packed MSB-first into 7-bit MIDI data. Key-scan and dump state
 * (0x05CA-0x05FF) are left out. An original unit ignores this header, and this
 * ROM ignores any other bulk header. */

static const uint8_t __code dump_hdr[] = {0xF0, 0x7D, 0x4F, 0x4D, 0x38, 0x01};
#define DUMP_GAP 0x05CAu
static const char __code t_sending[] = "sending data    over midi.......";
static const char __code t_receiving[] = "getting data    over midi.......";
/* Outside the relocatable xdata, which is full; a sysex load leaves this range alone. */
static __xdata __at(DUMP_RAM) struct {
    uint16_t a, v, end;
    uint8_t hdr_n, acc, n, cnt, is_new;
} dr;
#define dump_a dr.a
#define dump_v dr.v
#define dump_hdr_n dr.hdr_n
#define pk_acc dr.acc
#define pk_n dr.n
#define dump_n dr.cnt
#define DUMP_RX 0xFFu

/* pk_acc holds the pk_n bits not yet sent. dump_send pushes them out with zero bytes. */
static void dump_put(uint8_t v)
{
    uint16_t a = (uint16_t)(pk_acc << 8) | v;
    uint8_t n = pk_n + 8;
    while (n >= 8) {
        n -= 7;
        midi_tx((uint8_t)(a >> n) & 0x7F);
    }
    pk_acc = (uint8_t)a & (uint8_t)((1u << n) - 1u);
    pk_n = n;
}

/* Send parts, songs, and settings. The image is this ROM's own RAM, not another unit's. */
void dump_send(void)
{
    uint8_t i, mode = cfg.mode_song;
    uint16_t sent;
    cfg.mode_song = 1;
    ui_leds();
    cfg.mode_song = mode;
    part_release();
    lcd_text(0, t_sending, 32);
    lcd_flush();
    for (i = 0; i < sizeof dump_hdr; ++i)
        midi_tx(dump_hdr[i]);
    pk_acc = pk_n = 0;
    for (dump_a = PART_TABLE; dump_a < DUMP_GAP; ++dump_a)
        dump_put(*(xptr)dump_a);
    for (dump_a = HEAP_BASE; dump_a < cfg.heap_end; ++dump_a)
        dump_put(*(xptr)dump_a);
    /* The packer holds up to seven bits. Seven zero bytes push them out. The
     * receiver stops at cfg.heap_end, so those zeros are not stored. */
    for (i = 0; i < 7; ++i)
        dump_put(0);
    midi_tx(0xF7);
    midi_drain();
    sent = cycles_now();
    if (!cfg.mode_song) {
        mute = 0;
        if (!cur_part())
            part_create(cfg.part);
    }
    keys_rescan(sent);
}

/* Write one decoded byte. The first span is 0x0400-0x05C9; the heap follows. */
static void dump_store(uint8_t v)
{
    if (dump_a < DUMP_GAP || (dump_a >= HEAP_BASE && dump_a < cfg.heap_end && dump_a < HEAP_TOP))
        *(xptr)dump_a = v;
    if (dump_a + 1u == DUMP_GAP)
        dump_a = HEAP_BASE;
    else if (dump_a < HEAP_TOP)
        ++dump_a;
}

void seq_midi_in(void)
{
    uint8_t b;
    if (dump_hdr_n == DUMP_RX) {
        if (!midi_rx_ready())
            return;
        b = midi_rx();
        if (b & 0x80) {
            dump_hdr_n = 0;
            __asm__("ljmp 0");
        }
        dump_v = dump_v << 7 | b;
        dump_n += 7;
        if (dump_n >= 8) {
            dump_n -= 8;
            dump_store((uint8_t)(dump_v >> dump_n));
        }
        return;
    }
    while (!g_ticks && midi_rx_ready()) {
        b = midi_rx();
        if (b >= 0xF8) {
            realtime(b);
            continue;
        }
        if (b & 0x80) {
            if (sx_rec)
                rec_sysex_end();
            rx_st = b < 0xF0 || b == 0xF2 ? b : 0;
            rx_n = 0;
            rx_st_new = rx_st && rx_st < 0xF0;
            dump_hdr_n = b == 0xF0;
            if (b == 0xF0 && tp_recording && !rec_off && cfg.filt[5]) {
                sx_rec = sx_first = 1;
                sx_slot = 0;
            }
            continue;
        }
        if (dump_hdr_n) {
            if (b != dump_hdr[dump_hdr_n])
                dump_hdr_n = 0;
            else if (++dump_hdr_n == sizeof dump_hdr) {
                lcd_text(0, t_receiving, 32);
                lcd_flush();
                dump_a = PART_TABLE;
                dump_v = dump_n = 0;
                dump_hdr_n = DUMP_RX;
            }
        }
        if (sx_rec) {
            rec_sysex_byte(b);
            continue;
        }
        if (!rx_st)
            continue;
        if (!rx_n && (rx_st & 0xE0) != 0xC0) {
            rx_d1 = b;
            rx_t_msg = rx_t;
            rx_n = 1;
            continue;
        }
        rx_n = 0;
        if ((rx_st & 0xE0) == 0xC0) {
            rx_d1 = b;
            rx_t_msg = rx_t;
            b = 0;
        }
        rx_d2 = b;
        if (rx_st == 0xF2) {
            rx_st = 0;
            if (tp_state == TP_STOP) {
                if (cfg.echo) {
                    midi_tx(0xF2);
                    midi_tx(rx_d1);
                    midi_tx(b);
                }
                stop_spp = rx_d1 | ((uint16_t)b << 7);
                stop_on_n = 0;
                sync_spp = 1;
            }
            continue;
        }
#ifdef OPENMMT8_EDITX
        if (ed_on && tp_state == TP_STOP && ui_midi(rx_st, rx_d1, b))
            continue;
#endif
        echo();
        if (!sx_hold) {
            rec_event(rx_st, rx_d1, b);
            rx_st_new = 0;
        } else if (defer_n < MAX_DEFER) {
            defer_st[defer_n] = rx_st;
            defer_a[defer_n] = rx_d1;
            defer_b[defer_n++] = b;
        }
    }
}
