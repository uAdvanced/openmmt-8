/* Pattern generator. A lane's grid is a function of its header: role, style, seed, density,
 * Euclid k/n/rotation and length; the same header always gives the same grid. While
 * playing, a new grid is built a few steps per main-loop pass into the lane's next buffer
 * and takes over at the lane's loop point, so the clock never waits for it. */
#include "jam.h"

static __xdata uint16_t g_rs, g_tur;
static __xdata uint8_t g_cnt, g_cls, g_off;
static __xdata uint8_t *__xdata g_h;   /* lane header being generated */
static __xdata uint8_t *__xdata g_out; /* where the next step goes */

static const uint8_t __code arp_updn[10] = {0, 1, 2, 3, 4, 5, 4, 3, 2, 1};
static const uint8_t __code arp_conv[6] = {0, 5, 1, 4, 2, 3};

uint8_t rng8(__xdata uint16_t *s)
{
    uint16_t x = *s;
    x ^= x << 7;
    x ^= x >> 9;
    x ^= x << 8;
    *s = x;
    return (uint8_t)(x ^ (x >> 8));
}

#ifdef JAM8_HOST
uint16_t mul8(uint8_t a, uint8_t b)
{
    return (uint16_t)a * b;
}
#else
uint16_t mul8(uint8_t a, uint8_t b) __naked
{
    a;
    b;
    __asm
        mov     a, dpl
        mov     b, _mul8_PARM_2
        mul     ab
        mov     dpl, a
        mov     dph, b
        ret
    __endasm;
}
#endif

/* Bjorklund-equivalent spacing: step i of k pulses spread over n, rotated. */
uint8_t euclid_hit(uint8_t i, uint8_t k, uint8_t n, uint8_t rot)
{
    uint8_t t;
    if (!n || !k)
        return 0;
    if (k > n)
        k = n;
    t = (uint8_t)(((uint16_t)i + rot) % n);
    return (uint8_t)(((uint16_t)t * k) % n) < k;
}

void gen_begin(__xdata uint8_t *h)
{
    g_h = h;
    g_rs = (((uint16_t)h[H_SEED] << 8) | h[H_STYLE]) ^ ((uint16_t)h[H_ROLE] << 4) ^
           ((uint16_t)h[H_NOTE] << 11) ^ 0x5A3Cu;
    if (!g_rs)
        g_rs = 1;
    rng8(&g_rs);
    rng8(&g_rs);
    g_tur = g_rs ^ ((uint16_t)h[H_TUR] | ((uint16_t)h[H_TUR + 1] << 8));
    g_off = rng8(&g_rs) & 7;
    g_cnt = 0;
    g_cls = drum_class(h[H_NOTE]);
}

static uint8_t arp_code(uint8_t p)
{
    return DEG(p >= 3 ? 1 : 0, (p >= 3 ? p - 3 : p) * 2);
}

static const uint8_t __code chord_arp[8] = {
    DEG(0, 0), DEG(0, 2), DEG(0, 4), DEG(0, 6), DEG(1, 0), DEG(1, 2), DEG(0, 6), DEG(0, 4),
};

/* Step i of g_h's grid, written at g_out, which then moves on. */
static void gen_step(uint8_t i)
{
    static __xdata uint8_t s, st, dens, hit, acc, tie, rat, cond, code, vel, r, g;
    static __xdata uint8_t *__xdata h;
    h = g_h;
    s = i & 15;
    st = h[H_STYLE];
    dens = h[H_DENS];
    hit = acc = tie = rat = vel = 0;
    code = h[H_ROLE] == R_DRUM ? 0 : DEG(0, 0);
    cond = C_ALWAYS;
    r = rng8(&g_rs);
    switch (h[H_ROLE]) {
    case R_DRUM:
        if (st >= DS_EUCLID) {
            hit = euclid_hit(i, h[H_EK], h[H_EN], h[H_EROT]);
            acc = euclid_hit(i, (uint8_t)((h[H_EK] + 1) >> 1), h[H_EN], h[H_EROT]);
        } else {
            hit = (uint8_t)(drum_hits[st][g_cls] >> s) & 1;
            acc = (uint8_t)(drum_acc[st][g_cls] >> s) & 1;
        }
        if (g_cls == DC_CHAT || g_cls == DC_OHAT)
            cond = C_NOFILL;
        if (st == DS_TRAP && g_cls == DC_CHAT && hit && r < 70)
            rat = (uint8_t)(1 + (r & 1));
        break;
    case R_BASS:
        switch (st) {
        case BS_ROOT8:
            hit = !(s & 1);
            acc = !(s & 3);
            break;
        case BS_OFFBEAT:
            hit = (s & 3) == 2;
            if (r < 60)
                code = DEG(1, 0);
            break;
        case BS_OCTAVE:
            hit = 1;
            code = (s & 1) ? DEG(1, 0) : DEG(0, 0);
            acc = !(s & 3);
            break;
        case BS_ACID:
            hit = r >= 64;
            code = acid_deg[rng8(&g_rs) & 7];
            r = rng8(&g_rs);
            acc = r < 80;
            tie = (r & 0x3F) < 16;
            break;
        case BS_WALK:
            hit = !(s & 3) || ((s & 3) == 3 && r < 70);
            code = walk_deg[(uint8_t)((i >> 2) + g_off + ((s & 3) == 3)) & 7];
            acc = !(s & 3);
            break;
        default:
            hit = euclid_hit(i, h[H_EK], h[H_EN], h[H_EROT]);
            code = (r & 3) == 3 ? DEG(1, 0) : (r & 3) == 2 ? DEG(0, 4) : DEG(0, 0);
            break;
        }
        break;
    case R_CHORD:
        switch (st) {
        case CS_STAB:
            hit = (s & 3) == 2;
            break;
        case CS_PAD:
            hit = !s;
            tie = 1;
            break;
        case CS_STRUM:
            hit = s == 0 || s == 6 || s == 12;
            acc = !s;
            break;
        default:
            hit = euclid_hit(i, h[H_EK], h[H_EN], h[H_EROT]);
            break;
        }
        break;
    default: /* R_LEAD */
        switch (st) {
        case LS_TURING:
            hit = (g_tur & 0x30) != 0;
            code = DEG((g_tur >> 3) & 1, g_tur & 7);
            tie = (g_tur & 0xC0) == 0xC0;
            g_tur = (g_tur >> 1) | (g_tur << 15);
            break;
        case LS_EUCLID:
            hit = euclid_hit(i, h[H_EK], h[H_EN], h[H_EROT]);
            code = DEG(0, r & 7);
            break;
        default:
            hit = 1;
            acc = !(s & 3);
            break;
        }
        break;
    }

    /* Density 5 is the style as written; lower thins the weakest steps first, higher adds
     * ghost notes on empty steps and, from 8, ratchets. */
    g = metric_w[s];
    if (dens < 5) {
        if (hit && g < 5 - dens && !(h[H_ROLE] == R_CHORD && st == CS_PAD))
            hit = 0;
    } else if (dens > 5 && !hit && h[H_ROLE] != R_CHORD) {
        g = h[H_ROLE] == R_DRUM ? drum_ghost[g_cls] : 30;
        if (rng8(&g_rs) < (uint8_t)((dens - 5) * g)) {
            hit = 1;
            vel = (uint8_t)((h[H_VEL] * 5u) >> 3);
            acc = tie = 0;
        }
    }
    if (dens >= 8 && hit && !rat && h[H_ROLE] != R_CHORD && rng8(&g_rs) < (uint8_t)((dens - 7) * 30))
        rat = 1;
    if (h[H_ROLE] == R_DRUM && !hit && ((drum_fill[g_cls] >> s) & 1)) {
        hit = 1;
        cond = C_FILL;
        if (g_cls == DC_SNARE && s >= 12)
            rat = 1;
    }
    if (h[H_ROLE] == R_LEAD && st <= LS_CHORD && hit) {
        if (st == LS_CHORD) {
            code = chord_arp[(g_cnt + g_off) & 7];
            ++g_cnt;
        } else {
            switch (st) {
            case LS_UP:
                g = g_cnt % 6;
                break;
            case LS_DOWN:
                g = (uint8_t)(5 - g_cnt % 6);
                break;
            case LS_UPDN:
                g = arp_updn[g_cnt % 10];
                break;
            case LS_RAND:
                g = r % 6;
                break;
            default:
                g = arp_conv[g_cnt % 6];
                break;
            }
            code = arp_code(g);
            ++g_cnt;
        }
    }
    g_out[0] = hit ? (uint8_t)(S_ON | (acc ? S_ACC : 0) | (tie ? S_TIE : 0) | (rat << S_RAT_SHIFT) |
                             (cond << S_COND_SHIFT))
                 : 0;
    g_out[1] = code;
    g_out[2] = vel;
    g_out[3] = 0xF0;
    g_out += 4;
}

void gen_grid(uint8_t lane)
{
    static __xdata uint8_t i, n;
    gen_begin(HDR(lane));
    g_out = GRID(lane);
    n = g_h[H_LEN];
    for (i = 0; i < n; ++i)
        gen_step(i);
}

#ifndef JAM8_HOST
static __xdata uint8_t gen_req, g_lane, g_i;
__xdata uint8_t gen_ready;
#define NO_LANE 0xFFu

void gen_request(uint8_t lane)
{
    gen_req |= (uint8_t)(1 << lane);
    gen_ready &= (uint8_t)~(1 << lane);
    if (g_lane == lane)
        g_lane = NO_LANE;
}

void gen_cancel(void)
{
    gen_req = gen_ready = 0;
    g_lane = NO_LANE;
}

/* A few steps per call. */
void gen_slice(void)
{
    static __xdata uint8_t n, bit;
    static __xdata uint8_t *__xdata h;
    if (g_lane >= NLANES) {
        if (!gen_req)
            return;
        for (g_lane = 0; !(gen_req & (1 << g_lane)); ++g_lane)
            ;
        gen_req &= (uint8_t)~(1 << g_lane);
        g_i = 0;
        gen_begin(HDR(g_lane));
        g_out = NEXTG(g_lane);
    }
    h = HDR(g_lane);
    g_h = h;
    for (n = 0; n < 4 && g_i < h[H_LEN]; ++n, ++g_i)
        gen_step(g_i);
    if (g_i < h[H_LEN])
        return;
    bit = (uint8_t)(1 << g_lane);
    if (playing && lr[g_lane].step != 0xFF)
        gen_ready |= bit;
    else
        xcopy(cur_base + SCENE_GRIDS + (uint16_t)g_lane * GRID_SIZE, NEXT_BASE + (uint16_t)g_lane * GRID_SIZE,
              (uint16_t)h[H_LEN] * 4);
    g_lane = NO_LANE;
}
#endif

static __xdata uint16_t mut_rs;

static uint8_t mut_rnd(void)
{
    if (!mut_rs)
        mut_rs = 0xACE1u;
    return rng8(&mut_rs);
}

/* New seed (or, for a Turing lane, one flipped register bit); the old one goes into the
 * lane's 8-entry history. Bit 7 of a history entry marks a flipped bit. */
void lane_mutate(uint8_t lane)
{
    static __xdata uint8_t *__xdata h;
    static __xdata uint8_t r, e, i;
    h = HDR(lane);
    r = mut_rnd();
    i = h[H_HIDX] & 7;
    if (h[H_ROLE] == R_LEAD && h[H_STYLE] == LS_TURING) {
        e = (uint8_t)(0x80 | (r & 15));
        if (e & 8)
            h[H_TUR + 1] ^= (uint8_t)(1 << (e & 7));
        else
            h[H_TUR] ^= (uint8_t)(1 << (e & 7));
    } else {
        e = h[H_SEED];
        h[H_SEED] = (uint8_t)((e + 1 + r % 98) % 100);
    }
    h[H_HIST + i] = e;
    h[H_HIDX] = (uint8_t)((i + 1) & 7);
    h[H_FLAGS] &= (uint8_t)~F_CAPT;
#ifndef JAM8_HOST
    gen_request(lane);
#endif
}

void lane_unmutate(uint8_t lane)
{
    __xdata uint8_t *h = HDR(lane);
    uint8_t i = (uint8_t)((h[H_HIDX] - 1) & 7), e = h[H_HIST + i];
    if (e & 0x80) {
        if (e & 8)
            h[H_TUR + 1] ^= (uint8_t)(1 << (e & 7));
        else
            h[H_TUR] ^= (uint8_t)(1 << (e & 7));
    } else {
        h[H_SEED] = e;
    }
    h[H_HIDX] = i;
#ifndef JAM8_HOST
    gen_request(lane);
#endif
}
