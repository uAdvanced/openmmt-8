/* Play engine: eight lanes, each a loop of 1-64 steps with its own step length, on one
 * 96-PPQN transport. A step that plays schedules its note-ons and note-offs (swing, delay,
 * ratchets, strum) in a 32-entry event queue served every tick. */
#include "jam.h"

__xdata lane_rt lr[NLANES];
__bit playing, fill;
__xdata uint16_t tp_tick, tp_bar, scene_bar;
__xdata uint8_t tp_bt, mute_pend, queued, beat_flag, chain_pos, chain_rep;
__xdata uint8_t mom_mute, mom_solo, solo_latch, solo_pend, stutter, stutter_anchor;
static __xdata uint8_t swing_t[DIV_N];
static __xdata uint16_t rs;
static __xdata uint8_t preroll; /* ticks before the transport starts after PLAY */
static __bit first_tick;
#define NONE 0xFFu

/* A lane works out its step LEAD_T(l) ticks before the step is due and queues the notes that
 * far ahead, so the work of the eight lanes spreads over eight ticks while the notes still
 * leave on time. Bar and beat changes are applied LEAD ticks early to match, and PLAY
 * starts the transport after LEAD ticks of pre-roll. */
#define LEAD 8u
#define LEAD_T(l) ((uint8_t)(LEAD - (l)))

#define EQN 32u
#define DUEN 16u
static __xdata uint16_t eq_d[EQN]; /* tick the entry is due, against eq_now */
static __xdata uint8_t eq_s[EQN], eq_n[EQN], eq_v[EQN], eq_q[EQN], eq_l[EQN];
static __xdata uint8_t e_lane; /* lane that eq_add stamps on each new entry */
static __xdata uint8_t eq_cnt; /* entries 0 to eq_cnt - 1 are pending */
static __xdata uint8_t eq_seq; /* eq_q: order of queueing */
static __xdata uint16_t eq_now;

static uint8_t rnd(void)
{
    return rng8(&rs);
}

void note_now(uint8_t st, uint8_t n, uint8_t v)
{
    midi_tx(st);
    midi_tx(n);
    midi_tx(v);
}

/* ---- event queue: pending entries packed at the front, so loops cover only those ---- */

static void eq_add(uint8_t st, uint8_t n, uint8_t v, uint16_t d)
{
    uint8_t i = eq_cnt;
    if (i >= EQN)
        return;
    eq_s[i] = st;
    eq_n[i] = n;
    eq_v[i] = v;
    eq_d[i] = eq_now + d;
    eq_q[i] = eq_seq++;
    eq_l[i] = e_lane;
    eq_cnt = i + 1;
}

/* The pending note-off of ch/n still sounding t ticks from now, or NONE. */
static uint8_t eq_find_off(uint8_t st, uint8_t n, uint16_t t)
{
    uint8_t i;
    for (i = 0; i < eq_cnt; ++i)
        if (eq_s[i] == st && eq_n[i] == n && eq_l[i] == e_lane && (uint16_t)(eq_d[i] - eq_now) >= t)
            return i;
    return NONE;
}

/* Removes entry i: the last entry takes its place. */
static void eq_del(uint8_t i)
{
    uint8_t last = --eq_cnt;
    eq_s[i] = eq_s[last];
    eq_n[i] = eq_n[last];
    eq_v[i] = eq_v[last];
    eq_d[i] = eq_d[last];
    eq_q[i] = eq_q[last];
    eq_l[i] = eq_l[last];
}

/* End sounding notes of these lanes now, and drop the ones not yet played. */
static void eq_silence(uint8_t mask)
{
    static __xdata uint8_t i;
    for (i = 0; i < eq_cnt; ++i)
        if ((mask & (uint8_t)(1 << eq_l[i])) && (eq_s[i] & 0xF0) == 0x80)
            note_now(eq_s[i], eq_n[i], 0x40);
    for (i = eq_cnt; i--;)
        if (mask & (uint8_t)(1 << eq_l[i]))
            eq_del(i);
}

/* Note-offs due now go out before note-ons due now, so a retrigger is never cut. Note-ons
 * due together leave in the order they were queued: lane 1 first. A sent entry is marked
 * with status 0 and dropped afterwards. */
static void eq_process(void)
{
    static __xdata uint8_t i, j, k, nd, sent;
    static __xdata uint8_t due[DUEN], age[DUEN];
    nd = sent = 0;
    for (i = 0; i < eq_cnt; ++i) {
        if (eq_d[i] != eq_now)
            continue;
        if (eq_s[i] < 0x90) {
            note_now(eq_s[i], eq_n[i], 0x40);
            eq_s[i] = 0;
            sent = 1;
        } else if (nd < DUEN) {
            k = (uint8_t)(eq_seq - eq_q[i]);
            for (j = nd++; j && age[j - 1] < k; --j) {
                age[j] = age[j - 1];
                due[j] = due[j - 1];
            }
            age[j] = k;
            due[j] = i;
        } else {
            ++eq_d[i];
        }
    }
    for (j = 0; j < nd; ++j) {
        i = due[j];
        note_now(eq_s[i], eq_n[i], eq_v[i]);
        eq_s[i] = 0;
    }
    if (nd || sent)
        for (i = eq_cnt; i--;)
            if (!eq_s[i])
                eq_del(i);
    ++eq_now;
}

/* Stop: every note sounding ends now, notes not yet played are dropped. */
void eq_flush(void)
{
    uint8_t i;
    for (i = 0; i < eq_cnt; ++i)
        if ((eq_s[i] & 0xF0) == 0x80)
            note_now(eq_s[i], eq_n[i], 0x40);
    eq_cnt = 0;
}

/* The step being played, for emit(). */
static __xdata uint8_t e_ch, e_vel, e_rat, e_iv, e_tie;
static __xdata uint16_t e_d, e_gate;

/* One note of a step: e_rat hits e_iv ticks apart from delay e_d, each e_gate long. A tied
 * step whose note still sounds holds it on instead of striking it again. */
static void emit(uint8_t n, uint8_t extra)
{
    static __xdata uint8_t k, off, e;
    static __xdata uint16_t d;
    off = (uint8_t)(0x80 | e_ch);
    d = e_d + extra;
    for (k = 0; k < e_rat; ++k, d += e_iv) {
        e = eq_find_off(off, n, d);
        if (e != NONE) {
            if (e_tie && !k) {
                eq_d[e] = eq_now + d + e_gate;
                continue;
            }
            eq_d[e] = eq_now + d;
        }
        if (eq_cnt > EQN - 2)
            return;
        eq_add((uint8_t)(0x90 | e_ch), n, e_vel, d);
        eq_add(off, n, 0, d + e_gate);
    }
}

/* Steps from st to the lane's next step that plays (the whole loop when none does). */
static uint8_t steps_to_next(__xdata uint8_t *g, uint8_t st, uint8_t len, uint8_t rot)
{
    uint8_t k, idx = (uint8_t)(st + rot);
    for (k = 1; k < len; ++k) {
        if (++idx >= len)
            idx -= len;
        if (g[(uint16_t)idx * 4] & S_ON)
            return k;
    }
    return len;
}

static void lane_wrap(uint8_t l)
{
    __xdata uint8_t *h = HDR(l);
    uint8_t bit = (uint8_t)(1 << l);
    ++lr[l].loops;
    if (gen_ready & bit) {
        gen_ready &= (uint8_t)~bit;
        xcopy(cur_base + SCENE_GRIDS + (uint16_t)l * GRID_SIZE, NEXT_BASE + (uint16_t)l * GRID_SIZE,
              (uint16_t)h[H_LEN] * 4);
    }
    if (h[H_EVOL] && !(h[H_FLAGS] & F_CAPT) && (uint8_t)(rnd() % (uint8_t)100u) < h[H_EVOL])
        lane_mutate(l);
}

/* Momentary mute, then momentary solo, then a latched solo, then the stored mute.
 * A soloed lane plays even when its stored mute is on. */
uint8_t lane_audible(uint8_t l)
{
    uint8_t bit = (uint8_t)(1 << l);
    if (mom_mute & bit)
        return 0;
    if (mom_solo)
        return (mom_solo & bit) != 0;
    if (solo_latch)
        return (solo_latch & bit) != 0;
    return (HDR(l)[H_FLAGS] & F_MUTE) == 0;
}

static uint8_t audible_mask(void)
{
    static __xdata uint8_t l, m;
    m = 0;
    for (l = 0; l < NLANES; ++l)
        if (lane_audible(l))
            m |= (uint8_t)(1 << l);
    return m;
}

static void silence_dropped(uint8_t before)
{
    uint8_t drop = (uint8_t)(before & (uint8_t)~audible_mask());
    if (drop)
        eq_silence(drop);
}

static uint8_t sync_vl(__xdata uint8_t *h, __xdata lane_rt *r)
{
    uint8_t nv = h[H_VOICES] == 4 ? 4 : 3;
    if (r->nv == nv && r->vid == h_id)
        return 0;
    vl_base = h[H_NOTE];
    vl_sev = h[H_FLAGS] & F_SEV;
    if (r->nv != nv) {
        vl_init(r->v, nv);
        r->nv = nv;
    } else {
        vl_lead(r->v, nv);
    }
    r->vid = h_id;
    return 1;
}

static void lane_fire(uint8_t l)
{
    static __xdata uint8_t len, st, rot, div, b0, b1, b2, b3, nv, i, hum, ld, ps;
    static __xdata int16_t v;
    static __xdata uint8_t *__xdata h, *__xdata g, *__xdata vp;
    static __xdata lane_rt *__xdata r;

    e_lane = l;
    h = HDR(l);
    r = &lr[l];
    div = div_ticks[h[H_DIV] < DIV_N ? h[H_DIV] : DIV_16];
    ld = LEAD_T(l);
    r->cnt = div;
    len = h[H_LEN];
    if (!len || len > MAXLEN)
        len = h[H_LEN] = 16;
    st = (uint8_t)(r->step + 1);
    if (st >= len) {
        st = 0;
        if (r->step != 0xFF)
            lane_wrap(l);
    }
    r->step = st;
    if (r->cap && r->cap != 0xFF) {
        if (r->cap_held && r->cap_st < len && r->cap_st != st) {
            GRID(l)[(uint16_t)r->cap_st * 4] |= S_TIE;
            r->cap_st = NONE;
        }
        if (!--r->cap) {
            r->cap_held = 0;
            ui_dirty = 1;
        }
    }
    if (stutter)
        st = (uint8_t)((stutter_anchor + (stutter > 1 ? (st & 1) : 0)) % len);
    rot = h[H_ROT];
    if (rot >= len)
        rot %= len;
    i = (uint8_t)(st + rot);
    if (i >= len)
        i -= len;
    g = GRID(l);
    b0 = g[(uint16_t)i * 4];
    ps = r->pstat;
    r->pstat = 0;
    if (!(b0 & S_ON) || !lane_audible(l))
        return;
    switch (b0 >> S_COND_SHIFT) {
    case C_FILL:
        if (!fill)
            return;
        break;
    case C_NOFILL:
        if (fill)
            return;
        break;
    case C_1OF2:
        if (ps ? ps != 1 : (r->loops & 1))
            return;
        break;
    case C_2OF2:
        if (ps ? ps != 2 : !(r->loops & 1))
            return;
        break;
    case C_1OF4:
        if (r->loops & 3)
            return;
        break;
    case C_4OF4:
        if ((r->loops & 3) != 3)
            return;
        break;
    case C_FIRST:
        if (r->loops)
            return;
        break;
    }
    b1 = g[(uint16_t)i * 4 + 1];
    b2 = g[(uint16_t)i * 4 + 2];
    b3 = g[(uint16_t)i * 4 + 3];
    if ((b3 >> 4) != 15) {
        if ((rnd() & 15) > (b3 >> 4)) {
            r->pstat = 2;
            return;
        }
        r->pstat = 1;
    }

    e_d = (b3 & 15) + ld;
    if (st & 1)
        e_d += swing_t[h[H_DIV]];
    v = b2 ? b2 : h[H_VEL];
    if (b0 & S_ACC)
        v += 24;
    hum = h[H_HUM];
    if (hum)
        v += (int16_t)(rnd() % (uint8_t)(mul8(hum, 6) + 1)) - (int16_t)mul8(hum, 3);
    if (v < 1)
        v = 1;
    if (v > 127)
        v = 127;
    e_vel = (uint8_t)v;
    e_rat = (uint8_t)(((b0 & S_RAT) >> S_RAT_SHIFT) + 1);
    e_iv = div / e_rat;
    e_tie = b0 & S_TIE;
    if (e_tie)
        e_gate = mul8(steps_to_next(g, st, len, rot), div) + 2;
    else {
        hum = h[H_GATE] > 99 ? 99 : h[H_GATE];
        e_gate = mul8(e_iv, (uint8_t)(mul8(hum, 164) >> 6)) >> 8;
        if (!e_gate)
            e_gate = 1;
    }
    e_ch = h[H_CHAN] & 15;
    if (h[H_MCC]) {
        hum = h[H_MCC] == 65 ? (e_tie ? 127 : 0) : e_vel;
        eq_add((uint8_t)(0xB0 | e_ch), h[H_MCC], hum, e_d);
    }

    switch (h[H_ROLE]) {
    case R_DRUM:
        emit(b1 ? b1 : h[H_NOTE], 0);
        break;
    case R_CHORD:
        if ((b1 & 0xF0) == 0x40) {
            nv = b1 & 7;
            vp = CCAP(cfg.scene, i);
        } else {
            sync_vl(h, r);
            nv = r->nv;
            vp = r->v;
        }
        for (i = 0; i < nv; ++i) {
            if ((b1 & 0xF0) != 0x40 && (b1 & 15) && !((b1 >> i) & 1))
                continue;
            v = (int16_t)vp[i] + (int8_t)h[H_TRANS];
            if ((b1 & 0xF0) != 0x40 && (h[H_FLAGS] & F_OPEN) && i == 1)
                v += 12;
            if (v < 0 || v > 127)
                continue;
            emit((uint8_t)v, h[H_STYLE] == CS_STRUM ? (uint8_t)(i + i + i) : 0);
        }
        break;
    default:
        emit(harm_note(h[H_NOTE], b1, h[H_FLAGS] & F_FOLLOW, (int8_t)h[H_TRANS]), 0);
        break;
    }
}

/* ---- transport ---- */

/* Lanes reset LEAD ticks before their first step is due: in the pre-roll after PLAY, or
 * at a scene change, which is applied LEAD ticks before the bar. */
static void lanes_reset(void)
{
    uint8_t l;
    for (l = 0; l < NLANES; ++l) {
        lr[l].cnt = (uint8_t)(LEAD + 1 - LEAD_T(l));
        lr[l].step = lr[l].cap_st = 0xFF;
        lr[l].loops = lr[l].nv = lr[l].pstat = lr[l].cap_held = 0;
    }
}

static void apply_scene(uint8_t s)
{
    uint8_t l;
    scene_select(s);
    gen_cancel();
    lanes_reset();
    for (l = 0; l < NLANES; ++l)
        lr[l].cap = 0;
    scene_bar = 0;
    queued = NONE;
    mute_pend = 0;
    solo_latch = 0;
    solo_pend = 0;
    ui_dirty = 1;
}

static uint8_t chain_scene(uint8_t pos)
{
    return XMEM(CHAIN_BASE)[pos * 2];
}

static void chain_advance(void)
{
    uint8_t rep = XMEM(CHAIN_BASE)[chain_pos * 2 + 1];
    if (++chain_rep < (rep ? rep : 1))
        return;
    chain_rep = 0;
    if (++chain_pos >= CHAIN_LEN || !chain_scene(chain_pos))
        chain_pos = 0;
    if (chain_scene(chain_pos))
        queued = (uint8_t)(chain_scene(chain_pos) - 1);
}

/* Stored mutes and the latched solo, at the launch point. Notes already sounding on a
 * lane that this silences are cut here, not when the key was pressed. */
static void launch_apply(void)
{
    static __xdata uint8_t l, before;
    if (!mute_pend && !solo_pend)
        return;
    before = audible_mask();
    for (l = 0; l < NLANES; ++l)
        if (mute_pend & (1 << l))
            HDR(l)[H_FLAGS] ^= F_MUTE;
    mute_pend = 0;
    solo_latch ^= solo_pend;
    solo_pend = 0;
    silence_dropped(before);
    ui_dirty = 1;
}

void engine_swing(void)
{
    uint8_t d, s = cfg.swing;
    if (s < 50)
        s = 50;
    if (s > 75)
        s = 75;
    for (d = 0; d < DIV_N; ++d)
        swing_t[d] = (d == DIV_32 || d == DIV_16 || d == DIV_8)
                         ? (uint8_t)((uint16_t)div_ticks[d] * (s - 50) / 50)
                         : 0;
}

void engine_init(void)
{
    rs = 0x2F6Bu;
    queued = NONE;
    mom_mute = mom_solo = solo_latch = solo_pend = stutter = 0;
    gen_cancel();
    lanes_reset();
    engine_swing();
}

void engine_set_stutter(uint8_t mode)
{
    if (mode == stutter)
        return;
    if (mode && !stutter)
        stutter_anchor = lr[0].step == 0xFF ? 0 : lr[0].step;
    stutter = mode;
    ui_dirty = 1;
}

void engine_start(uint8_t cont)
{
    uint8_t l;
    if (playing)
        engine_stop();
    if (!cont) {
        tp_tick = tp_bar = scene_bar = 0;
        tp_bt = 0;
        lanes_reset();
        if (cfg.chain_on && chain_scene(0)) {
            chain_pos = chain_rep = 0;
            apply_scene((uint8_t)(chain_scene(0) - 1));
        }
        first_tick = 1;
        preroll = LEAD;
        if (cfg.sync)
            tp_ticks += LEAD;
    }
    for (l = 0; l < NLANES; ++l)
        if (lr[l].cap == 0xFF)
            lr[l].cap = HDR(l)[H_LEN];
    if (cfg.clk_out && !cfg.sync)
        midi_tx(cont ? 0xFB : 0xFA);
    playing = 1;
    ui_dirty = 1;
}

void engine_stop(void)
{
    playing = 0;
    eq_flush();
    if (cfg.clk_out && !cfg.sync)
        midi_tx(0xFC);
    ui_dirty = 1;
}

void engine_toggle_mute(uint8_t lane)
{
    if (!playing || cfg.quant == Q_STEP)
        HDR(lane)[H_FLAGS] ^= F_MUTE;
    else
        mute_pend ^= (uint8_t)(1 << lane);
    ui_dirty = 1;
}

void engine_toggle_solo(uint8_t lane)
{
    uint8_t bit = (uint8_t)(1 << lane), before;
    if (!playing || cfg.quant == Q_STEP) {
        before = audible_mask();
        solo_latch ^= bit;
        silence_dropped(before);
    } else {
        solo_pend ^= bit;
    }
    ui_dirty = 1;
}

void engine_set_mom_mute(uint8_t mask)
{
    uint8_t before;
    if (mask == mom_mute)
        return;
    before = audible_mask();
    mom_mute = mask;
    silence_dropped(before);
    ui_dirty = 1;
}

void engine_set_mom_solo(uint8_t mask)
{
    uint8_t before;
    if (mask == mom_solo)
        return;
    before = audible_mask();
    mom_solo = mask;
    silence_dropped(before);
    ui_dirty = 1;
}

void engine_arm(uint8_t lane)
{
    __xdata lane_rt *r = &lr[lane];
    r->cap = r->cap ? 0 : playing ? HDR(lane)[H_LEN] : 0xFF;
    ui_dirty = 1;
}

void engine_queue_scene(uint8_t s)
{
    if (s >= NSCENES)
        return;
    if (!playing)
        apply_scene(s);
    else
        queued = s;
    ui_dirty = 1;
}

/* A played note goes into every lane armed for capture, on the nearest step. */
uint8_t engine_note_in(uint8_t note, uint8_t vel)
{
    static __xdata uint8_t l, st, len, div, i, tb, dly, nv;
    static __xdata uint8_t *__xdata h, *__xdata g, *__xdata cc;
    uint8_t took = 0;
    for (l = 0; l < NLANES; ++l) {
        if (!lr[l].cap || lr[l].cap == 0xFF)
            continue;
        took = 1;
        h = HDR(l);
        len = h[H_LEN];
        div = div_ticks[h[H_DIV]];
        st = lr[l].step == 0xFF ? 0 : lr[l].step;
        dly = 0;
        /* tb: ticks until the step after lr.step is due. More than div means lr.step has
         * been worked out early but is not due yet. */
        tb = (uint8_t)(lr[l].cnt + LEAD_T(l));
        if (tb > div) {
            if ((uint8_t)(tb - div) > (div >> 1))
                st = st ? st - 1 : len - 1;
        } else if (tb <= (div >> 1)) {
            if (++st >= len)
                st = 0;
        } else if (cfg.quant == Q_STEP) {
            dly = (uint8_t)(div - tb);
            if (dly > 15)
                dly = 15;
        }
        i = (uint8_t)(st + h[H_ROT] % len);
        if (i >= len)
            i -= len;
        g = GRID(l) + (uint16_t)i * 4;
        g[0] = S_ON;
        switch (h[H_ROLE]) {
        case R_DRUM:
            g[1] = note == h[H_NOTE] ? 0 : note;
            break;
        case R_CHORD:
            cc = CCAP(cfg.scene, i);
            nv = (lr[l].cap_st == i && (g[1] & 0xF0) == 0x40) ? (g[1] & 7) : 0;
            if (nv < 4) {
                cc[nv++] = note;
                g[1] = (uint8_t)(0x40 | nv);
            }
            break;
        default:
            g[1] = harm_code(h[H_NOTE], (uint8_t)(note - (int8_t)h[H_TRANS]), h[H_FLAGS] & F_FOLLOW);
            break;
        }
        g[2] = vel;
        g[3] = (uint8_t)(0xF0 | dly);
        lr[l].cap_st = i;
        ++lr[l].cap_held;
        h[H_FLAGS] |= F_CAPT;
        gen_ready &= (uint8_t)~(1 << l);
    }
    return took;
}

void engine_note_off(uint8_t note)
{
    uint8_t l;
    note;
    for (l = 0; l < NLANES; ++l)
        if (lr[l].cap_held)
            --lr[l].cap_held;
}

/* Pre-computes chord voice-leading in background passes so chord changes never stall clock ticks. */
void engine_slice(void)
{
    uint8_t l;
    __xdata uint8_t *h;
    if (!playing)
        return;
    for (l = 0; l < NLANES; ++l) {
        h = HDR(l);
        if (h[H_ROLE] == R_CHORD && sync_vl(h, &lr[l]))
            return;
    }
}

/* Changes for the bar about to start, applied before any lane works out its first step. */
static void bar_start(uint16_t bar)
{
    uint8_t bars = cfg.bars[cfg.scene];
    if (scene_bar && !(scene_bar % (bars ? bars : 1))) {
        if (cfg.chain_on)
            chain_advance();
        if (queued != NONE)
            apply_scene(queued);
    }
    harm_apply_pending(Q_BAR);
    harm_bar(bar);
    if (cfg.quant == Q_BAR)
        launch_apply();
    ++scene_bar;
}

static void beat_start(void)
{
    harm_apply_pending(Q_BEAT);
    if (cfg.quant == Q_BEAT)
        launch_apply();
}

static void lanes_tick(void)
{
    uint8_t l;
    for (l = 0; l < NLANES; ++l)
        if (!--lr[l].cnt)
            lane_fire(l);
    eq_process();
}

void engine_tick(void)
{
    if (!playing)
        return;
    if (first_tick) {
        first_tick = 0;
        bar_start(0);
        beat_start();
    }
    if (preroll) {
        --preroll;
        lanes_tick();
        return;
    }
    if (tp_tick == BAR_TICKS - LEAD)
        bar_start(tp_bar + 1);
    if (tp_bt == PPQN - LEAD)
        beat_start();
    if (!tp_bt) {
        if (cfg.click) {
            P3_5 = !P3_5;
            P3_5 = !P3_5;
        }
        beat_flag = 1;
        ui_dirty = 1;
    }
    if (cfg.clk_out && !cfg.sync && !(tp_bt & 3))
        midi_rt();
    lanes_tick();
    if (++tp_bt == PPQN)
        tp_bt = 0;
    if (++tp_tick == BAR_TICKS) {
        tp_tick = 0;
        ++tp_bar;
    }
}
