/* Panel: the home screen, settings pages (shown while their function key is held, home
 * again on release; PAGE up/down picks the field, +/- or the keypad sets it), step edit
 * (EDIT toggles it), LEDs. */
#include "jam.h"
#include <stddef.h>

__bit ui_dirty;
__xdata uint8_t focus;
static __xdata uint8_t page, fld, held_trk, trk_used, rec_used, merge_used;
static __xdata uint8_t quant_used, loop_used, mute_trk, solo_trk, tap_trk, stop_plain;
static __xdata uint8_t ent_n, ent_t, msg_t, edit_pg, edit_sel, chain_ed, part_sel, copy_dst;
static __xdata uint8_t led_trk, led_mode, tap_n;
static __xdata uint16_t ent_v, tap_last, trk_at[NLANES];
#define HOLD_SCANS 75u /* a track held longer than 300 ms focuses without muting */
static __xdata char msgbuf[16];
#define NONE 0xFFu
#define MSG_SCANS 250u
#define ENT_SCANS 250u

enum {
    P_HOME, P_GEN, P_LEN, P_CHAN, P_HARM, P_QUANT, P_LOOP, P_CLOCK, P_CLICK, P_TEMPO, P_KNOB,
    P_SONG, P_PART, P_COPY, P_ERASE, P_EDIT, P_N
};

/* ---- lists (8 characters each) ---- */
static const char __code lists[] =
    "DRUM    BASS    CHORD   LEAD    "                                                 /* 0 role */
    "1/32    1/16T   1/16    1/8T    1/8     1/4     "                                 /* 4 step */
    "MAJOR   MINOR   DORIAN  PHRYGIANMIXOLYD HARM MINMIN PENTMAJ PENTBLUES   CHROMA  " /* 10 */
    "VAMP    i VI IIII V vi  ii V I  i iv v  BLUES 12i VII VITWO CHRD"                 /* 20 prog */
    "STEP    BEAT    BAR     "                                                         /* 28 */
    "OFF     ON      "                                                                 /* 31 */
    "INTERNALMIDI    "                                                                 /* 33 */
    "KEY     CHORD   "                                                                 /* 35 */
    "ALWAYS  FILL    NOT FILL1 OF 2  2 OF 2  1 OF 4  4 OF 4  FIRST   "                 /* 37 cond */
    "HOUSE   TECHNO  BREAKS  BOOMBAP GARAGE  DEMBOW  TRAP    ELECTRO EUCLID  "         /* 45 */
    "ROOT 8  OFFBEAT OCTAVE  ACID    WALK    EUCLID  "                                 /* 54 */
    "STAB    PAD     STRUM   EUCLID  "                                                 /* 60 */
    "ARP UP  ARP DOWNARP UPDNARP RANDCONVERGECHORD   TURING  EUCLID  ";                /* 64 */
enum { L_ROLE = 0, L_DIV = 4, L_SCALE = 10, L_PROG = 20, L_QUANT = 28, L_ONOFF = 31, L_SRC = 33,
       L_CMODE = 35, L_COND = 37, L_DSTY = 45 };
static const uint8_t __code style_list[NROLES] = {45, 54, 60, 64};
static const char __code notes[] = "C C#D D#E F F#G G#A A#B ";

/* ---- fields ---- */
enum { FK_LANE, FK_FLAG, FK_CFG, FK_TEMPO, FK_FINE, FK_BARS, FK_CHPOS, FK_CHSC, FK_CHRP, FK_PART, FK_CDST, FK_GTR,
       FK_ACT };
enum { FM_NUM, FM_NUM1, FM_OFF, FM_NOTE, FM_KEY, FM_SIGN, FM_STYLE, FM_PCT, FM_END, FM_ACT, FM_LIST };
enum { A_NONE, A_REGEN, A_LEN, A_ROT, A_ROLE, A_HARM, A_SWING, A_SYNC, A_PART, A_TEMPO };
enum { ACT_DUMP, ACT_ERASE };
typedef struct {
    char lab[8];
    uint8_t kind, off, min, max, fmt, act;
} field_t;
#define CF(m) offsetof(cfg_t, m)
#define LIST(l) (FM_LIST + (l))

static const field_t __code fields[] = {
    /* 0 GEN */
    {"STYLE   ", FK_LANE, H_STYLE, 0, 8, FM_STYLE, A_REGEN},
    {"SEED    ", FK_LANE, H_SEED, 0, 99, FM_NUM, A_REGEN},
    {"DENSITY ", FK_LANE, H_DENS, 0, 9, FM_NUM, A_REGEN},
    {"EVOLVE  ", FK_LANE, H_EVOL, 0, 99, FM_NUM, A_NONE},
    {"EUCLID K", FK_LANE, H_EK, 0, 64, FM_NUM, A_REGEN},
    {"EUCLID N", FK_LANE, H_EN, 1, 64, FM_NUM, A_REGEN},
    {"EUC ROT ", FK_LANE, H_EROT, 0, 63, FM_NUM, A_REGEN},
    {"VELOCITY", FK_LANE, H_VEL, 1, 127, FM_NUM, A_NONE},
    {"HUMANIZE", FK_LANE, H_HUM, 0, 9, FM_NUM, A_NONE},
    /* 9 LEN */
    {"LENGTH  ", FK_LANE, H_LEN, 1, 64, FM_NUM, A_LEN},
    {"STEP    ", FK_LANE, H_DIV, 0, DIV_N - 1, LIST(L_DIV), A_NONE},
    {"GATE    ", FK_LANE, H_GATE, 1, 99, FM_PCT, A_NONE},
    {"ROTATE  ", FK_LANE, H_ROT, 0, 63, FM_NUM, A_ROT},
    /* 13 CHAN */
    {"ROLE    ", FK_LANE, H_ROLE, 0, NROLES - 1, LIST(L_ROLE), A_ROLE},
    {"CHANNEL ", FK_LANE, H_CHAN, 0, 15, FM_NUM1, A_NONE},
    {"NOTE    ", FK_LANE, H_NOTE, 0, 127, FM_NOTE, A_REGEN},
    {"VOICES  ", FK_LANE, H_VOICES, 3, 4, FM_NUM, A_NONE},
    {"SEVENTHS", FK_FLAG, F_SEV, 0, 1, LIST(L_ONOFF), A_NONE},
    {"HARMONY ", FK_FLAG, F_FOLLOW, 0, 1, LIST(L_ONOFF), A_NONE},
    {"TRANSP  ", FK_LANE, H_TRANS, 0, 48, FM_SIGN, A_NONE},
    {"SPREAD  ", FK_FLAG, F_OPEN, 0, 1, LIST(L_ONOFF), A_NONE},
    {"MOD CC  ", FK_LANE, H_MCC, 0, 119, FM_OFF, A_NONE},
    /* 22 HARM */
    {"KEY     ", FK_CFG, CF(key), 0, 11, FM_KEY, A_HARM},
    {"SCALE   ", FK_CFG, CF(scale), 0, NSCALES - 1, LIST(L_SCALE), A_HARM},
    {"PROGRESS", FK_CFG, CF(prog), 0, NPROGS - 1, LIST(L_PROG), A_HARM},
    {"CTRL CH ", FK_CFG, CF(ctrl_ch), 0, 16, FM_OFF, A_NONE},
    {"CTRL BY ", FK_CFG, CF(ctrl_mode), 0, 1, LIST(L_CMODE), A_HARM},
    {"TRANSPOS", FK_GTR, 0, 0, 48, FM_SIGN, A_NONE},
    /* 28 QUANT */
    {"LAUNCH  ", FK_CFG, CF(quant), 0, 2, LIST(L_QUANT), A_NONE},
    {"SWING   ", FK_CFG, CF(swing), 50, 75, FM_PCT, A_SWING},
    /* 30 LOOP */
    {"BARS    ", FK_BARS, 0, 1, 16, FM_NUM, A_NONE},
    /* 31 CLOCK */
    {"SOURCE  ", FK_CFG, CF(sync), 0, 1, LIST(L_SRC), A_SYNC},
    {"CLK OUT ", FK_CFG, CF(clk_out), 0, 1, LIST(L_ONOFF), A_NONE},
    {"DUMP    ", FK_ACT, ACT_DUMP, 0, 0, FM_ACT, A_NONE},
    /* 34 CLICK */
    {"CLICK   ", FK_CFG, CF(click), 0, 1, LIST(L_ONOFF), A_NONE},
    /* 35 TEMPO */
    {"TEMPO   ", FK_TEMPO, 0, 40, 250, FM_NUM, A_TEMPO},
    {"FINE    ", FK_FINE, 0, 0, 9, FM_NUM, A_TEMPO},
    /* 37 KNOB */
    {"KNOB CC ", FK_CFG, CF(knob), 0, 119, FM_OFF, A_NONE},
    /* 38 SONG */
    {"CHAIN   ", FK_CFG, CF(chain_on), 0, 1, LIST(L_ONOFF), A_NONE},
    {"POSITION", FK_CHPOS, 0, 0, CHAIN_LEN - 1, FM_NUM1, A_NONE},
    {"SCENE   ", FK_CHSC, 0, 0, NSCENES, FM_END, A_NONE},
    {"REPEATS ", FK_CHRP, 0, 1, 16, FM_NUM, A_NONE},
    /* 42 PART */
    {"SCENE   ", FK_PART, 0, 0, NSCENES - 1, FM_NUM1, A_PART},
    /* 43 COPY */
    {"TO SCENE", FK_CDST, 0, 0, NSCENES - 1, FM_NUM1, A_NONE},
    /* 44 ERASE */
    {"SCENE   ", FK_ACT, ACT_ERASE, 0, 0, FM_ACT, A_NONE},
};

typedef struct {
    char title[8];
    uint8_t first, count, lane;
} page_t;
static const page_t __code pages[P_N] = {
    {"        ", 0, 0, 0},  {"GEN     ", 0, 9, 1},  {"LENGTH  ", 9, 4, 1},  {"CHANNEL ", 13, 9, 1},
    {"HARMONY ", 22, 6, 0}, {"QUANTIZE", 28, 2, 0}, {"LOOP    ", 30, 1, 0}, {"CLOCK   ", 31, 3, 0},
    {"CLICK   ", 34, 1, 0}, {"TEMPO   ", 35, 2, 0}, {"KNOB    ", 37, 1, 0}, {"SONG    ", 38, 4, 0},
    {"SCENE   ", 42, 1, 0}, {"COPY    ", 43, 1, 1}, {"ERASE   ", 44, 1, 1}, {"EDIT    ", 0, 9, 1},
};
static const uint8_t __code page_keys[] = {
    K_QUANT, P_QUANT, K_LOOP, P_LOOP, K_TAPE, P_GEN, K_LENGTH, P_LEN, K_CHAN, P_CHAN, K_TRANS, P_HARM,
    K_CLOCK, P_CLOCK, K_CLICK, P_CLICK, K_TEMPO, P_TEMPO, K_NAME, P_KNOB, K_SONG, P_SONG, K_PART, P_PART,
    K_COPY, P_COPY, K_ERASE, P_ERASE, K_EDIT, P_EDIT, 0xFF};

/* Step edit fields. */
enum { E_VEL, E_NOTE, E_OCT, E_RAT, E_PROB, E_COND, E_DELAY, E_ACC, E_TIE, E_N };
static const char __code edit_labels[] =
    "VELOCITYNOTE    OCTAVE  RATCHET PROB    COND    DELAY   ACCENT  TIE     ";

static const field_t __code *__xdata cur_f;

/* Keypad digit of key k (1-5, 6-9 and 0 on two rows), NONE when not a digit key. */
static uint8_t key_digit_of(uint8_t k)
{
    if ((uint8_t)(k - 33) < 5)
        return (uint8_t)(k - 32);
    if ((uint8_t)(k - 25) < 4)
        return (uint8_t)(k - 19);
    return k == 29 ? 0 : NONE;
}

static __xdata uint8_t *fh(void)
{
    return HDR(focus);
}

static uint8_t chain_at(uint8_t i)
{
    return XMEM(CHAIN_BASE)[(uint8_t)(chain_ed * 2 + i)];
}

/* Transpose of the first melodic lane: what the HARMONY page shows for all of them. */
static int8_t gtrans(void)
{
    uint8_t l;
    for (l = 0; l < NLANES; ++l)
        if (HDR(l)[H_ROLE] != R_DRUM)
            return (int8_t)HDR(l)[H_TRANS];
    return 0;
}

/* Every melodic lane moves d semitones, within two octaves. */
static void transpose(int8_t d)
{
    static __xdata uint8_t l;
    static __xdata int8_t t;
    for (l = 0; l < NLANES; ++l) {
        if (HDR(l)[H_ROLE] == R_DRUM)
            continue;
        t = (int8_t)(HDR(l)[H_TRANS] + d);
        if (t < -24)
            t = -24;
        if (t > 24)
            t = 24;
        HDR(l)[H_TRANS] = (uint8_t)t;
    }
}

static uint8_t f_max(void)
{
    if (cur_f->fmt == FM_STYLE)
        return (uint8_t)(style_count[fh()[H_ROLE] & 3] - 1);
    return cur_f->max;
}

static uint8_t get_val(void)
{
    uint8_t off = cur_f->off;
    switch (cur_f->kind) {
    case FK_LANE:
        if (cur_f->fmt == FM_SIGN)
            return (uint8_t)((int8_t)fh()[off] + 24);
        return fh()[off];
    case FK_FLAG:
        return (fh()[H_FLAGS] & off) != 0;
    case FK_CFG:
        return ((__xdata uint8_t *)&cfg)[off];
    case FK_TEMPO:
        return (uint8_t)(tempo_get() / 10);
    case FK_FINE:
        return (uint8_t)(tempo_get() % 10);
    case FK_BARS:
        return cfg.bars[cfg.scene];
    case FK_CHPOS:
        return chain_ed;
    case FK_CHSC:
        return chain_at(0);
    case FK_CHRP:
        return chain_at(1);
    case FK_PART:
        return part_sel;
    case FK_CDST:
        return copy_dst;
    case FK_GTR:
        return (uint8_t)(gtrans() + 24);
    }
    return 0;
}

static void lane_regen(void)
{
    fh()[H_FLAGS] &= (uint8_t)~F_CAPT;
    gen_request(focus);
}

static void set_val(int16_t v)
{
    static __xdata uint8_t *__xdata h;
    static __xdata uint8_t off, mx;
    h = fh();
    off = cur_f->off;
    mx = f_max();
    if (cur_f->kind == FK_ACT)
        return;
    if (v < cur_f->min)
        v = cur_f->min;
    if (v > mx)
        v = mx;
    switch (cur_f->kind) {
    case FK_LANE:
        h[off] = cur_f->fmt == FM_SIGN ? (uint8_t)(int8_t)(v - 24) : (uint8_t)v;
        break;
    case FK_FLAG:
        if (v)
            h[H_FLAGS] |= off;
        else
            h[H_FLAGS] &= (uint8_t)~off;
        break;
    case FK_CFG:
        ((__xdata uint8_t *)&cfg)[off] = (uint8_t)v;
        break;
    case FK_TEMPO:
        tempo_set((uint16_t)v * 10 + tempo_get() % 10);
        break;
    case FK_FINE:
        tempo_set(tempo_get() / 10 * 10 + (uint16_t)v);
        break;
    case FK_BARS:
        cfg.bars[cfg.scene] = (uint8_t)v;
        break;
    case FK_CHPOS:
        chain_ed = (uint8_t)v;
        break;
    case FK_CHSC:
        XMEM(CHAIN_BASE)[(uint8_t)(chain_ed * 2)] = (uint8_t)v;
        break;
    case FK_CHRP:
        XMEM(CHAIN_BASE)[(uint8_t)(chain_ed * 2 + 1)] = (uint8_t)v;
        break;
    case FK_PART:
        part_sel = (uint8_t)v;
        break;
    case FK_CDST:
        copy_dst = (uint8_t)v;
        break;
    case FK_GTR:
        transpose((int8_t)((int8_t)(v - 24) - gtrans()));
        break;
    }
    switch (cur_f->act) {
    case A_REGEN:
        lane_regen();
        break;
    case A_LEN:
        if (h[H_ROT] >= h[H_LEN])
            h[H_ROT] = 0;
        if (!(h[H_FLAGS] & F_CAPT))
            gen_request(focus);
        break;
    case A_ROT:
        if (h[H_ROT] >= h[H_LEN])
            h[H_ROT] = (uint8_t)(h[H_LEN] - 1);
        break;
    case A_ROLE:
        {
            static const uint8_t __code role_note[NROLES] = {36, 36, 60, 72};
            uint8_t r = h[H_ROLE] & 3;
            lane_defaults(h, r, role_note[r], 0);
            lr[focus].nv = 0;
            gen_request(focus);
        }
        break;
    case A_HARM:
        harm_init();
        break;
    case A_SWING:
        engine_swing();
        break;
    case A_SYNC:
        if (playing)
            engine_stop();
        break;
    case A_PART:
        engine_queue_scene(part_sel);
        break;
    }
    ui_dirty = 1;
}

/* A message for MSG_SCANS: text of up to 8 characters when a track (T1-T8 at 9) is shown,
 * a number right-aligned at 15 unless v is 0xFFFF. */
void ui_msg(const char __code *s, uint8_t lane, uint16_t v)
{
    uint8_t i;
    for (i = 0; i < 16; ++i)
        msgbuf[i] = ' ';
    for (i = 0; i < 16 && s[i]; ++i)
        msgbuf[i] = s[i];
    if (lane < NLANES) {
        msgbuf[9] = 'T';
        msgbuf[10] = (char)('1' + lane);
    }
    if (v != 0xFFFF) {
        msgbuf[15] = (char)('0' + v % 10);
        if (v >= 10)
            msgbuf[14] = (char)('0' + v / 10 % 10);
        if (v >= 100)
            msgbuf[13] = (char)('0' + v / 100 % 10);
    }
    msg_t = MSG_SCANS;
    ui_dirty = 1;
}

/* ---- step edit ---- */

static __xdata uint8_t *edit_step(void)
{
    return GRID(focus) + (uint16_t)edit_sel * 4;
}

static uint8_t edit_get(void)
{
    __xdata uint8_t *g = edit_step();
    switch (fld) {
    case E_VEL:
        return g[2];
    case E_NOTE:
        return fh()[H_ROLE] == R_DRUM ? g[1] : g[1] & 31;
    case E_OCT:
        return g[1] >> 5;
    case E_RAT:
        return (uint8_t)(((g[0] & S_RAT) >> S_RAT_SHIFT) + 1);
    case E_PROB:
        return g[3] >> 4;
    case E_COND:
        return g[0] >> S_COND_SHIFT;
    case E_DELAY:
        return g[3] & 15;
    case E_ACC:
        return (g[0] & S_ACC) != 0;
    default:
        return (g[0] & S_TIE) != 0;
    }
}

static void edit_set(int16_t v)
{
    static const uint8_t __code lo[E_N] = {0, 0, 0, 1, 0, 0, 0, 0, 0};
    static const uint8_t __code hi[E_N] = {127, 127, 7, 4, 15, 7, 15, 1, 1};
    static __xdata uint8_t *__xdata g;
    static __xdata uint8_t m;
    g = edit_step();
    m = hi[fld];
    if (fld == E_NOTE && fh()[H_ROLE] != R_DRUM)
        m = 31;
    if (v < lo[fld])
        v = lo[fld];
    if (v > m)
        v = m;
    switch (fld) {
    case E_VEL:
        g[2] = (uint8_t)v;
        break;
    case E_NOTE:
        g[1] = fh()[H_ROLE] == R_DRUM ? (uint8_t)v : (uint8_t)((g[1] & 0xE0) | v);
        break;
    case E_OCT:
        g[1] = (uint8_t)((g[1] & 31) | (v << 5));
        break;
    case E_RAT:
        g[0] = (uint8_t)((g[0] & ~S_RAT) | ((v - 1) << S_RAT_SHIFT));
        break;
    case E_PROB:
        g[3] = (uint8_t)((g[3] & 15) | (v << 4));
        break;
    case E_COND:
        g[0] = (uint8_t)((g[0] & 0x1F) | (v << S_COND_SHIFT));
        break;
    case E_DELAY:
        g[3] = (uint8_t)((g[3] & 0xF0) | v);
        break;
    case E_ACC:
        g[0] = v ? g[0] | S_ACC : g[0] & (uint8_t)~S_ACC;
        break;
    default:
        g[0] = v ? g[0] | S_TIE : g[0] & (uint8_t)~S_TIE;
        break;
    }
    fh()[H_FLAGS] |= F_CAPT;
    ui_dirty = 1;
}

/* Sets field f of the selected step, leaving the PAGE selection alone. */
static void edit_put(uint8_t f, int16_t v)
{
    uint8_t keep = fld;
    fld = f;
    edit_set(v);
    fld = keep;
}

static void edit_toggle(uint8_t t)
{
    uint8_t idx = (uint8_t)(edit_pg * 8 + t);
    __xdata uint8_t *g;
    if (idx >= fh()[H_LEN])
        return;
    edit_sel = idx;
    g = edit_step();
    g[0] ^= S_ON;
    if ((g[0] & S_ON) && !(g[3] & 0xF0))
        g[3] |= 0xF0;
    fh()[H_FLAGS] |= F_CAPT;
    gen_ready &= (uint8_t)~(1 << focus);
    ui_dirty = 1;
}

/* ---- keys ---- */

static uint8_t is_page_key(uint8_t k)
{
    uint8_t i;
    for (i = 0; page_keys[i] != 0xFF; i += 2)
        if (page_keys[i] == k)
            return 1;
    return 0;
}

static void ent_commit(void);

/* Back to the home screen; a number being typed is set first. */
static void live_view(void)
{
    if (ent_n)
        ent_commit();
    page = P_HOME;
    fld = 0;
    ui_dirty = 1;
}

/* STOP plus a function key leaves the page and does not start or stop playback.
 * A function key already down counts, so either key can be pressed first. */
static void exit_combo(void)
{
    stop_plain = 0;
    if (key_held(K_QUANT))
        quant_used = 1;
    if (key_held(K_LOOP))
        loop_used = 1;
    if (page != P_HOME)
        live_view();
}

static uint8_t page_key_held(void)
{
    uint8_t i;
    for (i = 0; page_keys[i] != 0xFF; i += 2)
        if (key_held(page_keys[i]))
            return 1;
    return 0;
}

/* EDIT toggles step edit. Every other function key shows its page while held. */
static void open_page(uint8_t p)
{
    page = p == P_EDIT && page == P_EDIT ? P_HOME : p;
    fld = 0;
    ent_n = 0;
    if (page == P_EDIT) {
        edit_pg = 0;
        edit_sel = 0;
    } else if (page == P_PART) {
        part_sel = cfg.scene;
    } else if (page == P_COPY) {
        copy_dst = cfg.scene;
    }
    ui_dirty = 1;
}

static void select_field(void)
{
    cur_f = &fields[pages[page].first + fld];
}

static void bump(int8_t d)
{
    static __xdata uint8_t *__xdata h;
    if (key_held(K_MERGE)) {
        merge_used = 1;
        if (d > 0)
            lane_mutate(focus);
        else
            lane_unmutate(focus);
        ui_msg(d > 0 ? "MUTATE" : "UNDO", focus, HDR(focus)[H_SEED]);
        return;
    }
    if (page == P_EDIT && held_trk != NONE)
        trk_used = 1;
    if (page == P_HOME) {
        if (held_trk != NONE) {
            tap_trk = 0;
            h = HDR(held_trk);
            if (h[H_STYLE] == style_count[h[H_ROLE] & 3] - 1) {
                if (d > 0 ? h[H_EK] < h[H_EN] : h[H_EK] > 0) {
                    h[H_EK] += d;
                    h[H_FLAGS] &= (uint8_t)~F_CAPT;
                    gen_request(held_trk);
                }
                ui_msg("EUCLID K", held_trk, h[H_EK]);
                return;
            }
            if ((int8_t)h[H_DENS] + d >= 0 && h[H_DENS] + d <= 9) {
                h[H_DENS] += d;
                h[H_FLAGS] &= (uint8_t)~F_CAPT;
                gen_request(held_trk);
            }
            ui_msg("DENSITY", held_trk, h[H_DENS]);
        } else if (!cfg.sync) {
            tempo_set((uint16_t)(tempo_get() + d * 10));
            ui_msg("TEMPO", 0xFF, tempo_get() / 10);
        }
        return;
    }
    if (page == P_EDIT) {
        edit_set((int16_t)edit_get() + d);
        return;
    }
    select_field();
    set_val((int16_t)get_val() + d);
}

static void rotate(int8_t d)
{
    static __xdata uint8_t *__xdata h;
    static __xdata uint8_t len;
    h = fh();
    len = h[H_LEN];
    if (page == P_EDIT) {
        if (d > 0 && (uint8_t)(edit_pg + 1) * 8 < len)
            ++edit_pg;
        else if (d < 0 && edit_pg)
            --edit_pg;
        ui_dirty = 1;
        return;
    }
    h[H_ROT] = d > 0 ? (uint8_t)(h[H_ROT] + 1 >= len ? 0 : h[H_ROT] + 1)
                     : (uint8_t)(h[H_ROT] ? h[H_ROT] - 1 : len - 1);
    ui_msg("ROTATE", focus, h[H_ROT]);
}

static void digit(uint8_t d)
{
    uint16_t lim;
    if (page == P_HOME) {
        engine_queue_scene(d ? d - 1 : 9);
        ui_msg("SCENE NEXT", 0xFF, d ? d : 10);
        return;
    }
    if (page == P_EDIT) {
        if (held_trk != NONE) {
            trk_used = 1;
            if (d >= 1 && d <= 4)
                edit_put(E_RAT, d);
            return;
        }
        if (fh()[H_ROLE] != R_DRUM) {
            if (d >= 1 && d <= 8)
                edit_put(E_NOTE, d - 1);
            return;
        }
        lim = 127;
    } else {
        select_field();
        if (cur_f->fmt >= FM_LIST || cur_f->kind == FK_ACT || cur_f->fmt == FM_STYLE)
            return;
        lim = (uint16_t)f_max() + 1;
    }
    ent_v = ent_v * 10 + d;
    ++ent_n;
    ent_t = 0;
    ui_dirty = 1;
    if (ent_n >= 3 || ent_v * 10 > lim)
        ent_commit();
}

static void ent_commit(void)
{
    uint16_t v = ent_v;
    ent_n = 0;
    ent_v = 0;
    if (page == P_EDIT) {
        edit_put(E_NOTE, (int16_t)v);
        return;
    }
    select_field();
    if (cur_f->fmt == FM_NUM1)
        --v;
    else if (cur_f->fmt == FM_SIGN)
        v += 24;
    set_val((int16_t)v);
}

static void tap(void)
{
    uint16_t now = scan_n, dt = now - tap_last;
    tap_last = now;
    if (tap_n && dt > 60 && dt < 750) {
        tempo_set((uint16_t)(150000UL / dt));
        ui_msg("TAP TEMPO", 0xFF, tempo_get() / 10);
    }
    tap_n = 1;
}

static void arm_lane(uint8_t t)
{
    engine_arm(t);
    ui_msg(lr[t].cap ? "CAPT ON" : "CAPT OFF", t, 0xFFFF);
}

static void execute(void)
{
    if (page == P_COPY) {
        scene_copy(cfg.scene, copy_dst);
        ui_msg("SCENE COPIED", 0xFF, copy_dst + 1);
    } else if (page == P_ERASE) {
        scene_erase(cfg.scene);
        gen_cancel();
        ui_msg("SCENE ERASED", 0xFF, cfg.scene + 1);
    } else if (page == P_CLOCK && fld == 2) {
        if (playing)
            engine_stop();
        dump_start();
        ui_msg("SENDING DUMP", 0xFF, 0xFFFF);
    } else {
        arm_lane(focus);
    }
}

/* QUANT held: tracks in mute_trk drop out. LOOP held past 300 ms: those tracks are the
 * momentary solo. A shorter LOOP press toggles the latched solo when the track is released. */
static void perf_apply(void)
{
    static __xdata uint8_t t, solo;
    solo = 0;
    if (key_held(K_LOOP)) {
        for (t = 0; t < NLANES; ++t)
            if ((solo_trk & (uint8_t)(1 << t)) && (uint16_t)(scan_n - trk_at[t]) > HOLD_SCANS)
                solo |= (uint8_t)(1 << t);
    }
    engine_set_mom_mute(key_held(K_QUANT) ? mute_trk : 0);
    engine_set_mom_solo(solo);
    engine_set_stutter(key_held(K_LOOP) ? (key_held(K_FF) ? 1 : key_held(K_REW) ? 2 : 0) : 0);
}

void ui_key(uint8_t k, uint8_t dn)
{
    uint8_t i, t, bit;
    if (k >= K_TRK1 && k < K_TRK1 + 8) {
        t = (uint8_t)(k - K_TRK1);
        bit = (uint8_t)(1 << t);
        if (!dn) {
            if (mute_trk & bit) {
                mute_trk = (uint8_t)(mute_trk & (uint8_t)~bit);
                perf_apply();
                return;
            }
            if (solo_trk & bit) {
                solo_trk = (uint8_t)(solo_trk & (uint8_t)~bit);
                i = (uint16_t)(scan_n - trk_at[t]) <= HOLD_SCANS;
                perf_apply();
                if (i)
                    engine_toggle_solo(t);
                return;
            }
            if (tap_trk & bit) {
                tap_trk = (uint8_t)(tap_trk & (uint8_t)~bit);
                if ((uint16_t)(scan_n - trk_at[t]) <= HOLD_SCANS)
                    engine_toggle_mute(t);
            }
            if (held_trk == t) {
                if (page == P_EDIT && !trk_used)
                    edit_toggle(t);
                held_trk = NONE;
            }
            return;
        }
        if (key_held(K_QUANT)) {
            quant_used = 1;
            mute_trk |= bit;
            trk_at[t] = scan_n;
            focus = t;
            perf_apply();
            live_view();
            return;
        }
        if (key_held(K_LOOP)) {
            loop_used = 1;
            solo_trk |= bit;
            trk_at[t] = scan_n;
            focus = t;
            live_view();
            return;
        }
        if (key_held(K_MERGE)) {
            merge_used = 1;
            focus = t;
            lane_mutate(t);
            ui_msg("MUTATE", t, HDR(t)[H_SEED]);
            return;
        }
        if (page == P_EDIT) {
            if ((uint8_t)(edit_pg * 8 + t) < fh()[H_LEN])
                edit_sel = (uint8_t)(edit_pg * 8 + t);
            held_trk = t;
            trk_used = 0;
        } else if (key_held(K_REC)) {
            rec_used = 1;
            arm_lane(t);
        } else if (page == P_COPY) {
            lane_copy(focus, t);
            lr[t].nv = 0;
            ui_msg("COPIED", t, 0xFFFF);
        } else if (page == P_ERASE) {
            lane_erase(t);
            gen_ready &= (uint8_t)~(1 << t);
            ui_msg("ERASED", t, 0xFFFF);
        } else {
            focus = t;
            if (page == P_HOME) {
                held_trk = t;
                trk_at[t] = scan_n;
                tap_trk |= bit;
            }
        }
        ui_dirty = 1;
        return;
    }
    if (k == K_FOOT || k == K_FILTER) {
        fill = dn || key_held(k == K_FOOT ? K_FILTER : K_FOOT);
        ui_dirty = 1;
        return;
    }
    if (!dn) {
        if (k == K_REW || k == K_FF)
            perf_apply();
        if (k == K_REC && !rec_used)
            execute();
        if (k == K_MERGE && !merge_used) {
            lane_mutate(focus);
            ui_msg("MUTATE", focus, HDR(focus)[H_SEED]);
        }
        if (k == K_QUANT || k == K_LOOP)
            perf_apply();
        for (i = 0; page_keys[i] != 0xFF; i += 2)
            if (page_keys[i] == k && page_keys[i + 1] != P_EDIT && page == page_keys[i + 1])
                live_view();
        if (k == K_STOP && stop_plain) {
            stop_plain = 0;
            if (playing)
                engine_stop();
            else
                engine_start(1);
        }
        return;
    }
    if (ent_n && k != K_PLUS && k != K_MINUS && key_digit_of(k) == NONE)
        ent_commit();
    if (is_page_key(k) && key_held(K_STOP)) {
        if (k == K_QUANT)
            quant_used = 1;
        if (k == K_LOOP)
            loop_used = 1;
        exit_combo();
        return;
    }
    switch (k) {
    case K_PLAY:
        if (key_held(K_CLICK))
            tap();
        else
            engine_start(0);
        return;
    case K_STOP:
        /* Act on release, unless a function key makes this the exit combo. */
        stop_plain = page_key_held() ? 0 : 1;
        if (!stop_plain)
            exit_combo();
        return;
    case K_REC:
        if (key_held(K_MERGE)) {
            merge_used = 1;
            rec_used = 1;
            for (t = 1; t < NLANES; ++t)
                if (!(HDR(t)[H_FLAGS] & F_CAPT))
                    lane_mutate(t);
            ui_msg("MUTATE ALL", 0xFF, 0xFFFF);
            return;
        }
        rec_used = 0;
        /* A page action acts on the press, while its function key is still held. */
        if (page == P_COPY || page == P_ERASE || (page == P_CLOCK && fld == 2)) {
            rec_used = 1;
            execute();
        }
        return;
    case K_MERGE:
        if (key_held(K_COPY) || page == P_COPY) {
            merge_used = 1;
            page = P_HOME;
            scene_snap_save();
            ui_msg("SNAPSHOT SAVED", 0xFF, cfg.scene + 1);
            return;
        }
        if (key_held(K_ERASE) || page == P_ERASE) {
            merge_used = 1;
            page = P_HOME;
            if (scene_snap_load()) {
                for (t = 0; t < NLANES; ++t)
                    lr[t].nv = 0;
                ui_msg("SNAPSHOT LOADED", 0xFF, cfg.scene + 1);
            }
            return;
        }
        merge_used = 0;
        return;
    case K_QUANT:
        quant_used = 0;
        break;
    case K_LOOP:
        loop_used = 0;
        break;
    case K_PLUS:
    case K_MINUS:
        bump(k == K_PLUS ? 1 : -1);
        return;
    case K_REW:
    case K_FF:
        if (key_held(K_LOOP)) {
            loop_used = 1;
            perf_apply();
            live_view();
            ui_msg(k == K_FF ? "STUTTER 1/16" : "STUTTER 1/8", 0xFF, 0xFFFF);
            return;
        }
        rotate(k == K_FF ? 1 : -1);
        return;
    case K_ECHO:
        cfg.thru ^= 1;
        ui_msg(cfg.thru ? "MIDI THRU ON" : "MIDI THRU OFF", 0xFF, 0xFFFF);
        return;
    case K_PGUP:
    case K_PGDN:
        t = page == P_EDIT ? E_N : pages[page].count;
        if (t) {
            fld = k == K_PGDN ? (uint8_t)(fld + 1 >= t ? 0 : fld + 1) : (uint8_t)(fld ? fld - 1 : t - 1);
            ent_n = 0;
            ent_v = 0;
            ui_dirty = 1;
        }
        return;
    }
    t = key_digit_of(k);
    if (t != NONE) {
        digit(t);
        return;
    }
    for (i = 0; page_keys[i] != 0xFF; i += 2)
        if (page_keys[i] == k) {
            open_page(page_keys[i + 1]);
            return;
        }
}

/* Every 4 ms. */
void ui_scan(void)
{
    if (msg_t && !--msg_t)
        ui_dirty = 1;
    if (ent_n && ++ent_t >= ENT_SCANS)
        ent_commit();
    if (solo_trk || mute_trk || stutter)
        perf_apply();
    if (beat_flag) {
        beat_flag = 0;
        ui_dirty = 1;
    }
}

void ui_cc(uint8_t num, uint8_t val)
{
    __xdata uint8_t *h;
    uint8_t mn, mx;
    num;
    if (page == P_HOME) {
        h = fh();
        mn = (uint8_t)((val * 10u) >> 7);
        if (mn != h[H_DENS]) {
            h[H_DENS] = mn;
            lane_regen();
        }
        ui_dirty = 1;
        return;
    }
    if (page == P_EDIT || !pages[page].count)
        return;
    select_field();
    if (cur_f->kind == FK_ACT)
        return;
    mn = cur_f->min;
    mx = f_max();
    set_val((int16_t)(mn + (((uint16_t)val * (mx - mn + 1)) >> 7)));
}

void ui_init(void)
{
    page = P_HOME;
    focus = 0;
    held_trk = NONE;
    led_trk = led_mode = 0xAA;
    ui_dirty = 1;
}

/* ---- rendering ---- */

static void put_list(uint8_t pos, uint8_t idx)
{
    lcd_text(pos, lists + (uint16_t)idx * 8, 8);
}

static void put_note(uint8_t pos, uint8_t n)
{
    uint8_t o = n / 12;
    lcd_text(pos, notes + (n % 12) * 2, 2);
    lcd[pos + 2] = o ? (char)('0' + o - 1) : '-';
}

static void put_tempo(uint8_t pos)
{
    uint16_t t = tempo_get();
    lcd_num(pos, t / 10, 5);
    lcd[pos + 5] = '.';
    lcd[pos + 6] = (char)('0' + t % 10);
}

static void put_02d(uint8_t pos, uint8_t v)
{
    lcd_num(pos, v, 2);
    if (lcd[pos] == ' ')
        lcd[pos] = '0';
}

static void put_val(uint8_t v)
{
    uint8_t fm = cur_f->fmt;
    lcd_fill(24, 8);
    if (fm >= FM_LIST) {
        put_list(24, (uint8_t)(fm - FM_LIST + v));
        return;
    }
    switch (fm) {
    case FM_STYLE:
        put_list(24, (uint8_t)(style_list[fh()[H_ROLE] & 3] + v));
        break;
    case FM_NUM1:
        lcd_num(27, (uint16_t)v + 1, 5);
        break;
    case FM_OFF:
    case FM_END:
        if (!v)
            lcd_text(29, fm == FM_OFF ? "OFF" : "END", 3);
        else
            lcd_num(27, v, 5);
        break;
    case FM_NOTE:
        put_note(29, v);
        break;
    case FM_KEY:
        lcd_text(30, notes + v * 2, 2);
        break;
    case FM_SIGN:
        lcd_num(27, v >= 24 ? v - 24 : 24 - v, 5);
        if (v != 24)
            lcd[28] = v > 24 ? '+' : '-';
        break;
    case FM_PCT:
        lcd_num(26, v, 5);
        lcd[31] = '%';
        break;
    case FM_ACT:
        lcd_text(24, cur_f->off == ACT_DUMP ? "SEND REC" : "ERA REC ", 8);
        break;
    default:
        if (cur_f->kind == FK_TEMPO)
            put_tempo(25);
        else
            lcd_num(27, v, 5);
        break;
    }
}

static void put_chord(uint8_t pos)
{
    uint8_t r = harm_root_pc(), q = harm_minor();
    lcd_text(pos, notes + r * 2, 2);
    if (lcd[pos + 1] == ' ')
        --pos;
    lcd[pos + 2] = q == 1 ? 'm' : q == 2 ? 'o' : ' ';
}

static void render_home(void)
{
    static const char __code roles[] = "DBCL";
    __xdata uint8_t *h = fh();
    uint8_t l, c, bars, bit, aud, active;
    lcd_fill(0, 16);
    lcd_text(0, lists + (uint16_t)(style_list[h[H_ROLE] & 3] + h[H_STYLE]) * 8, 8);
    put_chord(9);
    lcd[14] = 'T';
    lcd[15] = (char)('1' + focus);
    active = mom_solo ? mom_solo : solo_latch;
    for (l = 0, bit = 1; l < NLANES; ++l, bit <<= 1) {
        h = HDR(l);
        aud = lane_audible(l);
        c = roles[h[H_ROLE] & 3];
        if (h[H_FLAGS] & F_MUTE)
            c = '.';
        if (lr[l].cap)
            c = 'R';
        if (fill && !(h[H_FLAGS] & F_MUTE) && h[H_ROLE] == R_DRUM)
            c = 'F';
        if (active && (active & bit) && aud)
            c = 'S';
        else if (!aud && !(h[H_FLAGS] & F_MUTE))
            c = '-';
        if ((mute_pend | solo_pend) & bit)
            c = '*';
        lcd[16 + l] = c;
    }
    lcd_fill(24, 5);
    lcd[29] = 'S';
    put_02d(30, cfg.scene + 1);
    if (queued != 0xFF) {
        lcd[25] = '>';
        put_02d(26, queued + 1);
    } else if (playing) {
        bars = cfg.bars[cfg.scene];
        lcd_num(24, (scene_bar - 1) % (bars ? bars : 1) + 1, 2);
        lcd[26] = '.';
        lcd[27] = (char)('1' + tp_tick / PPQN);
    }
}

static void render_edit(void)
{
    __xdata uint8_t *g = edit_step(), *h = fh();
    uint8_t v;
    lcd_text(0, "EDIT T", 6);
    lcd[6] = (char)('1' + focus);
    lcd_text(7, " P  S   ", 8);
    lcd[9] = (char)('1' + edit_pg);
    lcd_num(12, edit_sel + 1, 2);
    lcd[15] = (g[0] & S_ON) ? '*' : '-';
    lcd_text(16, edit_labels + fld * 8, 8);
    lcd_fill(24, 8);
    v = edit_get();
    switch (fld) {
    case E_VEL:
        if (v)
            lcd_num(27, v, 5);
        else
            lcd_text(28, "LANE", 4);
        break;
    case E_NOTE:
        if (h[H_ROLE] == R_DRUM) {
            if (v)
                put_note(29, v);
            else
                lcd_text(28, "LANE", 4);
        } else {
            lcd_text(25, "DEG", 3);
            lcd_num(29, (uint16_t)v + 1, 3);
        }
        break;
    case E_OCT:
        lcd_num(27, v >= 4 ? v - 4 : 4 - v, 5);
        if (v != 4)
            lcd[30] = v > 4 ? '+' : '-';
        break;
    case E_PROB:
        lcd_num(26, (uint16_t)(v + 1) * 100 / 16, 5);
        lcd[31] = '%';
        break;
    case E_COND:
        put_list(24, (uint8_t)(L_COND + v));
        break;
    case E_ACC:
    case E_TIE:
        put_list(24, (uint8_t)(L_ONOFF + v));
        break;
    default:
        lcd_num(27, v, 5);
        break;
    }
}

void ui_render(void)
{
    const page_t __code *p = &pages[page];
    if (page == P_HOME)
        render_home();
    else if (page == P_EDIT)
        render_edit();
    else {
        lcd_text(0, p->title, 8);
        lcd_fill(8, 8);
        if (p->lane) {
            lcd[8] = 'T';
            lcd[9] = (char)('1' + focus);
            lcd_text(11, lists + (uint16_t)(L_ROLE + (fh()[H_ROLE] & 3)) * 8, 5);
        } else if (page == P_HARM) {
            put_chord(13);
        } else if (page == P_TEMPO) {
            lcd_text(8, cfg.sync ? "SYNC EXT" : "SYNC INT", 8);
        } else {
            lcd_text(9, "SCENE", 5);
            lcd_num(14, cfg.scene + 1, 2);
        }
        if (p->count) {
            select_field();
            lcd_text(16, cur_f->lab, 8);
            put_val(get_val());
        }
    }
    if (ent_n) {
        lcd_fill(24, 8);
        lcd_num(27, ent_v, 5);
        lcd[31] = '_';
    }
    if (msg_t) {
        uint8_t i;
        for (i = 0; i < 16; ++i)
            lcd[16 + i] = msgbuf[i];
    }
}

void ui_leds(void)
{
    static __xdata uint8_t *__xdata h, *__xdata g;
    static __xdata lane_rt *__xdata r;
    static __xdata uint8_t l, on, m, fast, slow, len, idx, ph, bit, caps;
    on = m = caps = 0;
    for (l = 0, r = lr, bit = 1; l < NLANES; ++l, ++r, bit <<= 1)
        if (r->cap)
            caps |= bit;
    fast = (uint8_t)((scan_n >> 5) & 1);
    slow = (uint8_t)((scan_n >> 7) & 1);
    ph = 0xFF;
    if (page == P_EDIT) {
        h = fh();
        g = GRID(focus);
        len = h[H_LEN];
        if (playing && lr[focus].step != 0xFF) {
            ph = (uint8_t)(lr[focus].step + h[H_ROT] % len);
            if (ph >= len)
                ph -= len;
        }
        idx = (uint8_t)(edit_pg * 8);
        g += (uint16_t)idx * 4;
        for (bit = 1; bit; bit <<= 1, ++idx, g += 4) {
            if (idx < len && (*g & S_ON))
                on |= bit;
            if (idx == ph && fast)
                on ^= bit;
        }
    } else {
        for (l = 0, bit = 1; bit; ++l, bit <<= 1) {
            if (caps & bit)
                idx = slow;
            else {
                idx = lane_audible(l);
                if ((mute_pend | solo_pend) & bit)
                    idx ^= fast;
            }
            if (idx)
                on |= bit;
        }
    }
    if (caps)
        m |= L_REC;
    if (playing)
        m |= L_PLAY;
    if (queued != 0xFF && fast)
        m |= L_PART;
    if (cfg.chain_on)
        m |= L_SONG;
    if (page == P_EDIT)
        m |= L_EDIT;
    if (cfg.thru)
        m |= L_ECHO;
    if (playing && tp_bt < 12)
        m |= L_LOOP;
    on = (uint8_t)~on;
    m = (uint8_t)~m;
    if (on != led_trk)
        LED_DATA = led_trk = on;
    if (m != led_mode)
        LED_BANK = led_mode = m;
}
