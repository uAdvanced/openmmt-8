/* Front panel user interface.
 *
 * Function keys (ERASE, COPY, TEMPO, ...) own the display while held; releasing the key that
 * opened a screen returns to the previous display. Every screen is rendered from state into
 * lcd[] and only changed cells reach the LCD. All timeouts count timer0 ticks. */
#include <stddef.h>
#include "core.h"

__bit ui_dirty;
#ifdef OPENMMT8_TAPE
__bit tape_run;
__bit tape_aborted;
__bit tape_wait;
__bit tape_heard;
static __bit tape_pin, tape_go;
__data uint8_t tape_edges, tape_expect;
static __data uint8_t tape_p3_ref;
static __bit tape_decode_go;
__data uint8_t tape_sh;
/* P3.2 edges after the sync tone before verify/load decode starts. */
#define TAPE_EDGES 238u
#define TAPE_PAGES 6
#else
/* SEND ALL PARTS & SONGS OUT MIDI only. */
#define TAPE_PAGES 1
#endif

#include "screens.h"


/* 10-character data selections shared by ERASE and COPY pages 1..22. */
static const char __code sel_items[6][10] = {
    "notes only", "pitch bend", "aftertouch", "controller", "patches   ", "sysex     ",
};
static const char __code filt_items[6][13] = {
    "NOTES:       ", "PITCH BEND:  ", "AFTER TOUCH: ",
    "CONTROLLERS: ", "PROG CHANGE: ", "SYSTEM EXCL: ",
};
#ifdef OPENMMT8_TAPE
#define CLK_LAST CLK_TAPE
#else
#define CLK_LAST CLK_INTERNAL
#endif
static const char __code clock_items[CLK_LAST + 1][16] = {
    "midi + internal ", "just internal   ",
#ifdef OPENMMT8_TAPE
    "sync from tape  ",
#endif
};
static const char __code quant_items[4][16] = {
    "note on         ", "note on + off   ", "note off        ", "keep note len   ",
};
static const char __code fractions[] = "2 4 6 8 121624324864";
static const char __code off_on[] = "OFFON ";
static const char __code midi_ch[] = "midi ch ";
static const char __code erase_[] = "erase ";
static const char __code unchanged[] = "no change";
static const char __code all[] = "ALL";
static const char __code empty_part[] = "  no part yet   ";
static const char __code empty_song[] = "  no song yet   ";

/* Keypad digit of key k (1-5 and 6-9, 0 on two rows), 0xFF = not a digit key. */
/* uint8 remainder. A subtract loop keeps __moduint out of the image. */
static uint8_t urem(uint8_t a, uint8_t b)
{
    while (a >= b)
        a = (uint8_t)(a - b);
    return a;
}

static uint8_t key_digit(uint8_t k)
{
    if ((uint8_t)(k - 33) < 5)
        return k - 32;
    if ((uint8_t)(k - 25) < 4)
        return k - 19;
    return k == 29 ? 0 : 0xFF;
}

__data uint8_t fn;                  /* key owning the display, K_NONE = none */
static __bit nm_live;              /* NAME: line 2 holds edits, not redrawn */
static __data uint8_t page;         /* page or cursor within fn */
static __xdata uint8_t msg;        /* message template, 0 = none */
static __xdata uint8_t msg_ticks, fn_poll;
static __bit msg_rec;
static __xdata uint8_t led_defer; /* passes until the LEDs follow the screen */
__bit edit_on;
__data uint8_t ent_n;
static __xdata uint8_t ent_ticks; /* passes left of a keypad entry, 0 = 256 */
static __xdata uint16_t ent_v;
__xdata char ent_c[4];
static __xdata uint16_t v_len;
static __xdata uint8_t v_dst, v_trans, v_trk, m1, m2;
#ifdef OPENMMT8_TAPE
static __xdata uint8_t tape_num;
static __data uint16_t tape_w;
static __data uint8_t tape_n;
#endif

static __xdata uint8_t rec_manual;
static __xdata uint8_t e_step; /* song edit step, 0-based */
static __xdata uint8_t e_mask, e_done, c_src, c_dst; /* ERASE tracks, COPY source/destination track */
/* TRANSPOSE/QUANTIZE tracks: all until a track is pressed, which selects it alone. */
static __xdata uint8_t t_mask;
static __bit t_picked;
/* FF/REW locate: beat shown (1-based, 0 = before beat 1); entered from playback; typed. */
static __xdata uint16_t loc_v;
static __xdata uint8_t loc_on, loc_run, loc_typed;
static const char __code beat_line[16] = "   BEAT         ";
static void shift_sync(void);

void ui_init(void)
{
    fn = K_NONE;
    rec_track = K_NONE;
    next_part = K_NONE;
    if (!cfg.mode_song)
        part_select(cfg.part);
    shift_sync();
    ui_dirty = 1;
}

/* ---- rendering helpers ---- */

static __xdata uint8_t scr_t = T_SELPART, scr_drawn = T_SELPART;

void ui_screen(uint8_t id)
{
    const uint8_t __code *s = tpl_z;
    __xdata char *d = lcd;
    __xdata uint8_t pend[8];
    uint8_t n, c, k = 0, i = 32, sp = 0;
    scr_t = id;
    for (n = 0; n < id; ++n)
        s += tpl_len[n];
    n = tpl_len[id];
    do {
        c = ' ';
        if (k)
            --k;
        else {
            if (sp)
                c = pend[--sp];
            else if (n) {
                --n;
                c = *s++;
            }
            /* A byte >= 0x80 is a pair in tpl_dict. The second byte waits. */
            while (c >= 0x80) {
                uint8_t ix = (uint8_t)((c - 0x80u) << 1);
                pend[sp++] = tpl_dict[ix + 1];
                c = tpl_dict[ix];
            }
            if (c < 0x20) {
                k = (uint8_t)(c - 1);
                c = ' ';
            }
        }
        *d++ = c;
    } while (--i);
}

uint16_t ui_cadence(uint8_t held)
{
    return held ? 7800u + tpl_pass[scr_t][0] : 7500u + tpl_pass[scr_t][1];
}

/* The pass length drawing the rendered screen over a different one, else 0. */
uint16_t ui_draw_cycles(void)
{
    uint8_t v = tpl_pass[scr_t][2];
    if (scr_t == scr_drawn)
        return 0;
    scr_drawn = scr_t;
    return v ? 9000u + v * 32u : 0;
}

#define field(pos, v, w) lcd_num8(pos, (uint8_t)(v), (w) | LCD_ENTRY)

static void lcd_part(uint8_t pos)
{
    lcd_num8(pos, cfg.part, 2);
}

static void lcd_song(uint8_t pos)
{
    lcd_num8(pos, cfg.song, 2);
}

static void name_line(xptr name)
{
    uint8_t i;
    lcd[16] = ' ';
    for (i = 0; i < 14; ++i)
        lcd[17 + i] = (char)name[i];
    lcd[31] = ' ';
}

static void off_on_at(uint8_t pos, uint8_t on)
{
    const char __code *s = off_on + (on ? 3 : 0);
    __xdata char *d = lcd + pos;
    for (on = 3; on; --on)
        *d++ = *s++;
}

static void sel_item(uint8_t pos, uint8_t pg)
{
    if (pg <= 6) {
        lcd_text(pos, sel_items[pg - 1], 10);
    } else {
        lcd_text(pos, midi_ch, 8);
        pg -= 6;
        if (pg < 10) {
            lcd[pos + 8] = (char)('0' + pg);
            lcd[pos + 9] = ' ';
        } else {
            lcd_num8(pos + 8, pg, 2);
        }
    }
}

/* Part at song step i of the current song, 0xFF past the end. */
static uint8_t song_part(uint8_t i)
{
    xptr s = song_rec(cfg.song);
    if (i >= song_steps(s))
        return 0xFF;
    return s[S_STEPS + 2 * i];
}

static void step_line(uint8_t part)
{
    lcd_num8(21, e_step + 1, 2);
    lcd_cursor = 30 | LCD_BLINK;
    if (ent_n || part != 0xFF)
        field(30, part, 2);
    else
        lcd_fill(30, 2);
}

static void render_home(void)
{
    xptr r;
    lcd_cursor = 13 | LCD_UL;
    if (cfg.mode_song) {
        if (edit_on) {
            ui_screen(T_EDITSONG);
            lcd_song(12);
            step_line(song_part(e_step));
            return;
        }
        ui_screen(T_SELSONG);
        field(13, cfg.song, 2);
        r = song_rec(cfg.song);
        if (song_steps(r))
            name_line(r + S_NAME);
        else
            lcd_text(16, empty_song, 16);
        return;
    }
    ui_screen(T_SELPART);
    field(13, cfg.part, 2);
    r = cur_part();
    if (r && part_len(r))
        name_line(r + P_NAME);
    else
        lcd_text(16, empty_part, 16);
}

static void render_play(void)
{
    CURSOR(14 | LCD_UL);
    if (cfg.mode_song) {
        ui_screen(T_SONGPLAY);
        lcd_num8(5, cfg.song, 2);
        lcd_num8(13, song_step + 1, 3);
        lcd_num8(21, play_part, 2);
        lcd_num(29, tp_beat + 1, 3);
        return;
    }
    if (tp_state == TP_COUNTIN) {
        if (scr_t != T_RECCD)
            ui_screen(T_RECCD);
        lcd_num8(14, cfg.part, 2);
        /* Waiting for the first MIDI clock, the count shows one beat more. */
        lcd_num8(30, count_left + (count_ticks == 0xFF && !ET0), 2);
    } else if (rec_screen) {
        ui_screen(T_RECBEAT);
        lcd_num8(14, cfg.part, 2);
        lcd_num(24, tp_beat + 1, 3);
    } else if (next_part != K_NONE || ent_n) {
        ui_screen(T_NEXT);
        lcd_num8(5, cfg.part, 2);
        if (next_part < 10) {
            field(14, next_part, 1);
            lcd[15] = ' ';
        } else
            field(14, next_part, 2);
        lcd_num(24, tp_beat + 1, 3);
    } else {
        ui_screen(T_PLAY);
        lcd_num8(14, cfg.part, 2);
        lcd_num(24, tp_beat + 1, 3);
    }
}

/* ---- part EDIT screens ---- */

enum { E_NOTE, E_PB, E_AT, E_CC, E_PC, E_SX, E_END }; /* ev_kind() - 1 */
/* Fields: event time beat and sub-beat, note, velocity, duration beats and sub-beats,
 * channel, pitch bend, controller number, value, system exclusive data byte. */
enum { F_BEAT, F_SUB, F_DBEAT, F_DSUB, F_NOTE, F_VEL, F_CHAN, F_CNUM, F_VAL, F_SX, F_PB };
/* PAGE UP order of the fields of each event type, and their count. */
static const uint8_t __code rings[5][7] = {
    {F_BEAT, F_SUB, F_NOTE, F_VEL, F_DBEAT, F_DSUB, F_CHAN},
    {F_BEAT, F_SUB, F_PB, F_CHAN},
    {F_BEAT, F_SUB, F_VAL, F_CHAN},
    {F_BEAT, F_SUB, F_CNUM, F_VAL, F_CHAN},
    {F_BEAT, F_SUB, F_VAL, F_CHAN},
};
static const uint8_t __code ring_n[7] = {7, 4, 4, 5, 4, 0, 1};
static const uint8_t __code f_pos[] = {0, 4, 16, 20, 8, 13, 29, 16, 20, 18, 17};
static const uint8_t __code f_dig[] = {3, 2, 3, 2, 3, 3, 2, 3, 3, 3, 4};
/* Byte fields: bits kept around the value, and its range. */
static const uint8_t __code f_keep[] = {0, 0, 0, 0, 0x80, 0, 0xF0, 0x80, 0x80, 0x80};
static const uint8_t __code f_lo[] = {0, 0, 0, 0, 0, 1, 1, 0, 0, 0};
static const uint8_t __code f_hi[] = {0, 0, 0, 0, 127, 127, 16, 121, 127, 127};
static const char __code note_l[] = "CCDDEFFGGAAB";
static const char __code ins_nm[6][9] = {
    "note     ", "bend     ", "atouch   ", "control  ", "patch    ", "sysex    ",
};
static const char __code ins_aft[] = "atouch   ";
static const char __code ins_ctrl[] = "control  ";
static const char __code ins_prog[] = "patch    ";
static const char __code erase_ev[] = "  delete event  ";
static __xdata uint8_t ed_fld, ed_ins; /* cursor field index, insert choice */
/* The page closed by its poll after a message: the part display shows, EDIT still on. */
static __bit ed_home;
#ifdef OPENMMT8_EDITX
static const char __code undo_ev[] = "UNDO LAST EDIT  ";
/* ed_fresh: the next change is the first on this event (undo keeps it). st_rec: RECORD
 * held for step entry. */
static __bit ed_fresh, st_rec, aud_on, cc_ref;
static __xdata uint16_t st_pos; /* the step: entry tick, and the FF/REW grid position */
static __xdata uint8_t aud_n, aud_ch, aud_t, cc_last;
#define AUD_PASSES 40

/* Time t at pos: "beat/sub-beat", or in bars of 4 beats "bar.beat.sub-beat". One
 * shift-subtract division by 96 gives the beat (r4:r3) and the sub-beat (r2); edopt.bars
 * is read at 0x05F3. */
static void lcd_time(uint8_t pos, uint16_t t) __naked
{
    pos;
    t;
    __asm
        mov     r7, dpl
        mov     r3, _lcd_time_PARM_2
        mov     r4, (_lcd_time_PARM_2 + 1)
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
        add     a, #0xa0
        jnc     00002$
        mov     r2, a
        inc     r3
00002$:
        djnz    r1, 00001$
        mov     dptr, #(0x05f0 + 3)
        movx    a, @dptr
        mov     r5, a
        mov     a, r7
        add     a, #4
        mov     r6, a
        mov     a, r7
        add     a, #(_lcd + 3)
        mov     dpl, a
        clr     a
        addc    a, #((_lcd + 3) >> 8)
        mov     dph, a
        mov     a, r5
        jz      00004$
        mov     a, #0x2e
        movx    @dptr, a
        inc     dptr
        mov     a, r3
        anl     a, #3
        add     a, #0x31
        movx    @dptr, a
        inc     dptr
        mov     r1, #2
00003$:
        clr     c
        mov     a, r4
        rrc     a
        mov     r4, a
        mov     a, r3
        rrc     a
        mov     r3, a
        inc     r6
        djnz    r1, 00003$
        mov     a, #0x2e
        sjmp    00005$
00004$:
        mov     a, #0x2f
00005$:
        movx    @dptr, a
        mov     _lcd_num8_PARM_2, r2
        mov     _lcd_num8_PARM_3, #2
        mov     dpl, r6
        push    ar7
        push    ar4
        push    ar3
        lcall   _lcd_num8
        pop     ar3
        pop     ar4
        pop     ar7
        mov     a, r3
        add     a, #1
        mov     _lcd_num_PARM_2, a
        clr     a
        addc    a, r4
        mov     (_lcd_num_PARM_2 + 1), a
        mov     _lcd_num_PARM_3, #3
        mov     dpl, r7
        ljmp    _lcd_num
    __endasm;
}
#endif

/* Data byte k of the system exclusive message at ew.e, 0 past its end. */
static xptr sx_at(uint8_t k)
{
    xptr e = ew.e;
    uint8_t s;
    for (;;) {
        e += e[0] & 0x80 ? 3 : 1;
        for (s = 0; s < 4; s += s ? 2 : 1) {
            if (s && (e[s] & 0x80))
                return 0;
            if (!k--)
                return e + s;
        }
        e += 4;
        if (e[0] != 0x7D)
            return 0;
    }
}

/* The editor's event, type, field count and cursor field (ew.k: its data byte). */
static void ed_look(uint8_t back)
{
    uint8_t i;
    ew.e = ed_event(back);
    ew.ty = ev_end(ew.e) ? E_END : ev_kind(ew.e) - 1;
    ew.n = ring_n[ew.ty];
    if (ew.ty == E_SX)
        for (ew.n = 2; sx_at(ew.n - 2); ++ew.n)
            ;
    i = urem(ed_fld, ew.n);
    ew.k = i - 2;
    ew.f = i < 2 ? i : ew.ty == E_SX ? F_SX : rings[ew.ty][i];
}

#ifdef OPENMMT8_EDITX
/* With FF/REW by the grid, the step holds no event from the editor's on. */
static uint8_t ed_empty(void)
{
    return edopt.grid && (ew.ty == E_END || ew.time >= st_pos + ed_grid());
}

/* No event to change: the end, or an empty step. */
static uint8_t ed_end(void)
{
    return ew.ty == E_END || ed_empty();
}

/* The step to the one holding the editor's event. */
static void st_snap(void)
{
    uint8_t g = ed_grid();
    if (ew.ty != E_END)
        st_pos = ew.time / g * g;
}

/* The step to the next one within the part, and the editor to its first event. */
static void st_go(uint16_t p)
{
    st_pos = p;
    ed_seek(p);
}

static void st_next(void)
{
    uint16_t n = st_pos + ed_grid();
    st_go(n < cur_len() * TICKS_PER_BEAT ? n : st_pos);
}

/* The editor to the event after its own. */
static void ed_fwd(void)
{
    ed_off = (uint16_t)(ed_next(ew.e) - ed_base());
    ed_look(0);
}

/* The edit track's channel setting of the part (0 = as recorded). */
static uint8_t ed_chan(void)
{
    return cur_part()[P_CHAN + 7 - ed_trk];
}

static void aud_tx(uint8_t st, uint8_t v)
{
    midi_tx(st | aud_ch);
    midi_tx(aud_n);
    midi_tx(v);
}

static void aud_off(void)
{
    if (aud_on) {
        aud_on = 0;
        aud_tx(0x80, 0x40);
    }
}

/* Audition: with the option on and stopped, the editor's note sounds briefly. */
static void audition(void)
{
    xptr b;
    aud_off();
    if (!edopt.aud || tp_state != TP_STOP)
        return;
    ed_look(0);
    if (ew.ty != E_NOTE || ed_empty())
        return;
    b = ev_body(ew.e);
    aud_ch = ed_chan();
    aud_ch = aud_ch ? aud_ch - 1 : b[1] & 15;
    aud_n = ew.e[0] & 0x7F;
    aud_tx(0x90, *b);
    aud_on = 1;
    aud_t = AUD_PASSES;
}
#endif

static int16_t clampi(int16_t v, int16_t lo, int16_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* The byte holding byte field f of the editor's event. */
static xptr ed_ptr(uint8_t f)
{
    xptr b = ev_body(ew.e);
    return f == F_SX ? sx_at(ew.k) : f == F_NOTE || f == F_CNUM ? ew.e : f == F_CHAN ? b + 1 : b;
}

/* The event time for the time fields, else the note duration. */
static uint16_t ed_ticks(uint8_t f)
{
    xptr b = ev_body(ew.e);
    return f < F_DBEAT ? ew.time : (b[2] << 8) | b[3];
}

/* Value of field f of the editor's event (beats of the time count from 1). */
static int16_t ed_get(uint8_t f)
{
    xptr b = ev_body(ew.e);
    uint16_t u;
    if (f < F_NOTE) {
        u = ed_ticks(f);
        return f & 1 ? mod96(u) : u / TICKS_PER_BEAT + !f;
    }
    if (f == F_PB)
        return ((b[3] << 7) | b[2]) - 8192;
    return (*ed_ptr(f) & ~f_keep[f]) + (f == F_CHAN);
}

/* Store v in the cursor field: a beat or sub-beat carries into the time, which must stay
 * within the part (the event then moves); a duration is at least one tick. */
#ifdef OPENMMT8_EDITX
static void ed_store(int16_t v)
#else
static void ed_set(int16_t v)
#endif
{
    uint8_t f = ew.f;
    uint16_t u;
    xptr b = ev_body(ew.e);
    if (f < F_NOTE) {
        u = ed_ticks(f);
        v = f & 1 ? v + u - mod96(u) : (v - !f) * TICKS_PER_BEAT + mod96(u);
        if (f < F_DBEAT) {
            if ((uint16_t)v < cur_len() * TICKS_PER_BEAT && (uint16_t)v != u) {
                ew.time = v;
                ed_rebuild(ew.e);
            }
            return;
        }
        if (v < 1)
            v = 1;
        b[2] = (uint8_t)(v >> 8);
        b[3] = (uint8_t)v;
    } else if (f == F_PB) {
        v = clampi(v, -8192, 8191) + 8192;
        b[2] = v & 0x7F;
        b[3] = (uint8_t)(v >> 7);
    } else {
        v = clampi(v, f_lo[f], f_hi[f]) - (f == F_CHAN);
        b = ed_ptr(f);
        *b = (*b & f_keep[f]) | (uint8_t)v;
    }
}

#ifdef OPENMMT8_EDITX
/* The single path of field changes: the first on an event keeps it for undo. */
static void ed_set(int16_t v)
{
    if (ed_fresh) {
        ed_fresh = 0;
        ed_snap();
    }
    ed_store(v);
    ed_mark();
    if (ew.f < F_DBEAT)
        st_snap();
}

static void ed_bump(int8_t d)
{
    ed_set(ed_get(ew.f) + d);
    audition();
}
#endif

static uint8_t ed_pos(uint8_t f)
{
    if (f == F_SX)
        return 18 + 4 * urem(ew.k, 3);
#ifdef OPENMMT8_EDITX
    if (edopt.bars && (f == F_SUB || f == F_NOTE))
        return f == F_SUB ? 6 : 9;
#endif
    return f == F_VAL && ew.ty != E_CC ? 17 : f_pos[f];
}

static void render_edit(void)
{
    uint8_t i, f, pos;
    int16_t v;
    xptr p;
    ed_look(0);
#ifdef OPENMMT8_EDITX
    if (st_rec || ed_empty()) {
        ui_screen(st_rec ? T_STEPIN : T_EDSTEP);
        lcd_time(st_rec ? 8 : 0, st_pos);
        lcd_text(st_rec ? 30 : 26, fractions + 2 * cfg.quant_val, 2);
        CURSOR(0);
        return;
    }
#endif
    if (ew.ty == E_END) {
        ui_screen(T_EDEND);
        CURSOR(0);
        return;
    }
    ui_screen(T_EDEV);
#ifdef OPENMMT8_EDITX
    lcd_time(0, ew.time);
#endif
    lcd[19] = ew.ty == E_NOTE ? '/' : ew.ty == E_CC ? ':' : ' ';
#ifdef OPENMMT8_EDITX
    if (ew.ty != E_NOTE) {
        lcd_text(8 + edopt.bars, ins_nm[ew.ty], 9 - edopt.bars);
        lcd[16] = ' ';
    }
#else
    if (ew.ty != E_NOTE)
        lcd_text(8, ins_nm[ew.ty], 9);
#endif
    if (ew.ty == E_SX) {
        lcd_fill(16, 16);
        f = ew.f == F_SX ? ew.k / 3u * 3u : 0;
        for (i = 0; i < 4; ++i)
            if ((p = sx_at(f + i)))
                if (i < 3)
                    lcd_num8(18 + 4 * i, *p & 0x7F, 3);
                else
                    lcd[30] = '~';
        i = 2;
    } else {
        i = ew.n;
#ifdef OPENMMT8_EDITX
        if (ed_chan()) {
#else
        if (cur_part()[P_CHAN + 7 - ed_trk]) {
#endif
            lcd[28] = '(';
            lcd[31] = ')';
        }
    }
    while (i--) {
        f = ew.ty == E_SX ? i : rings[ew.ty][i];
#ifdef OPENMMT8_EDITX
        if (f < F_DBEAT)
            continue;
#endif
        v = ed_get(f);
        pos = ed_pos(f);
        if (f == F_NOTE) {
            f = urem((uint8_t)v, 12);
            lcd[pos++] = note_l[f];
            if ((0x54A >> f) & 1)
                lcd[pos++] = '#';
            f = (uint8_t)((uint8_t)v / (uint8_t)12);
            if (f < 2) {
                lcd[pos++] = '-';
                f = 4 - f;
            }
            lcd[pos] = (char)('0' + f - 2);
        } else if (f == F_PB) {
            lcd[16] = v < 0 ? '-' : ' ';
            lcd_num(17, v < 0 ? -v : v, 4);
        } else {
            lcd_num(pos, v, f_dig[f]);
        }
    }
    pos = ed_pos(ew.f);
    lcd_cursor = pos | LCD_BLINK;
    if (ent_n) {
        f = ew.f == F_NOTE ? 5 : f_dig[ew.f];
        for (i = 0; i < f; ++i)
            lcd[pos + i] = i < ent_n ? ent_c[i] : ' ';
    }
}

static void render_insert(void)
{
    ed_look(0);
    ui_screen(T_INSERT);
    CURSOR(7 | LCD_BLINK);
    lcd_text(7, ed_ins == 4 ? ins_prog : ed_ins == 2 ? ins_aft : ed_ins == 3 ? ins_ctrl : ins_nm[ed_ins], 9);
#ifdef OPENMMT8_EDITX
    v_len = ed_empty() ? st_pos : ew.ty == E_END ? ew.time - 1 : ew.time;
    lcd_time(19, v_len);
#else
    v_len = ew.ty == E_END ? ew.time - 1 : ew.time;
    lcd_num(19, v_len / TICKS_PER_BEAT + 1, 3);
    lcd_num8(23, mod96(v_len), 2);
#endif
}

__bit shift_live;

static __xdata uint8_t *shift_at(void)
{
    return (__xdata uint8_t *)cfg.shift + v_trk;
}

static void shift_sync(void) __naked
{
    __asm
        mov     dptr, #0x04df
        mov     r7, #8
        clr     _shift_live
00001$:
        movx    a, @dptr
        jz      00002$
        setb    _shift_live
00002$:
        inc     dptr
        djnz    r7, 00001$
        ret
    __endasm;
}

/* Song-mode quantize: a signed per-track delay in 384ths. 00 on the keypad is positive. */
static void shift_draw(void)
{
    uint8_t s = *shift_at();
    ui_screen(T_SHIFT);
    if (s & 0x80) {
        lcd[16] = '-';
        s = -s;
    } else {
        lcd[16] = ' ';
    }
    field(17, s, 2);
}

static void shift_store(uint8_t s)
{
    *shift_at() = s;
    shift_sync();
}

/* Dense ids so the screen switch is a jump table. 0 is the home fallback. */
static const uint8_t __code rfn_id[] = {
    K_REC, K_MEMORY, K_LENGTH, K_TEMPO, K_TRANS, K_ERASE, K_COPY, K_MERGE,
    K_CHAN, K_QUANT, K_FILTER, K_CLOCK, K_CLICK, K_TAPE, 0
};

#define CB(pos) ((pos) | LCD_BLINK)
#ifndef OPENMMT8_TAPE
/* The cell of the value each function screen edits; the cases below refine it by page. */
static const uint8_t __code rfn_cur[] = {
    0, 0, CB(23), CB(10), CB(17), 0, CB(28), 0, CB(20), CB(14), CB(29), CB(24), 0, 0
};
static const uint8_t __code click_cur[4] = {CB(14), CB(29), CB(11), CB(28)};
#endif

static void render_fn(void)
{
    xptr r;
    uint8_t v, i;

    for (i = 0; rfn_id[i] != fn && rfn_id[i]; ++i)
        ;
    CURSOR(rfn_cur[i]);
    switch (i) {
    case 0:
        ui_screen(T_RECSEL);
        break;
    case 1: {
        uint16_t pct = heap_free() / 632u;
        ui_screen(T_MEMORY);
        if (pct > 99)
            lcd_num(18, pct, 3);
        else
            lcd_num(18, pct, 2);
        break;
    }
    case 2:
        ui_screen(page ? T_LENTOP : T_LEN);
        if (!page)
            lcd_part(9);
        lcd_num(23, v_len, 3 | LCD_ENTRY);
        break;
    case 3:
        ui_screen(T_TEMPO);
        field(10, cfg.tempo, 3);
        break;
    case 4:
        ui_screen(page ? T_TRDN : T_TRUP);
        field(17, v_trans, 2);
        break;
    case 5:
        if (ed_on) {
            render_edit();
#ifdef OPENMMT8_EDITX
            lcd_text(16, ed_ins ? undo_ev : erase_ev, 16);
#else
            lcd_text(16, erase_ev, 16);
#endif
            break;
        }
        if (cfg.mode_song && edit_on) {
            ui_screen(T_ERASESTEP);
            step_line(song_part(e_step));
            break;
        }
        if (cfg.mode_song) {
            ui_screen(T_ERASESONG);
            lcd_song(13);
            break;
        }
        ui_screen(T_ERASEPART);
        lcd_part(13);
        if (page) {
            lcd_text(16, erase_, 6);
            sel_item(22, page);
            CURSOR(CB(22));
        }
        break;
    case 6:
        if (ed_on) {
            render_insert();
            break;
        }
        if (cfg.mode_song && edit_on) {
            ui_screen(T_INSSTEP);
            step_line(v_dst);
            break;
        }
        if (cfg.mode_song) {
            ui_screen(T_COPYSONG);
            lcd_song(17);
            field(28, v_dst, 2);
        } else if (page) {
            ui_screen(T_COPYSEL);
            sel_item(6, page);
            lcd_part(22);
            field(28, v_dst, 2);
        } else {
            ui_screen(T_COPYPART);
            lcd_part(17);
            field(28, v_dst, 2);
        }
        break;
    case 7:
        ui_screen(T_MERGE);
        break;
    case 8:
        ui_screen(T_CHAN);
        r = cur_part();
        v = r[P_CHAN + 7 - v_trk];
        if (ent_n)
            field(20, 0, 2);
        else if (v)
            lcd_num8(20, v, 2);
        else
            lcd_text(20, unchanged, 9);
        break;
    case 9:
        if (cfg.mode_song) {
            shift_draw();
            CURSOR(CB(17));
            break;
        }
        ui_screen(T_QUANT);
        lcd_text(14, fractions + 2 * cfg.quant_val, 2);
        lcd_text(16, quant_items[cfg.quant_mode], 16);
        break;
    case 10:
#ifdef OPENMMT8_EDITX
        if (page > 6) {
            ui_screen(T_EDCC + page - 7);
            v = ((__xdata uint8_t *)&edopt)[page - 7];
            if (page == 7 && (v || ent_n))
                field(29, v, 3);
            else
                off_on_at(29, v);
            break;
        }
#endif
        if (page == 6) {
            ui_screen(T_FILTCH);
            if (ent_n)
                field(29, 0, 2);
            else if (cfg.filt_chan)
                lcd_num8(29, cfg.filt_chan, 2);
            else
                lcd_text(29, all, 3);
            break;
        }
        ui_screen(T_FILT);
        lcd_text(16, filt_items[page], 13);
        off_on_at(29, cfg.filt[page]);
        break;
    case 11:
        ui_screen(T_CLKSRC + page);
        if (!page) {
            lcd_text(16, clock_items[cfg.clock_src], 16);
            CURSOR(CB(16));
        } else
            off_on_at(24, page == 1 ? cfg.clock_out : cfg.auto_start);
        break;
    case 12:
        CURSOR(click_cur[page]);
        if (page < 2) {
            ui_screen(T_CLICKA);
            lcd_text(14, fractions + 2 * cfg.click_val, 2);
            off_on_at(29, cfg.rec_click);
        } else {
            ui_screen(T_CLICKB);
            off_on_at(11, cfg.play_click);
            field(28, cfg.countdown, 2);
        }
        break;
    case 13:
#ifdef OPENMMT8_TAPE
        ui_screen(T_TAPE0 + page);
        if (page == 3 || page == 4)
            field(28, tape_num, 2);
#else
        ui_screen(T_TAPE5);
#endif
        break;
    default:
        render_home();
        break;
    }
}

static void render_lcd(void)
{
    if (msg) {
        ui_screen(msg);
        CURSOR(0);
        return;
    }
    if (fn == K_NAME) {
        if (!nm_live)
            render_home();
        lcd_cursor = (17 + page) | LCD_BLINK;
        return;
    }
    if (fn != K_NONE) {
        render_fn();
        return;
    }
    if (loc_on) {
        uint8_t i, d, n = ent_n;
        ent_n = 0;
        if (loc_run)
            render_play();
        else
            render_home();
        ent_n = n;
        d = 24;
        if (cfg.mode_song) {
            d = 29;
            lcd_fill(29, 3);
        } else {
            lcd_text(16, beat_line, 16);
        }
        CURSOR(d | LCD_BLINK);
        if (n) {
            for (i = 0; i < n; ++i)
                lcd[d + i] = ent_c[i];
        } else {
            lcd_num(d, loc_v, 3);
        }
        return;
    }
    if (ed_on && !ed_home) {
        render_edit();
        return;
    }
    if (tp_state != TP_STOP) {
        render_play();
        return;
    }
    render_home();
}

/* ---- LEDs ---- */

/* FF04 mode LEDs, active low. */
#define L_PLAY 0x01
#define L_REC 0x02
#define L_PART 0x04
#define L_EDIT 0x08
#define L_SONG 0x10
#define L_ECHO 0x20
#define L_LOOP 0x40
/* Flash half-periods in key repeat periods: the record track choice, the copy destination. */
#define FLASH_REC 40
#define FLASH_COPY 80

static __xdata uint8_t led_trk = 0xAA, led_mode = 0xAA, fl_n, fl_on;

static uint8_t trk_bit(uint8_t t)
{
    return t == K_NONE ? 0 : bit_tab[t];
}

/* Leaf (no calls) as it runs from the key scan's pass, the deepest main loop path. */
static uint8_t empty_tracks(void)
{
    xptr s = (xptr)(PART_TABLE + 2u * cfg.part), p, e;
    uint8_t t, m = 0;
    p = (xptr)((s[0] << 8) | s[1]);
    s = p + P_OFF + 14;
    for (t = 1; t; t <<= 1, s -= 2) {
        e = p + (uint16_t)(s[0] | (s[1] << 8));
        if (e[0] == 0x80 && (e[4] & 0x80))
            m |= t;
    }
    return m;
}

/* Track LEDs lit (active high). */
static uint8_t tracks_lit(void)
{
    uint8_t v;
    if (msg == T_COPYDONE)
        return 0;
    if (ed_on && !ed_home)
        return trk_bit(ed_trk);
    switch (fn) {
    case K_MEMORY:
        return 0;
    case K_REC:
        v = trk_bit(rec_track);
        return fl_on ? v | empty_tracks() : v;
    case K_CHAN:
        return trk_bit(v_trk);
    case K_COPY:
        if (c_src == K_NONE)
            return 0xFF;
        return trk_bit(c_src) | (fl_on ? 0 : trk_bit(c_dst));
    case K_ERASE:
        if (e_done > 1 || (e_done && msg != T_ERASEDONE))
            return 0;
        return e_mask ? e_mask : 0xFF;
    case K_QUANT:
    case K_TRANS:
        return t_mask;
    case K_MERGE:
        return trk_bit(m1) | trk_bit(m2);
    }
    if (rec_armed) {
        if (tp_state == TP_COUNTIN || tp_state == TP_PLAY)
            return 0xFF;
        return trk_bit(rec_track) | empty_tracks();
    }
    return (uint8_t)~mute;
}

void ui_leds(void)
{
    uint8_t v = (uint8_t)~tracks_lit();
    if (v != led_trk)
        LED_DATA = led_trk = v;
    v = 0xFF;
    if (tp_state == TP_PLAY || (loc_on && loc_run))
        v &= ~L_PLAY;
    if (rec_armed || ((fn == K_REC || fn == K_MEMORY) && rec_track != K_NONE))
        v &= ~L_REC;
    v &= cfg.mode_song ? ~L_SONG : ~L_PART;
    if (edit_on || ed_on)
        v &= ~L_EDIT;
    if (cfg.echo)
        v &= ~L_ECHO;
    if (cfg.loop)
        v &= ~L_LOOP;
    if (v != led_mode)
        LED_BANK = led_mode = v;
}

/* Recording stops: the record light goes out before the take is stored. */
void ui_led_rec_off(void)
{
    LED_BANK = led_mode |= L_REC;
}

static void fn_close(void);

/* Called once per pass of the original unit's main loop. */
void ui_pass(void)
{
    uint8_t d = led_defer;
#ifdef OPENMMT8_EDITX
    if (aud_t && !--aud_t)
        aud_off();
#endif
    if (ent_n && !--ent_ticks) {
        ent_n = 0;
        if (tp_state == TP_STOP)
            ui_dirty = 1;
    }
    if (msg) {
        if (key_held(K_REC))
            msg_ticks = MSG_PASSES;
        else if (!--msg_ticks) {
            msg = 0;
            v_trans = 0;
            ui_dirty = 1;
            fn_poll = 1;
            led_defer = fn == K_ERASE;
        }
    } else if (fn_poll && !--fn_poll) {
        if (fn != K_NONE && !key_held(fn)) {
            fn_close();
            ed_home = ed_on;
            ui_dirty = 1;
            led_defer = 2;
            d = 0;
        }
    }
    if (d && !--led_defer)
        ui_leds();
    if (fl_n && !--fl_n) {
        fl_n = fn == K_REC ? FLASH_REC : FLASH_COPY;
        fl_on ^= 1;
        ui_leds();
    }
}

#ifdef OPENMMT8_TAPE
static void tape_progress(void);
#endif

void ui_render(void)
{
#ifdef OPENMMT8_TAPE
    if (tape_run) {
        if (tape_aborted)
            ui_screen(T_TAPEABT);
        else if (tape_heard)
            tape_progress();
        else
            ui_screen(tape_wait ? T_TAPENOW : T_TAPESAV);
        lcd_cursor = 0;
        return;
    }
#endif
    render_lcd();
    if (!led_defer)
        ui_leds();
}

/* ---- keypad entry ---- */

/* Collect one digit of a `width`-digit number. Returns 1 when complete (value in ent_v).
 * A first digit above first_max, or a completed value above max, is ignored. */
static uint8_t entry(uint8_t d, uint8_t width, uint8_t first_max, uint16_t max)
{
    uint16_t v = ent_n ? (((ent_v << 2) + ent_v) << 1) + d : d;
    ent_ticks = 0;
    if (!ent_n && d > first_max)
        return 0;
    if (ent_n + 1 == width && v > max)
        return 0;
    ent_c[ent_n++] = (char)('0' + d);
    ent_v = v;
    if (ent_n < width)
        return 0;
    ent_n = 0;
    return 1;
}

/* A two-digit entry, 00 to 99. */
static uint8_t entry99(uint8_t d)
{
    return entry(d, 2, 9, 99);
}

/* A MIDI channel, 01 to 16. */
static uint8_t entry16(uint8_t d)
{
    return entry(d, 2, 1, 16);
}

/* ---- FF/REW locate ---- */

static uint16_t loc_len(void)
{
    xptr p = part_rec(cfg.mode_song ? play_part : cfg.part);
    return p ? part_len(p) : 0;
}

static void loc_start(void)
{
    loc_on = 1;
    loc_typed = 0;
    ent_n = 0;
    loc_run = tp_state == TP_PLAY;
    if (loc_run) {
        loc_v = tp_beat + 1;
        seq_stop();
    } else {
        loc_v = stop_spp / 4u + 1u;
    }
    keys_locate_start();
}

static void loc_end(void)
{
    loc_on = 0;
    ent_n = 0;
    seq_locate(loc_v);
    if (loc_run)
        seq_continue();
}

void ui_locate_step(uint8_t fwd)
{
    uint16_t len;
    if (!loc_on || loc_typed || ent_n)
        return;
    len = loc_len();
    if (fwd) {
        if (loc_v < len)
            ++loc_v;
        else if (cfg.loop)
            loc_v = 1;
    } else {
        if (loc_v)
            --loc_v;
        else if (cfg.loop)
            loc_v = len;
    }
    ui_dirty = 1;
}

static void loc_key(uint8_t k)
{
    uint8_t d = key_digit(k);
    if (d == 0xFF || loc_typed)
        return;
    if (entry(d, 3, 9, 999)) {
        loc_typed = 1;
        loc_v = ent_v && ent_v <= loc_len() ? ent_v : 1;
    }
}

static void show_msg(uint8_t id)
{
    msg = id;
    msg_ticks = MSG_PASSES;
    msg_rec = 1;
}

static void msg_copy(uint8_t ok)
{
    show_msg(ok ? T_COPYDONE : T_FULL);
    msg_rec = 0;
    if (fl_n) {
        fl_n = FLASH_REC;
        fl_on = 0;
    }
}

static uint8_t lowest_empty_track(void)
{
    xptr p = cur_part();
    uint8_t t;
    for (t = 0; t < 8; ++t)
        if (ev_end(track(p, t)))
            return t;
    return K_NONE;
}

static uint8_t wrap100(uint8_t v, uint8_t up)
{
    if (up)
        return v == 99 ? 0 : v + 1;
    return v ? v - 1 : 99;
}

static uint8_t ring(uint8_t v, uint8_t up, uint8_t n)
{
    if (up)
        return (uint8_t)(v + 1 == n ? 0 : v + 1);
    return (uint8_t)(v ? v - 1 : n - 1);
}

/* Returns the clamped value v + d within lo..hi. */
static uint8_t clamp_step(uint8_t v, uint8_t up, uint8_t lo, uint8_t hi)
{
    if (up)
        return v < hi ? v + 1 : v;
    return v > lo ? v - 1 : v;
}

/* ---- function screens ---- */

static void fn_open(uint8_t k)
{
    if (cfg.mode_song) {
        if (k == K_LENGTH || k == K_MERGE || k == K_CHAN || k == K_TRANS || k == K_REC)
            return;
        if (k == K_NAME && !song_rec(cfg.song))
            return;
        if (edit_on && (k == K_COPY || k == K_ERASE) && song_part(e_step) == 0xFF)
            return;
    }
    if (tp_state != TP_STOP && k != K_TEMPO && k != K_CLICK && k != K_CHAN && k != K_FILTER) {
        if (k == K_REC && tp_state == TP_PLAY) {
            if (!rec_manual)
                rec_track = lowest_empty_track();
            seq_punch_in();
        }
        return;
    }
    fn = k;
    page = 0;
    ent_n = 0;
    fl_n = fl_on = 0;
    switch (k) {
    case K_LENGTH:
        v_len = cur_len();
        break;
    case K_COPY:
        v_dst = cfg.mode_song ? (edit_on ? song_part(e_step) : cfg.song) : cfg.part;
        c_src = c_dst = K_NONE;
        break;
    case K_ERASE:
        e_mask = e_done = 0;
        break;
#ifdef OPENMMT8_EDITX
    case K_FILTER:
        if (ed_on)
            page = 7;
        break;
#endif
    case K_TRANS:
        v_trans = 0;
        t_picked = 0;
        t_mask = 0xFF;
        break;
    case K_QUANT:
        t_picked = 0;
        t_mask = 0xFF;
        if (cfg.mode_song) {
            v_trk = 0;
            t_mask = 1;
        }
        break;
    case K_CHAN:
        v_trk = 0;
        break;
    case K_MERGE:
        m1 = m2 = K_NONE;
        break;
#ifdef OPENMMT8_TAPE
    case K_TAPE:
        tape_num = 0;
        break;
#endif
    case K_REC:
        if (!rec_manual)
            rec_track = lowest_empty_track();
        fl_n = FLASH_REC + 2;
        fl_on = 1;
        break;
    }
}

static void fn_close(void)
{
    if (fn == K_REC || fn == K_MEMORY)
        rec_armed = rec_track != K_NONE;
    fn = K_NONE;
    ent_n = 0;
    nm_live = 0;
    fl_n = fl_on = 0;
}

/* ---- part EDIT keys ---- */

/* A typed value: the beat within the part, sub-beats up to 95, the sign of the shown pitch
 * bend kept. */
static void ed_typed(void)
{
    uint8_t f = ew.f;
    int16_t v = (int16_t)ent_v;
#ifdef OPENMMT8_EDITX
    if (f == F_BEAT && edopt.bars)
        v = ((v - 1) << 2) + 1 + ((ed_get(F_BEAT) - 1) & 3);
#endif
    if (f < F_NOTE)
        v = clampi(v, !f, f & 1 ? 95 : f ? 682 : cur_len());
    else if (f == F_PB && ed_get(F_PB) < 0)
        v = -v;
    ed_set(v);
}

#ifdef OPENMMT8_EDITX
/* RECORD with ERASE or COPY held in part EDIT: erase the event or undo the last edit, or
 * insert the choice (on an empty step, at the step). */
static void ed_execute(void)
{
    uint8_t empty;
    ed_look(0);
    empty = ed_empty();
    if (fn == K_ERASE) {
        if (ed_ins) {
            show_msg(ed_undo() ? T_UNDONE : T_FULL);
            ed_look(0);
            st_snap();
        } else if (!ed_end()) {
            ed_rebuild(0);
            show_msg(T_EVERASED);
            ed_undo_ok = 0;
        }
        fn = K_NONE;
    } else {
        if (empty)
            ew.time = st_pos;
        else if (ew.ty == E_END)
            --ew.time;
        show_msg(ed_insert(ed_ins) ? T_EVINS : T_FULL);
        ed_undo_ok = 0;
    }
    ed_fresh = 1;
    msg_rec = 0;
}

/* FF/REW by the grid: through the events of the step, then to the next or previous step. */
static void grid_move(uint8_t up)
{
    uint16_t o = ed_off;
    if (up) {
        if (!ed_empty()) {
            ed_fwd();
            if (!ed_empty())
                return;
        }
        st_next();
        return;
    }
    st_go(st_pos);
    if (ed_off == o && st_pos) {
        o = ed_grid();
        st_go(st_pos > o ? st_pos - o : 0);
    }
}

/* Keys of part EDIT with no function page open. Returns 0 for a key it leaves. */
static uint8_t ed_key(uint8_t k)
{
    uint8_t d = key_digit(k), end;
    ed_home = 0;
    ed_look(0);
    end = ed_end();
    if (d != 0xFF) {
        if (!end && entry(d, f_dig[ew.f], 9, 9999))
            ed_typed();
        return 1;
    }
    if (k != K_PLUS && k != K_MINUS)
        ed_fresh = 1;
    if (k >= K_TRK1 && k < K_TRK1 + 8) {
        ed_trk = k - K_TRK1;
        ed_off = st_pos = 0;
        audition();
    } else switch (k) {
    case K_EDIT:
        ed_on = 0;
        break;
    case K_ERASE:
        if (end && !ed_undo_ok)
            break;
        /* fall through */
    case K_COPY:
        fn_open(k);
        ed_ins = k == K_ERASE && end;
        break;
    case K_FF:
    case K_REW:
        if (edopt.grid) {
            grid_move(k == K_FF);
        } else {
            if (k == K_REW) {
                ed_look(1);
                ed_look(0);
            } else if (!end)
                ed_fwd();
            st_snap();
        }
        audition();
        break;
    case K_PGUP:
    case K_PGDN:
        if (!end)
            ed_fld = ring(urem(ed_fld, ew.n), k == K_PGUP, ew.n);
        break;
    case K_PLUS:
    case K_MINUS:
        if (!end)
            ed_bump(k == K_PLUS ? 1 : -1);
        break;
    case K_REC:
        st_rec = 1;
        break;
    default:
        return 0;
    }
    ent_n = 0;
    return 1;
}

/* MIDI in while part EDIT is stopped with no page open. The data knob controller moves the
 * cursor field by its change (not echoed: returns 1). A note on with RECORD held enters a
 * note at the step, one grid step long; otherwise it sets the shown note and velocity. */
uint8_t ui_midi(uint8_t st, uint8_t a, uint8_t b)
{
    uint8_t knob, end;
    if (fn != K_NONE || loc_on)
        return 0;
    ed_home = 0;
    ui_dirty = 1;
    ed_look(0);
    st &= 0xF0;
    knob = st == 0xB0 && a == edopt.cc && a;
    end = ed_end();
    if (knob) {
        st = b - cc_last;
        cc_last = b;
        if (cc_ref && !end)
            ed_bump((int8_t)st);
        cc_ref = 1;
    } else if (st == 0x90 && b) {
        if (st_rec) {
            if (!ed_step(st_pos))
                show_msg(T_FULL);
            ed_fresh = 1;
        } else if (ew.ty == E_NOTE && !end) {
            ew.f = F_NOTE;
            ed_set(a);
            *ev_body(ew.e) = b;
        }
    }
    return knob;
}
#else
/* RECORD with ERASE or COPY held in part EDIT: erase the event, or insert the choice. */
static void ed_execute(void)
{
    ed_look(0);
    if (fn == K_ERASE) {
        ed_rebuild(0);
        show_msg(T_EVERASED);
        fn = K_NONE;
    } else {
        if (ew.ty == E_END)
            --ew.time;
        show_msg(ed_insert(ed_ins) ? T_EVINS : T_FULL);
    }
    msg_rec = 0;
}

/* Keys of part EDIT with no function page open. Returns 0 for a key it leaves. */
static uint8_t ed_key(uint8_t k)
{
    uint8_t d = key_digit(k);
    ed_home = 0;
    ed_look(0);
    if (d != 0xFF) {
        if (ew.ty != E_END && entry(d, f_dig[ew.f], 9, 9999))
            ed_typed();
        return 1;
    }
    if (k >= K_TRK1 && k < K_TRK1 + 8) {
        ed_trk = k - K_TRK1;
        ed_off = 0;
    } else switch (k) {
    case K_EDIT:
        ed_on = 0;
        break;
    case K_ERASE:
        if (ew.ty == E_END)
            break;
        /* fall through */
    case K_COPY:
        fn_open(k);
        ed_ins = 0;
        break;
    case K_FF:
        if (ew.ty != E_END)
            ed_off = (uint16_t)(ed_next(ew.e) - ed_base());
        break;
    case K_REW:
        ed_look(1);
        break;
    case K_PGUP:
    case K_PGDN:
        if (ew.ty != E_END)
            ed_fld = ring(urem(ed_fld, ew.n), k == K_PGUP, ew.n);
        break;
    case K_PLUS:
    case K_MINUS:
        if (ew.ty != E_END)
            ed_set(ed_get(ew.f) + (k == K_PLUS ? 1 : -1));
        break;
    case K_REC:
        break;
    default:
        return 0;
    }
    ent_n = 0;
    return 1;
}
#endif

/* +/- on a byte setting: function key, page (0xFF any; 0x80 the filter pages 0-5, the
 * setting of page 0 first), setting and range. */
#define CFG_AT(f) ((__xdata uint8_t *)(0x04C8 + offsetof(cfg_t, f)))
#ifdef OPENMMT8_EDITX
#define EDOPT_AT(f) ((__xdata uint8_t *)(0x05F0 + offsetof(edopt_t, f)))
#endif
typedef struct {
    uint8_t fn, page;
    __xdata uint8_t *p;
    uint8_t lo, hi;
} adj_t;
static const adj_t __code adj[] = {
    {K_TEMPO, 0xFF, CFG_AT(tempo), 20, 255},
    {K_TRANS, 0xFF, &v_trans, 0, 99},
    {K_QUANT, 0xFF, CFG_AT(quant_val), 0, 9},
    {K_FILTER, 6, CFG_AT(filt_chan), 0, 16},
    {K_FILTER, 0x80, CFG_AT(filt), 0, 1},
#ifdef OPENMMT8_EDITX
    {K_FILTER, 7, EDOPT_AT(cc), 0, 119},
    {K_FILTER, 8, EDOPT_AT(aud), 0, 1},
    {K_FILTER, 9, EDOPT_AT(grid), 0, 1},
    {K_FILTER, 10, EDOPT_AT(bars), 0, 1},
#endif
    {K_CLOCK, 0, CFG_AT(clock_src), 0, CLK_LAST},
    {K_CLOCK, 1, CFG_AT(clock_out), 0, 1},
    {K_CLOCK, 2, CFG_AT(auto_start), 0, 1},
    {K_CLICK, 0, CFG_AT(click_val), 0, 9},
    {K_CLICK, 1, CFG_AT(rec_click), 0, 1},
    {K_CLICK, 2, CFG_AT(play_click), 0, 1},
    {K_CLICK, 3, CFG_AT(countdown), 0, 99},
    {K_CHAN, 0xFF, 0, 0, 16},
    {0},
};

static __bit adj_up;
static void fn_adjust(void)
{
    const adj_t __code *a;
    xptr r;
    uint8_t n;

    if (fn == K_QUANT && cfg.mode_song) {
        __xdata uint8_t *p = shift_at();
        uint8_t s = *p;
        if (adj_up) {
            if (s != 48)
                ++s;
        } else if (s != 0xD0)
            --s;
        shift_store(s);
        ent_n = 0;
        return;
    }
    switch (fn) {
    case K_LENGTH:
        if (adj_up) {
            if (v_len >= 682)
                return;
            ++v_len;
        } else {
            if (v_len <= 1)
                return;
            --v_len;
        }
        break;
    case K_COPY:
        v_dst = wrap100(v_dst, adj_up);
        break;
#ifdef OPENMMT8_TAPE
    case K_TAPE:
        if (page == 3 || page == 4)
            tape_num = wrap100(tape_num, adj_up);
        break;
#endif
    default:
        for (a = adj; a->fn != fn || (a->page != 0xFF && a->page != page
                                        && (a->page != 0x80 || page >= 6)); ++a)
            if (!a->fn)
                return;
        r = a->page == 0x80 ? a->p + page : fn == K_CHAN ? cur_part() + P_CHAN + 7 - v_trk : a->p;
        n = clamp_step(*r, adj_up, a->lo, a->hi);
        if (n == *r)
            return;
        *r = n;
        if (fn == K_TEMPO)
            timer_set_tempo(n);
        else if (fn == K_CLOCK && page == 0)
            clock_apply();
        else if (fn == K_FILTER)
            scr_drawn = 0xFF;
    }
    ent_n = 0;
}

static void fn_digit(uint8_t d)
{
    switch (fn) {
    case K_TEMPO:
        if (entry(d, 3, 9, 255)) {
            cfg.tempo = ent_v < 20 ? 20 : (uint8_t)ent_v;
            timer_set_tempo(cfg.tempo);
        }
        break;
    case K_LENGTH:
        if (entry(d, 3, 9, 682)) {
            v_len = ent_v;
            keys_pass_extend(LENGTH_ENTRY_CYCLES);
        }
        break;
    case K_TRANS:
        if (entry99(d))
            v_trans = (uint8_t)ent_v;
        break;
    case K_COPY:
        if (entry99(d))
            v_dst = (uint8_t)ent_v;
        break;
    case K_CHAN:
        if (entry16(d))
            cur_part()[P_CHAN + 7 - v_trk] = (uint8_t)ent_v;
        break;
    case K_QUANT:
        if (!cfg.mode_song)
            cfg.quant_val = d ? d - 1 : 9;
        break;
    case K_FILTER:
        if (page == 6 && entry16(d))
            cfg.filt_chan = (uint8_t)ent_v;
#ifdef OPENMMT8_EDITX
        else if (page == 7 && entry(d, 3, 1, 119))
            edopt.cc = (uint8_t)ent_v;
#endif
        break;
    case K_CLOCK:
        if (!page && d >= 1 && d <= CLK_LAST + 1) {
            cfg.clock_src = d - 1;
            clock_apply();
        }
        break;
    case K_CLICK:
        if (page == 0)
            cfg.click_val = d ? d - 1 : 9;
        else if (page == 3 && entry99(d))
            cfg.countdown = (uint8_t)ent_v;
        break;
#ifdef OPENMMT8_TAPE
    case K_TAPE:
        if ((page == 3 || page == 4) && entry99(d))
            tape_num = (uint8_t)ent_v;
        break;
#endif
    }
}

static void fn_page(uint8_t up)
{
    switch (fn) {
    case K_LENGTH:
    case K_TRANS:
        page ^= 1;
        break;
    case K_ERASE:
    case K_COPY:
        if (!cfg.mode_song)
            page = ring(page, up, 23);
        break;
    case K_QUANT:
        cfg.quant_mode = ring(cfg.quant_mode, up, 4);
        break;
    case K_FILTER:
#ifdef OPENMMT8_EDITX
        page = ed_on ? 7 + ring(page - 7, up, 4) : ring(page, up, 7);
#else
        page = ring(page, up, 7);
#endif
        scr_drawn = 0xFF; /* page text changed; charge the redraw on the scan grid */
        break;
    case K_CLOCK:
        page = ring(page, up, 3);
        break;
    case K_CLICK:
        page = ring(page, up, 4);
        break;
    case K_TAPE:
        page = ring(page, up, TAPE_PAGES);
        break;
    }
}

static void fn_track(uint8_t t)
{
    switch (fn) {
    case K_REC:
        rec_manual = 1;
        rec_track = rec_track == t ? K_NONE : t;
        break;
    case K_CHAN:
        v_trk = t;
        ent_n = 0;
        break;
    case K_ERASE:
        if (!cfg.mode_song)
            e_mask |= bit_tab[t];
        e_done = 0;
        break;
    case K_COPY:
        if (cfg.mode_song)
            break;
        if (c_src == K_NONE) {
            c_src = t;
        } else {
            c_dst = t;
            fl_n = FLASH_COPY;
            fl_on = 0;
        }
        break;
    case K_TRANS:
    case K_QUANT:
        if (fn == K_QUANT && cfg.mode_song) {
            v_trk = t;
            t_mask = bit_tab[t];
            ent_n = 0;
            break;
        }
        if (!t_picked) {
            t_picked = 1;
            t_mask = 0;
        }
        t_mask ^= bit_tab[t];
        break;
    case K_MERGE:
        if (m1 == K_NONE)
            m1 = t;
        else
            m2 = t;
        break;
    }
}

static void fn_execute(void)
{
    xptr r;
#ifdef OPENMMT8_EDITX
    ed_undo_ok = 0;
#endif
    switch (fn) {
    case K_LENGTH:
        if (ent_n)
            break;
        if (v_len) {
            part_resize(page ? v_len | 0x8000 : v_len);
            show_msg(T_LENCHG);
        }
        break;
    case K_NAME:
        r = cfg.mode_song ? song_rec(cfg.song) + S_NAME : cur_part() + P_NAME;
        mem_move((uint16_t)r, (uint16_t)(lcd + 17), 14);
        nm_live = 0;
        show_msg(T_NEWPART + cfg.mode_song);
        break;
    case K_TAPE:
#ifdef OPENMMT8_TAPE
        if (page < 5) {
            if (page == 2)
                tape_heap_reset();
            tape_run = 1;
            tape_aborted = 0;
            tape_heard = 0;
            tape_decode_go = 0;
            tape_edges = 0;
            tape_pin = P3_2;
            tape_p3_ref = 0x04u;
            tape_wait = page != 0;
            if (tape_wait)
                KEY_ROWS = 0x7E;
            tape_expect = 0xff;
            if (page == 3)
                tape_expect = tape_num;
            else if (page == 4)
                tape_expect = (uint8_t)(tape_num + 100);
            /* Loading one song lights the song LED, as on the original. */
            if (page == 4) {
                cfg.mode_song = 1;
                ui_leds();
            }
            ui_dirty = 1;
            break;
        }
#endif
        if (page == TAPE_PAGES - 1) {
            dump_send();
            fn_poll = 2;
            ui_dirty = 1;
        }
        break;
    case K_TEMPO:
        if (cfg.mode_song) {
            r = song_rec(cfg.song);
            if (r)
                r[S_TEMPO] = cfg.tempo;
            show_msg(T_TEMPOSAVED);
        }
        break;
    case K_COPY:
        if (cfg.mode_song && edit_on) {
            if (ent_n)
                break;
            song_insert_step(cfg.song, e_step, v_dst);
            v_dst = song_part(++e_step);
            show_msg(T_STEPINS);
            break;
        }
        ent_n = 0;
        if (cfg.mode_song) {
            op.src = cfg.song;
            op.dst = v_dst;
            msg_copy(song_copy());
            break;
        }
        op.src = cfg.part;
        op.src_t = c_src;
        op.dst = v_dst;
        op.dst_t = c_dst != K_NONE ? c_dst : c_src;
        op.sel = page;
        msg_copy(part_copy());
        break;
    case K_ERASE:
        if (cfg.mode_song && edit_on) {
            song_erase_step(cfg.song, e_step);
            if (e_step && song_part(e_step) == 0xFF)
                --e_step;
            show_msg(T_STEPERASED);
            /* The erase page does not return while ERASE is still held. */
            fn = K_NONE;
            break;
        }
        if (cfg.mode_song) {
            song_erase(cfg.song);
        } else {
            op.src = cfg.part;
            op.mask = e_mask;
            op.sel = page;
            part_erase();
            e_done = e_done ? 2 : 1;
        }
        show_msg(T_ERASEDONE);
        if (!cfg.mode_song && !e_mask && part_open) {
            part_open = 0;
            msg_ticks += page ? ERASE_OPEN_SEL_PASSES : ERASE_OPEN_PASSES;
            msg_rec = 0;
        }
        break;
    case K_MERGE:
        if (msg) {
            show_msg(T_MERGED);
            break;
        }
        if (m1 != K_NONE && m2 != K_NONE && m1 != m2) {
            op.src = cfg.part;
            op.src_t = m1;
            op.dst_t = m2;
            show_msg(part_merge() ? T_MERGED : T_FULL);
            m1 = m2 = K_NONE;
        }
        break;
    case K_TRANS:
        ent_n = 0;
        op.src = cfg.part;
        op.mask = t_mask;
        op.sel = page ? (uint8_t)-v_trans : v_trans;
        part_transpose();
        show_msg(T_TRDONE);
        msg_rec = 0;
        break;
    case K_QUANT:
        if (cfg.mode_song)
            break;
        if (msg) {
            show_msg(T_QDONE);
            break;
        }
        {
            uint16_t t0 = cycles_now();
            show_msg(T_QDONE);
            ui_render();
            lcd_flush();
            op.src = cfg.part;
            op.mask = t_mask;
            op.sel = cfg.quant_val;
            op.dst = cfg.quant_mode;
            op.mask = part_quantize();
            cycles_wait(t0, QUANT_CYCLES);
            if (op.mask == 2)
                cycles_wait(t0 + QUANT_CYCLES, QUANT_MOVE_CYCLES);
            show_msg(op.mask ? T_QDONE : T_FULL);
            keys_restart();
        }
        break;
    }
}

/* NAME edits the name as shown on line 2 and RECORD stores the shown text; once a message
 * or a redraw replaces the line, editing continues from the text then shown. */
static void name_key(uint8_t k, uint8_t d)
{
    __xdata char *c = lcd + 17 + page;
    if (k == K_PGUP) {
        if (++page == 14)
            page = 0;
        return;
    }
    if (k == K_PGDN) {
        if (!page--)
            page = 13;
        return;
    }
    if (d != 0xFF)
        *c = '0' + d;
    else if (k == K_PLUS) {
        if (*c != 0x7F)
            ++*c;
    } else if (k == K_MINUS) {
        if (*c != ' ')
            --*c;
    } else
        return;
    nm_live = 1;
}

static void fn_key(uint8_t k)
{
    uint8_t d = key_digit(k);
    if (ed_on && (fn == K_ERASE || fn == K_COPY)) {
        if (k == K_REC)
            ed_execute();
#ifdef OPENMMT8_EDITX
        else if (k == K_PGUP || k == K_PGDN) {
            if (fn == K_COPY) {
                ed_ins = ring(ed_ins, k == K_PGUP, 5);
            } else if (ed_undo_ok) {
                ed_look(0);
                if (!ed_end())
                    ed_ins ^= 1;
            }
        }
#else
        else if (fn == K_COPY && (k == K_PGUP || k == K_PGDN))
            ed_ins = ring(ed_ins, k == K_PGUP, 5);
#endif
        return;
    }
    if (fn == K_REC && k == K_LENGTH) {
        fn = K_MEMORY;
        return;
    }
    if (k == K_REC) {
        fn_execute();
        return;
    }
    if (fn == K_NAME) {
        name_key(k, d);
        return;
    }
    if (k == K_PLUS || k == K_MINUS) {
        adj_up = k == K_PLUS;
        fn_adjust();
        return;
    }
    if (d != 0xFF) {
        fn_digit(d);
        return;
    }
    if (k == K_PGUP || k == K_PGDN) {
        ent_n = 0;
        fn_page(k == K_PGUP);
        return;
    }
    if (k >= K_TRK1 && k < K_TRK1 + 8)
        fn_track(k - K_TRK1);
}

/* ---- keys outside function screens ---- */

static void select_number(uint8_t n)
{
    if (tp_state != TP_STOP && !cfg.mode_song) {
        next_part = n;
        ent_n = 0;
        return;
    }
    if (cfg.mode_song) {
        song_select(n);
    } else {
        part_select(n);
        rec_manual = 0;
    }
}

/* Set the part of the current song edit step (appending a step past the end). */
static void step_set(uint8_t part)
{
    xptr s;
    if (song_part(e_step) == 0xFF) {
        song_insert_step(cfg.song, e_step, part);
        return;
    }
    s = song_rec(cfg.song);
    s[S_STEPS + 2 * e_step] = part;
}

static void song_edit_key(uint8_t k)
{
    uint8_t d = key_digit(k), cur = song_part(e_step);
    xptr s;

    if (d != 0xFF) {
        if (entry99(d))
            step_set((uint8_t)ent_v);
        return;
    }
    switch (k) {
    case K_PLUS:
    case K_MINUS:
        ent_n = 0;
        step_set(cur == 0xFF ? 0 : wrap100(cur, k == K_PLUS));
        break;
    case K_FF:
        ent_n = 0;
        if (cur != 0xFF)
            ++e_step;
        break;
    case K_REW:
        ent_n = 0;
        if (e_step)
            --e_step;
        break;
    default:
        if (k >= K_TRK1 && k < K_TRK1 + 8 && cur != 0xFF) {
            s = song_rec(cfg.song);
            s[S_STEPS + 2 * e_step + 1] ^= bit_tab[k - K_TRK1];
            mute = (uint8_t)~s[S_STEPS + 2 * e_step + 1];
        }
        break;
    }
}

/* *p = !*p. The settings are battery-backed: any nonzero value left in memory reads as on,
 * and flipping only bit 0 of such a value would never turn it off. */
static void flag_toggle(__xdata uint8_t *p) __naked
{
    p;
    __asm
        movx    a, @dptr
        cjne    a, #1, 00001$
00001$:
        clr     a
        rlc     a
        movx    @dptr, a
        ret
    __endasm;
}

static void home_key(uint8_t k)
{
    uint8_t d = key_digit(k);
    uint8_t cur;

    if (cfg.mode_song && edit_on && tp_state == TP_STOP &&
        (d != 0xFF || k == K_PLUS || k == K_MINUS || k == K_FF || k == K_REW ||
         (k >= K_TRK1 && k < K_TRK1 + 8))) {
        song_edit_key(k);
        return;
    }

    if (d != 0xFF) {
        uint8_t w = tp_state != TP_STOP && !cfg.mode_song ? 1 : 2;
        if (entry(d, w, 9, 99))
            select_number((uint8_t)ent_v);
        return;
    }
    switch (k) {
    case K_PLUS:
    case K_MINUS:
        ent_n = 0;
        if (tp_state != TP_STOP && !cfg.mode_song)
            cur = next_part != K_NONE ? next_part : cfg.part;
        else
            cur = cfg.mode_song ? cfg.song : cfg.part;
        select_number(wrap100(cur, k == K_PLUS));
        break;
    case K_SONG:
        if (tp_state != TP_STOP)
            break;
        if (!cfg.mode_song)
            part_release();
        cfg.mode_song = 1;
        song_select(cfg.song);
        edit_on = 0;
        ed_on = 0;
        ent_n = 0;
        break;
    case K_PART:
        if (tp_state != TP_STOP)
            break;
        if (cfg.mode_song) {
            song_release();
            mute = 0;
        }
        cfg.mode_song = 0;
        part_select(cfg.part);
        edit_on = 0;
        ed_on = 0;
        ent_n = 0;
        break;
    case K_EDIT:
        ent_n = 0;
        if (cfg.mode_song) {
            edit_on ^= 1;
            e_step = 0;
        } else if (cur_len()) {
            ed_on = 1;
#ifdef OPENMMT8_EDITX
            ed_undo_ok = 0;
            ed_fresh = 1;
            cc_ref = st_rec = 0;
            st_pos = 0;
            ed_look(0);
            st_snap();
#endif
        }
        break;
    case K_PLAY:
        ent_n = 0;
        next_part = K_NONE;
        seq_play();
        break;
    case K_STOP:
        if (tp_state != TP_STOP)
            seq_stop();
        else if (rec_armed)
            rec_armed = 0;
        else
            seq_continue();
        break;
    case K_FF:
    case K_REW:
        if ((tp_state == TP_STOP && !cfg.mode_song) || (tp_state == TP_PLAY && !tp_recording))
            loc_start();
        break;
    case K_LOOP:
        flag_toggle(&cfg.loop);
        break;
    case K_ECHO:
        flag_toggle(&cfg.echo);
        break;
    default:
        if (k >= K_TRK1 && k < K_TRK1 + 8)
            mute ^= bit_tab[k - K_TRK1];
        break;
    }
}

void ui_key_down(uint8_t k)
{
#ifdef OPENMMT8_TAPE
    if (tape_run)
        return;
#endif
    if (k == K_FOOT) {
        if (tp_state == TP_COUNTIN)
            return;
        k = tp_state == TP_STOP ? K_PLAY : K_STOP;
    }
    /* The transport keys act from any screen. */
    if (k == K_PLAY || k == K_STOP) {
        msg = 0;
        if (fn != K_NONE)
            fn_close();
    }
    /* While a message shows, only RECORD acts (repeating the operation). */
    if (msg) {
        if (k == K_REC && msg_rec && fn != K_NONE)
            fn_execute();
        return;
    }
    ui_dirty = 1;
    if (loc_on) {
        loc_key(k);
        return;
    }
    if (fn != K_NONE) {
        fn_key(k);
        return;
    }
    if (ed_on && ed_key(k))
        return;
    if (k == K_REC && tp_recording) {
        seq_punch_out();
        return;
    }
    static const uint8_t __code fn_keys[] = {
        K_ERASE, K_TRANS, K_COPY, K_REC, K_TEMPO, K_CLICK, K_CHAN, K_TAPE, K_CLOCK, K_MERGE,
        K_FILTER, K_QUANT, K_LENGTH, K_NAME, 0
    };
    uint8_t i;
    for (i = 0; fn_keys[i]; ++i)
        if (fn_keys[i] == k) {
            ent_n = 0;
            fn_open(k);
            return;
        }
    home_key(k);
}

void ui_key_up(uint8_t k)
{
#ifdef OPENMMT8_TAPE
    if (tape_run)
        return;
#endif
    if (loc_on && (k == K_FF || k == K_REW)) {
        if (!key_held(k ^ 1)) {
            loc_end();
            ui_dirty = 1;
        }
        return;
    }
#ifdef OPENMMT8_EDITX
    /* Step entry: RECORD up moves to the next step (with no notes played, a rest). */
    if (k == K_REC && st_rec && ed_on) {
        st_rec = 0;
        st_next();
        ed_fresh = 1;
        ui_dirty = 1;
        return;
    }
#endif
    /* The function page notices its key is up by polling it: not while a message or the
     * memory display covers the page, but a pass after the page shows again. */
    if (k == fn || (fn == K_MEMORY && (k == K_REC || k == K_LENGTH))) {
        if (fn == K_MEMORY) {
            if (k == K_LENGTH) {
                fn = K_REC;
                /* RECSEL stays up for several blocks after MEMORY before home. */
                fn_poll = 8;
                ui_dirty = 1;
            }
            return;
        }
        if (msg || fn_poll)
            return;
        fn_close();
        ui_dirty = 1;
    }
}

#ifdef OPENMMT8_TAPE
void tape_lcd_part(uint8_t idx)
{
    uint8_t n, pos, start;
    uint16_t d;
    if (idx >= 100u) {
        lcd[16] = 'S';
        lcd[17] = 'O';
        lcd[18] = 'N';
        lcd[19] = 'G';
        lcd[20] = ':';
        lcd[21] = ' ';
        n = (uint8_t)(idx - 100u);
        start = 16;
    } else {
        n = idx;
        start = 22;
    }
    lcd[22] = (char)('0' + n / 10u);
    lcd[23] = (char)('0' + urem(n, 10));
    for (pos = start; pos < 24; ++pos)
        lcd_hw_cell(pos);
    for (d = 180; d; --d)
        ;
}

#include "tape_ui.inc"
#endif
