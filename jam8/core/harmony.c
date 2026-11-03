/* Harmony bus: key, scale and chord root shared by the melodic lanes, and the voice
 * leading of chord lanes. A control-channel note changes the key (or, in chord mode, the
 * chord root) on the grid the launch quantize sets. */
#include "jam.h"

__xdata uint8_t h_semis[12], h_n, h_deg, h_id, h_key;
static __xdata uint8_t pend_key, pend_deg, ovr, ctrl_cnt, ctrl_root, pend_root, ovr_root;
static __xdata uint16_t ctrl_held, pend_pcs, ovr_pcs;
#define NONE 0xFFu

void harm_set_scale(void)
{
    uint16_t m = scale_masks[cfg.scale < NSCALES ? cfg.scale : 0];
    uint8_t i, n = 0;
    for (i = 0; i < 12; ++i, m >>= 1)
        if (m & 1)
            h_semis[n++] = i;
    h_n = n;
    if (h_deg >= n)
        h_deg = 0;
    ++h_id;
}

void harm_init(void)
{
    h_key = MOD12(cfg.key);
    pend_key = pend_deg = ovr = NONE;
    ctrl_cnt = 0;
    ctrl_held = pend_pcs = ovr_pcs = 0;
    h_deg = 0;
    harm_set_scale();
    harm_bar(0);
    ++h_id;
}

static void set_deg(uint8_t d)
{
    while (d >= h_n)
        d -= h_n;
    if (d != h_deg) {
        h_deg = d;
        ++h_id;
    }
}

/* At each bar start: the progression's root for this bar, unless a played chord holds. */
void harm_bar(uint16_t bar)
{
    uint8_t p = cfg.prog < NPROGS ? cfg.prog : 0;
    ctrl_cnt = 0;
    ctrl_held = 0;
    if (ovr == NONE && !ovr_pcs)
        set_deg(prog_deg[p][(uint8_t)(bar % prog_len[p])]);
}

void harm_apply_pending(uint8_t at)
{
    if (cfg.quant > at)
        return;
    if (pend_key != NONE) {
        cfg.key = h_key = pend_key;
        pend_key = NONE;
        ovr_pcs = 0;
        ++h_id;
    }
    if (pend_deg != NONE) {
        ovr = pend_deg;
        ovr_pcs = pend_pcs;
        ovr_root = pend_root;
        pend_pcs = 0;
        set_deg(pend_deg);
        ++h_id;
        pend_deg = NONE;
    }
}

void harm_ctrl_note(uint8_t note)
{
    static __xdata uint8_t pc, rel, d;
    static __xdata uint16_t bit;
    pc = MOD12(note);
    bit = (uint16_t)1 << pc;
    if (!(ctrl_held & bit)) {
        if (!ctrl_cnt)
            ctrl_root = pc;
        ctrl_held |= bit;
        ++ctrl_cnt;
    }
    if (!cfg.ctrl_mode) {
        pend_key = pc;
        ovr = NONE;
        pend_pcs = ovr_pcs = 0;
    } else {
        rel = MOD12((ctrl_cnt >= 2 ? ctrl_root : pc) + 12 - h_key);
        for (d = 0; d < h_n; ++d)
            if (h_semis[d] >= rel)
                break;
        if (d >= h_n)
            d = 0;
        if (ctrl_cnt >= 2) {
            pend_deg = d;
            pend_root = ctrl_root;
            pend_pcs = ctrl_held;
        } else if (h_semis[d] == rel) {
            pend_deg = d;
            pend_pcs = 0;
        }
    }
    harm_apply_pending(Q_STEP);
}

void harm_ctrl_off(uint8_t note)
{
    uint16_t bit = (uint16_t)1 << MOD12(note);
    if (ctrl_held & bit) {
        ctrl_held &= ~bit;
        --ctrl_cnt;
    }
}

uint8_t harm_note(uint8_t base, uint8_t code, uint8_t follow, int8_t trans)
{
    uint8_t deg = code & 31;
    int8_t oct = (int8_t)((code >> 5) - 4);
    int16_t note;
    if (follow)
        deg += h_deg;
    while (deg >= h_n) {
        deg -= h_n;
        ++oct;
    }
    note = (int16_t)(uint8_t)(base - MOD12(base)) + h_key + h_semis[deg] + trans;
    for (; oct > 0; --oct)
        note += 12;
    for (; oct < 0; ++oct)
        note -= 12;
    if (note < 0)
        note = 0;
    if (note > 127)
        note = 127;
    return (uint8_t)note;
}

/* The degree code nearest to a played note (the inverse of harm_note). */
uint8_t harm_code(uint8_t base, uint8_t note, uint8_t follow)
{
    static __xdata int16_t rel, total;
    static __xdata int8_t oct;
    static __xdata uint8_t d, best, bd, dist;
    rel = (int16_t)note - (uint8_t)(base - MOD12(base)) - h_key;
    oct = 0;
    best = 0;
    bd = 99;
    while (rel < 0) {
        rel += 12;
        --oct;
    }
    while (rel >= 12) {
        rel -= 12;
        ++oct;
    }
    for (d = 0; d < h_n; ++d) {
        dist = (uint8_t)(rel > h_semis[d] ? rel - h_semis[d] : h_semis[d] - rel);
        if (dist < bd) {
            bd = dist;
            best = d;
        }
    }
    if (12 - rel < bd) {
        best = 0;
        ++oct;
    }
    total = (int16_t)oct * h_n + best;
    if (follow)
        total -= h_deg;
    oct = 0;
    while (total < 0) {
        total += h_n;
        --oct;
    }
    while (total >= h_n) {
        total -= h_n;
        ++oct;
    }
    if (oct < -4)
        oct = -4;
    if (oct > 3)
        oct = 3;
    return DEG(oct, (uint8_t)total);
}

static uint8_t pc_of_deg(uint8_t d)
{
    while (d >= h_n)
        d -= h_n;
    return (uint8_t)((h_key + h_semis[d]) % 12);
}

/* Pitch classes of the current chord: stacked scale thirds on the root degree, or custom chord. */
uint16_t harm_chord_pcs(uint8_t sev)
{
    uint16_t m = 0;
    uint8_t i, n = sev ? 4 : 3;
    if (ovr_pcs)
        return ovr_pcs;
    for (i = 0; i < n; ++i)
        m |= (uint16_t)1 << pc_of_deg((uint8_t)(h_deg + 2 * i));
    return m;
}

uint8_t harm_root_pc(void)
{
    return ovr_pcs ? ovr_root : pc_of_deg(h_deg);
}

/* 0 major, 1 minor, 2 diminished. */
uint8_t harm_minor(void)
{
    uint8_t r = harm_root_pc(), t, f;
    if (ovr_pcs) {
        t = (ovr_pcs & ((uint16_t)1 << MOD12(r + 3))) ? 3 : 4;
        f = (ovr_pcs & ((uint16_t)1 << MOD12(r + 6))) ? 6 : 7;
    } else {
        t = MOD12(pc_of_deg((uint8_t)(h_deg + 2)) + 12 - r);
        f = MOD12(pc_of_deg((uint8_t)(h_deg + 4)) + 12 - r);
    }
    if (t == 3)
        return f == 6 ? 2 : 1;
    return 0;
}

__xdata uint8_t vl_base, vl_sev;

/* Root-position voicing above vl_base: root, third, fifth, then the seventh or the octave. */
void vl_init(__xdata uint8_t *v, uint8_t nv)
{
    static __xdata uint8_t i;
    for (i = 0; i < nv; ++i)
        v[i] = harm_note(vl_base, i < 3 ? DEG(0, 2 * i) : (vl_sev ? DEG(0, 6) : DEG(1, 0)), 1, 0);
}

static __xdata uint8_t vl_out[4], vl_lo, vl_hi;
static __xdata uint16_t vl_mask;

static uint8_t vl_used(uint8_t upto, uint8_t m)
{
    uint8_t j;
    for (j = 0; j < upto; ++j)
        if (vl_out[j] == m)
            return 1;
    return 0;
}

static __xdata uint8_t vl_cand[27], vl_nc;

/* vl_cand: the pitches of vl_lo..vl_hi whose pitch class is in vl_mask. */
static void vl_cands(void)
{
    uint8_t pc = MOD12(vl_lo), m, n = 0;
    uint16_t bit = (uint16_t)1 << pc;
    for (m = vl_lo; m <= vl_hi; ++m) {
        if (vl_mask & bit)
            vl_cand[n++] = m;
        if (++pc == 12) {
            pc = 0;
            bit = 1;
        } else {
            bit <<= 1;
        }
    }
    vl_nc = n;
}

/* The nearest candidate to c, less than an octave away, that vl_out[0..upto) does not
 * hold; ties go down. c itself when there is none. */
static uint8_t vl_near(uint8_t c, uint8_t upto)
{
    uint8_t k, m, d, bd = 12, best = c;
    for (k = 0; k < vl_nc; ++k) {
        m = vl_cand[k];
        d = m > c ? (uint8_t)(m - c) : (uint8_t)(c - m);
        if (d > bd || d >= 12 || (d == bd && m > best) || vl_used(upto, m))
            continue;
        bd = d;
        best = m;
    }
    return best;
}

/* Each voice moves to the nearest free pitch of the new chord (ties go down), within an
 * octave and a half around vl_base. Then a chord tone no voice took replaces a voice that
 * doubles another's pitch class. */
void vl_lead(__xdata uint8_t *v, uint8_t nv)
{
    static __xdata uint8_t i, j, pc, m, n;
    static __xdata uint8_t vpc[4];
    static __xdata uint16_t have, pcs, bit;
    static __xdata uint8_t *__xdata vv;
    vv = v;
    n = nv;
    pcs = harm_chord_pcs(vl_sev);
    vl_lo = vl_base > 7 ? vl_base - 7 : 0;
    vl_hi = vl_base < 108 ? vl_base + 19 : 127;
    have = 0;
    vl_mask = pcs;
    vl_cands();
    for (i = 0; i < n; ++i) {
        vl_out[i] = vl_near(vv[i], i);
        vpc[i] = MOD12(vl_out[i]);
        have |= (uint16_t)1 << vpc[i];
    }
    for (pc = 0, bit = 1; pc < 12; ++pc, bit <<= 1) {
        if (!(pcs & bit) || (have & bit))
            continue;
        for (i = 0; i < n; ++i) {
            for (j = 0; j < n; ++j)
                if (j != i && vpc[j] == vpc[i])
                    break;
            if (j == n)
                continue;
            m = vl_out[i];
            vl_out[i] = 0;
            vl_mask = bit;
            vl_cands();
            vl_out[i] = vl_near(m, n);
            vpc[i] = MOD12(vl_out[i]);
            have |= bit;
            break;
        }
    }
    for (i = 0; i < n; ++i)
        vv[i] = vl_out[i];
}
