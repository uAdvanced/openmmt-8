/* Scene bank (16 scenes x 8 lanes in battery RAM), the factory scenes, lane and scene
 * copy/erase, and the sysex dump of the settings, the bank and the chain. */
#include "jam.h"
#include <stddef.h>

__xdata uint16_t cur_base;

void xcopy(uint16_t dst, uint16_t src, uint16_t n)
{
    __xdata uint8_t *d = XMEM(dst), *s = XMEM(src);
    while (n--)
        *d++ = *s++;
}

void xfill(uint16_t dst, uint8_t v, uint16_t n)
{
    __xdata uint8_t *d = XMEM(dst);
    while (n--)
        *d++ = v;
}

void scene_select(uint8_t s)
{
    if (s >= NSCENES)
        s = 0;
    cfg.scene = s;
    cur_base = SCENE_AT(s);
}

void lane_defaults(__xdata uint8_t *h, uint8_t role, uint8_t note, uint8_t style)
{
    uint8_t i;
    for (i = 0; i < HDR_SIZE; ++i)
        h[i] = 0;
    h[H_ROLE] = role;
    h[H_CHAN] = role == R_DRUM ? 9 : role;
    h[H_NOTE] = note;
    h[H_LEN] = 16;
    h[H_DIV] = DIV_16;
    h[H_GATE] = role == R_DRUM ? 30 : role == R_BASS ? 60 : 45;
    h[H_STYLE] = style;
    h[H_SEED] = 1;
    h[H_DENS] = 5;
    h[H_EK] = 4;
    h[H_EN] = 16;
    h[H_FLAGS] = role == R_DRUM ? 0 : F_FOLLOW;
    h[H_VEL] = 100;
    h[H_HUM] = 2;
    h[H_VOICES] = 3;
}

void lane_copy(uint8_t from, uint8_t to)
{
    if (from == to)
        return;
    xcopy(cur_base + (uint16_t)to * HDR_SIZE, cur_base + (uint16_t)from * HDR_SIZE, HDR_SIZE);
    xcopy(cur_base + SCENE_GRIDS + (uint16_t)to * GRID_SIZE, cur_base + SCENE_GRIDS + (uint16_t)from * GRID_SIZE,
          GRID_SIZE);
}

void lane_erase(uint8_t lane)
{
    xfill(cur_base + SCENE_GRIDS + (uint16_t)lane * GRID_SIZE, 0, GRID_SIZE);
    HDR(lane)[H_FLAGS] |= F_CAPT;
}

void scene_copy(uint8_t from, uint8_t to)
{
    if (from != to)
        xcopy(SCENE_AT(to), SCENE_AT(from), SCENE_SIZE);
}

void scene_erase(uint8_t s)
{
    uint8_t l;
    xfill(SCENE_AT(s) + SCENE_GRIDS, 0, NLANES * GRID_SIZE);
    for (l = 0; l < NLANES; ++l)
        XMEM(SCENE_AT(s) + (uint16_t)l * HDR_SIZE)[H_FLAGS] |= F_CAPT;
}

void scene_snap_save(void)
{
    xcopy(SNAP_BASE, cur_base, SCENE_SIZE);
    XMEM(SNAP_BARS_ADDR)[0] = cfg.bars[cfg.scene];
    XMEM(SNAP_SIGN_ADDR)[0] = JAM_MAGIC;
}

uint8_t scene_snap_load(void)
{
    if (XMEM(SNAP_SIGN_ADDR)[0] != JAM_MAGIC)
        return 0;
    xcopy(cur_base, SNAP_BASE, SCENE_SIZE);
    cfg.bars[cfg.scene] = XMEM(SNAP_BARS_ADDR)[0];
    gen_cancel();
    return 1;
}

/* ---- factory scenes ---- */

#define LP(l, f, v) ((uint8_t)((l) * HDR_SIZE + (f))), (v)

static const uint8_t __code fac_role[NLANES] = {
    R_DRUM, R_DRUM, R_DRUM, R_DRUM, R_BASS, R_CHORD, R_LEAD, R_DRUM};
static const uint8_t __code fac_note[NLANES] = {36, 39, 42, 46, 36, 60, 72, 37};
static const uint8_t __code fac_patch[] = {
    /* 1: house */
    LP(2, H_DENS, 6), LP(4, H_STYLE, BS_OFFBEAT), LP(5, H_FLAGS, F_FOLLOW | F_SEV), LP(5, H_VOICES, 4),
    LP(6, H_STYLE, LS_UPDN), LP(6, H_LEN, 12), LP(6, H_DENS, 4),
    LP(7, H_STYLE, DS_EUCLID), LP(7, H_EK, 5), LP(7, H_EROT, 2), 0xFF,
    /* 2: techno */
    LP(0, H_STYLE, DS_TECHNO), LP(1, H_STYLE, DS_TECHNO), LP(2, H_STYLE, DS_TECHNO), LP(2, H_DENS, 7),
    LP(3, H_STYLE, DS_TECHNO), LP(4, H_STYLE, BS_ACID), LP(4, H_EVOL, 20), LP(4, H_GATE, 50),
    LP(5, H_STYLE, CS_PAD), LP(6, H_STYLE, LS_TURING), LP(6, H_EVOL, 15),
    LP(7, H_STYLE, DS_EUCLID), LP(7, H_EK, 3), LP(7, H_EN, 8), LP(7, H_EROT, 2), 0xFF,
    /* 3: Euclid polymeter */
    LP(0, H_STYLE, DS_EUCLID),
    LP(1, H_NOTE, 38), LP(1, H_STYLE, DS_EUCLID), LP(1, H_EK, 3), LP(1, H_EN, 8), LP(1, H_EROT, 4), LP(1, H_LEN, 8),
    LP(2, H_STYLE, DS_EUCLID), LP(2, H_EK, 7), LP(2, H_EN, 12), LP(2, H_LEN, 12),
    LP(3, H_NOTE, 37), LP(3, H_STYLE, DS_EUCLID), LP(3, H_EK, 5), LP(3, H_EN, 7), LP(3, H_LEN, 7),
    LP(4, H_STYLE, BS_EUCLID), LP(4, H_EK, 3), LP(4, H_EN, 5), LP(4, H_LEN, 5),
    LP(5, H_STYLE, CS_PAD), LP(5, H_LEN, 32),
    LP(6, H_STYLE, LS_TURING), LP(6, H_LEN, 10),
    LP(7, H_NOTE, 46), LP(7, H_STYLE, DS_EUCLID), LP(7, H_EK, 2), LP(7, H_EN, 5), LP(7, H_EROT, 1), LP(7, H_LEN, 5),
    0xFF};

void scene_factory(void)
{
    static const uint8_t __code chain[] = {1, 2, 2, 2, 1, 2, 3, 4};
    const uint8_t __code *p = fac_patch;
    uint8_t s, l;

    xfill(CFG_BASE, 0, 256);
    cfg.tempo_hi = 1240u >> 8;
    cfg.tempo_lo = 1240u & 0xFF;
    cfg.swing = 54;
    cfg.key = 9; /* A */
    cfg.scale = 1;
    cfg.prog = 1;
    cfg.ctrl_ch = 1;
    cfg.ctrl_mode = 1;
    cfg.clk_out = 1;
    cfg.quant = Q_BAR;
    for (s = 0; s < NSCENES; ++s)
        cfg.bars[s] = 4;

    for (s = 0; s < 3; ++s) {
        scene_select(s);
        for (l = 0; l < NLANES; ++l)
            lane_defaults(HDR(l), fac_role[l], fac_note[l], 0);
        while (*p != 0xFF) {
            XMEM(cur_base)[p[0]] = p[1];
            p += 2;
        }
        ++p;
        for (l = 0; l < NLANES; ++l)
            gen_grid(l);
    }

    for (s = 3; s < NSCENES; ++s)
        scene_copy(0, s);
    xfill(CHAIN_BASE, 0, CHAIN_LEN * 2);
    for (s = 0; s < sizeof chain; ++s)
        XMEM(CHAIN_BASE)[s] = chain[s];
    scene_select(0);
    scene_snap_save();
    mem_sign();
}

/* ---- validity: battery RAM keeps whatever the previous ROM left there ---- */
static const char __code mem_sig[4] = {'J', 'A', 'M', '8'};

uint8_t mem_signed(void)
{
    uint8_t i;
    if (cfg.magic != JAM_MAGIC)
        return 0;
    for (i = 0; i < 4; ++i)
        if (cfg.sig[i] != mem_sig[i])
            return 0;
    return 1;
}

void mem_sign(void)
{
    uint8_t i;
    for (i = 0; i < 4; ++i)
        cfg.sig[i] = mem_sig[i];
    cfg.magic = JAM_MAGIC;
}

typedef struct {
    uint8_t off, lo, hi, def;
} lim_t;

static const lim_t __code cfg_lim[] = {
    {offsetof(cfg_t, swing), 50, 75, 54},   {offsetof(cfg_t, key), 0, 11, 0},
    {offsetof(cfg_t, scale), 0, NSCALES - 1, 0}, {offsetof(cfg_t, prog), 0, NPROGS - 1, 0},
    {offsetof(cfg_t, ctrl_ch), 0, 16, 0},   {offsetof(cfg_t, ctrl_mode), 0, 1, 0},
    {offsetof(cfg_t, knob), 0, 119, 0},     {offsetof(cfg_t, sync), 0, 1, 0},
    {offsetof(cfg_t, clk_out), 0, 1, 0},    {offsetof(cfg_t, click), 0, 1, 0},
    {offsetof(cfg_t, quant), 0, Q_BAR, Q_BAR}, {offsetof(cfg_t, scene), 0, NSCENES - 1, 0},
    {offsetof(cfg_t, thru), 0, 1, 0},       {offsetof(cfg_t, chain_on), 0, 1, 0},
};

static const lim_t __code hdr_lim[] = {
    {H_ROLE, 0, NROLES - 1, R_DRUM}, {H_CHAN, 0, 15, 9},   {H_NOTE, 0, 127, 36}, {H_LEN, 1, MAXLEN, 16},
    {H_DIV, 0, DIV_N - 1, DIV_16},   {H_GATE, 1, 99, 30},  {H_SEED, 0, 99, 1},   {H_DENS, 0, 9, 5},
    {H_EK, 0, 64, 4},                {H_EN, 1, 64, 16},    {H_EROT, 0, 63, 0},   {H_EVOL, 0, 99, 0},
    {H_VEL, 1, 127, 100},            {H_HUM, 0, 9, 2},     {H_VOICES, 3, 4, 3},  {H_ROT, 0, 63, 0},
    {H_MCC, 0, 119, 0},
};

static void clamp(__xdata uint8_t *p, const lim_t __code *t, uint8_t n)
{
    uint8_t v;
    for (; n; --n, ++t) {
        v = p[t->off];
        if (v < t->lo || v > t->hi)
            p[t->off] = t->def;
    }
}

/* Every setting, lane header and chain entry back inside the range the UI can set. */
void mem_sanitize(void)
{
    __xdata uint8_t *h;
    uint8_t s, l;
    uint16_t t = tempo_get();

    if (t < 400 || t > 2500) {
        cfg.tempo_hi = 1200u >> 8;
        cfg.tempo_lo = 1200u & 0xFF;
    }
    clamp((__xdata uint8_t *)&cfg, cfg_lim, sizeof cfg_lim / sizeof cfg_lim[0]);
    for (s = 0; s < NSCENES; ++s) {
        if (cfg.bars[s] < 1 || cfg.bars[s] > 16)
            cfg.bars[s] = 4;
        for (l = 0; l < NLANES; ++l) {
            h = XMEM(SCENE_AT(s) + (uint16_t)l * HDR_SIZE);
            clamp(h, hdr_lim, sizeof hdr_lim / sizeof hdr_lim[0]);
            if (h[H_STYLE] >= style_count[h[H_ROLE]])
                h[H_STYLE] = 0;
            if ((uint8_t)(h[H_TRANS] + 24) > 48)
                h[H_TRANS] = 0;
            if (h[H_ROT] >= h[H_LEN])
                h[H_ROT] = 0;
        }
    }
    h = XMEM(CHAIN_BASE);
    for (s = 0; s < CHAIN_LEN; ++s, h += 2) {
        if (h[0] > NSCENES)
            h[0] = 0;
        if (h[1] > 16)
            h[1] = 1;
    }
}

/* ---- sysex dump: F0 7D 4A 38 01, each byte as two nibbles, a 7-bit sum as two nibbles, F7.
 * The settings page, then the scene bank and the chain. ---- */
__bit dump_run;
__xdata uint8_t dump_rx_state;
static __xdata uint16_t d_count;
static __xdata uint8_t d_sum, d_phase, d_hi;
static const uint8_t __code d_head[] = {0xF0, 0x7D, 0x4A, 0x38, 0x01};
#define DUMP_TOTAL (256u + (MEM_END - BANK_BASE))

static uint16_t d_addr(uint16_t n)
{
    return n < 256u ? CFG_BASE + n : BANK_BASE + (n - 256u);
}

void dump_start(void)
{
    uint8_t i;
    for (i = 0; i < sizeof d_head; ++i)
        midi_tx(d_head[i]);
    d_count = 0;
    d_sum = 0;
    dump_run = 1;
}

void dump_slice(void)
{
    uint8_t b;
    while (midi_tx_free() > 4) {
        if (d_count == DUMP_TOTAL) {
            midi_tx(d_sum >> 4);
            midi_tx(d_sum & 15);
            midi_tx(0xF7);
            dump_run = 0;
            ui_msg("DUMP SENT", 0xFF, 0xFFFF);
            return;
        }
        b = XMEM(d_addr(d_count))[0];
        d_sum = (uint8_t)((d_sum + b) & 0x7F);
        midi_tx(b >> 4);
        midi_tx(b & 15);
        ++d_count;
    }
}

/* Receive: called for every byte of a sysex message, F0 to F7. */
void dump_rx(uint8_t b)
{
    if (b == 0xF0) {
        dump_rx_state = 1;
        return;
    }
    if (!dump_rx_state)
        return;
    if (dump_rx_state < sizeof d_head) {
        dump_rx_state = d_head[dump_rx_state] == b ? dump_rx_state + 1 : 0;
        if (dump_rx_state == sizeof d_head) {
            d_count = 0;
            d_sum = 0;
            d_phase = 0;
            if (playing)
                engine_stop();
            ui_msg("RECEIVING DUMP", 0xFF, 0xFFFF);
        }
        return;
    }
    if (b == 0xF7) {
        dump_rx_state = 0;
        if (d_count == DUMP_TOTAL + 1) {
            mem_sign();
            mem_sanitize();
            harm_init();
            gen_cancel();
            scene_select(cfg.scene);
            tempo_set(tempo_get());
            ui_msg("DUMP LOADED", 0xFF, 0xFFFF);
        } else {
            cfg.magic = 0;
            ui_msg("DUMP ERROR", 0xFF, 0xFFFF);
        }
        return;
    }
    if (!d_phase) {
        d_hi = b;
        d_phase = 1;
        return;
    }
    d_phase = 0;
    b = (uint8_t)((d_hi << 4) | (b & 15));
    if (d_count < DUMP_TOTAL) {
        XMEM(d_addr(d_count))[0] = b;
        d_sum = (uint8_t)((d_sum + b) & 0x7F);
        ++d_count;
    } else if (d_count == DUMP_TOTAL) {
        d_count += b == d_sum;
    }
}
