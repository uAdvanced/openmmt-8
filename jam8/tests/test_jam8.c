/* Host unit tests for the pure JAM-8 modules (gen.c, harmony.c, tables.c), built with gcc
 * and -DJAM8_HOST so xdata is a plain array. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jam.h"

uint8_t host_xram[0x10000];

/* Stubs for the hardware side of scene.c. */
static uint8_t *tx_buf;
static uint32_t tx_n, tx_cap;
static const char *last_msg;
uint8_t playing, fill;
void midi_tx(uint8_t b)
{
    if (tx_n < tx_cap)
        tx_buf[tx_n] = b;
    ++tx_n;
}
uint8_t midi_tx_free(void) { return 200; }
void engine_stop(void) { playing = 0; }
void ui_msg(const char *s, uint8_t lane, uint16_t v) { last_msg = s; }
static uint16_t tempo10;
void tempo_set(uint16_t t) { tempo10 = t; }
uint16_t tempo_get(void) { return ((uint16_t)cfg.tempo_hi << 8) | cfg.tempo_lo; }
void gen_cancel(void) {}

static int fails, checks;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            ++fails;                                                                               \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static void euclid_str(char *out, uint8_t k, uint8_t n, uint8_t rot)
{
    uint8_t i;
    for (i = 0; i < n; ++i)
        out[i] = euclid_hit(i, k, n, rot) ? 'x' : '.';
    out[n] = 0;
}

static void test_euclid(void)
{
    char s[65];
    euclid_str(s, 3, 8, 0);
    CHECK(!strcmp(s, "x..x..x."), "E(3,8) = %s", s);
    euclid_str(s, 3, 8, 1);
    CHECK(!strcmp(s, "..x..x.x"), "E(3,8) rotated by 1 = %s", s);
    euclid_str(s, 5, 8, 0);
    CHECK(!strcmp(s, "x.x.xx.x"), "E(5,8) = %s", s);
    euclid_str(s, 4, 16, 0);
    CHECK(!strcmp(s, "x...x...x...x..."), "E(4,16) = %s", s);
    euclid_str(s, 0, 8, 0);
    CHECK(!strcmp(s, "........"), "E(0,8) = %s", s);
    euclid_str(s, 9, 8, 0);
    CHECK(!strcmp(s, "xxxxxxxx"), "k > n fills every step: %s", s);
    {
        /* Every k of every n up to 32: k hits, gaps differ by at most one step. */
        uint8_t k, n, i, hits, first, last, gmin, gmax, g;
        int ok = 1;
        for (n = 1; n <= 32; ++n)
            for (k = 1; k <= n; ++k) {
                hits = 0;
                first = last = 0xFF;
                gmin = 0xFF;
                gmax = 0;
                for (i = 0; i < n; ++i)
                    if (euclid_hit(i, k, n, 0)) {
                        ++hits;
                        if (last != 0xFF) {
                            g = (uint8_t)(i - last);
                            gmin = g < gmin ? g : gmin;
                            gmax = g > gmax ? g : gmax;
                        } else {
                            first = i;
                        }
                        last = i;
                    }
                g = (uint8_t)(n - last + first);
                gmin = g < gmin ? g : gmin;
                gmax = g > gmax ? g : gmax;
                if (hits != k || gmax - gmin > 1)
                    ok = 0;
            }
        CHECK(ok, "Euclid spacing is even for all k <= n <= 32");
    }
}

static void set_key(uint8_t key, uint8_t scale)
{
    memset(&cfg, 0, sizeof cfg);
    cfg.key = key;
    cfg.scale = scale;
    cfg.quant = Q_BAR;
    harm_init();
}

static void set_degree(uint8_t d)
{
    h_deg = d;
    ++h_id;
}

static int pcs_cover(const uint8_t *v, uint8_t nv, uint16_t pcs)
{
    uint16_t have = 0;
    uint8_t i;
    for (i = 0; i < nv; ++i)
        have |= (uint16_t)1 << (v[i] % 12);
    return have == pcs;
}

static void test_voice_leading(void)
{
    static const uint8_t degs[] = {5, 3, 4, 0}; /* Am, F, G, C */
    static const char *names[] = {"Am", "F", "G", "C"};
    uint8_t v[4], prev[4], i, j, mv;
    set_key(0, 0); /* C major */
    vl_base = 60;
    vl_sev = 0;
    vl_init(v, 3);
    CHECK(v[0] == 60 && v[1] == 64 && v[2] == 67, "C root position = %d %d %d", v[0], v[1], v[2]);
    for (j = 0; j < sizeof degs; ++j) {
        memcpy(prev, v, 3);
        set_degree(degs[j]);
        vl_lead(v, 3);
        mv = 0;
        for (i = 0; i < 3; ++i) {
            uint8_t d = (uint8_t)abs((int)v[i] - (int)prev[i]);
            mv = d > mv ? d : mv;
        }
        CHECK(mv <= 2, "to %s each voice moves at most 2 semitones (moved %d: %d %d %d)", names[j], mv, v[0],
              v[1], v[2]);
        CHECK(pcs_cover(v, 3, harm_chord_pcs(0)), "%s voicing %d %d %d holds every chord tone", names[j], v[0],
              v[1], v[2]);
    }
    set_degree(0);
    vl_init(v, 3);
    set_degree(5);
    vl_lead(v, 3);
    CHECK(v[0] == 60 && v[1] == 64 && v[2] == 69, "C to Am keeps C and E, G goes to A: %d %d %d", v[0], v[1],
          v[2]);
    set_degree(3);
    vl_lead(v, 3);
    CHECK(v[0] == 60 && v[1] == 65 && v[2] == 69, "Am to F moves E to F: %d %d %d", v[0], v[1], v[2]);

    /* Seventh chords, four voices, through a ii-V-I. */
    vl_sev = 1;
    set_degree(1);
    vl_init(v, 4);
    for (j = 0; j < 2; ++j) {
        memcpy(prev, v, 4);
        set_degree(j ? 0 : 4);
        vl_lead(v, 4);
        mv = 0;
        for (i = 0; i < 4; ++i) {
            uint8_t d = (uint8_t)abs((int)v[i] - (int)prev[i]);
            mv = d > mv ? d : mv;
        }
        CHECK(mv <= 2, "ii-V-I sevenths move each voice at most 2 (step %d moved %d)", j, mv);
        CHECK(pcs_cover(v, 4, harm_chord_pcs(1)), "seventh voicing holds all four tones (step %d)", j);
    }
    /* Voices stay inside the lane range. */
    vl_sev = 0;
    vl_base = 60;
    set_degree(0);
    vl_init(v, 3);
    for (j = 0; j < 40; ++j) {
        set_degree((uint8_t)(j * 3 % 7));
        vl_lead(v, 3);
        for (i = 0; i < 3; ++i)
            CHECK(v[i] >= 53 && v[i] <= 79, "voice %d = %d stays in 53-79", i, v[i]);
    }
}

static void test_harmony(void)
{
    uint8_t code, base = 48, n;
    set_key(0, 0);
    set_degree(0);
    CHECK(harm_minor() == 0, "C major I is major");
    set_degree(1);
    CHECK(harm_minor() == 1, "C major ii is minor");
    set_degree(6);
    CHECK(harm_minor() == 2, "C major vii is diminished");
    set_key(9, 1); /* A minor */
    set_degree(0);
    CHECK(harm_root_pc() == 9 && harm_minor() == 1, "A minor i is Am");
    CHECK(harm_note(48, DEG(0, 0), 1, 0) == 57, "A minor root above C3 is A3: %d", harm_note(48, DEG(0, 0), 1, 0));
    CHECK(harm_note(48, DEG(0, 2), 1, 0) == 60, "third is C4");
    CHECK(harm_note(48, DEG(1, 0), 1, 0) == 69, "octave up");
    CHECK(harm_note(48, DEG(-1, 0), 1, 0) == 45, "octave down");
    CHECK(harm_note(48, DEG(0, 0), 1, 2) == 59, "transpose +2");
    set_degree(3); /* iv: D */
    CHECK(harm_note(48, DEG(0, 0), 1, 0) == 62, "follow moves the root to D");
    CHECK(harm_note(48, DEG(0, 0), 0, 0) == 57, "without follow the key root stays");
    /* harm_code is the inverse of harm_note on scale tones. */
    for (code = DEG(-2, 0); code < DEG(2, 0); ++code) {
        if ((code & 31) >= h_n)
            continue;
        n = harm_note(base, code, 1, 0);
        CHECK(harm_code(base, n, 1) == code, "code 0x%02x -> note %d -> 0x%02x", code, n, harm_code(base, n, 1));
    }
    /* Pending chord root: applied at the bar, not before. */
    cfg.ctrl_mode = 1;
    set_degree(0);
    harm_ctrl_note(62);
    CHECK(h_deg == 0, "control note waits for the bar");
    harm_apply_pending(Q_BAR);
    CHECK(h_deg == 3, "D sets degree iv at the bar (h_deg %d)", h_deg);
    harm_bar(1);
    CHECK(h_deg == 3, "the played chord holds over the progression");
    cfg.quant = Q_STEP;
    harm_ctrl_note(64);
    CHECK(h_deg == 4, "QUANT STEP applies at once");
    harm_ctrl_off(64);
    /* Multi-note chord on control channel: D major (D-F#-A) in A minor overrides pcs. */
    harm_ctrl_note(62);
    harm_ctrl_note(66);
    harm_ctrl_note(69);
    CHECK(harm_root_pc() == 2 && harm_minor() == 0, "multi-note D-F#-A in A minor is D major");
    CHECK(harm_chord_pcs(0) == ((1u << 2) | (1u << 6) | (1u << 9)), "custom chord pcs mask");
    harm_ctrl_off(62);
    harm_ctrl_off(66);
    harm_ctrl_off(69);
    cfg.ctrl_mode = 0;
    harm_ctrl_note(62);
    harm_apply_pending(Q_BAR);
    CHECK(h_key == 2 && cfg.key == 2, "key mode: D becomes the key");
    harm_ctrl_off(62);
}

static uint8_t *lane_setup(uint8_t lane, uint8_t role, uint8_t note, uint8_t style)
{
    uint8_t *h = HDR(lane), i;
    for (i = 0; i < HDR_SIZE; ++i)
        h[i] = 0;
    h[H_ROLE] = role;
    h[H_NOTE] = note;
    h[H_LEN] = 16;
    h[H_DIV] = DIV_16;
    h[H_STYLE] = style;
    h[H_SEED] = 1;
    h[H_DENS] = 5;
    h[H_EK] = 4;
    h[H_EN] = 16;
    h[H_VEL] = 100;
    h[H_FLAGS] = role == R_DRUM ? 0 : F_FOLLOW;
    return h;
}

static int hits(uint8_t lane)
{
    uint8_t i, n = HDR(lane)[H_LEN];
    int c = 0;
    for (i = 0; i < n; ++i)
        if ((GRID(lane)[i * 4] & S_ON) && (GRID(lane)[i * 4] >> S_COND_SHIFT) != C_FILL)
            ++c;
    return c;
}

static void test_generator(void)
{
    uint8_t a[256], *h, i, d;
    int prev, c;
    set_key(9, 1);
    h = lane_setup(0, R_DRUM, 36, DS_HOUSE);
    gen_grid(0);
    for (i = 0; i < 16; ++i)
        CHECK(!!(GRID(0)[i * 4] & S_ON) == !(i & 3), "house kick on the beat only (step %d)", i);
    for (i = 0; i < 16; ++i)
        CHECK(GRID(0)[i * 4 + 1] == 0, "drum steps play the lane note (step %d)", i);

    h = lane_setup(1, R_BASS, 36, BS_ACID);
    gen_grid(1);
    memcpy(a, GRID(1), 64);
    memset(GRID(1), 0, 64);
    gen_grid(1);
    CHECK(!memcmp(a, GRID(1), 64), "the same header gives the same grid");
    h[H_SEED] = 2;
    gen_grid(1);
    CHECK(memcmp(a, GRID(1), 64), "another seed gives another grid");
    h[H_SEED] = 1;
    gen_grid(1);
    CHECK(!memcmp(a, GRID(1), 64), "seed 1 again restores the grid");
    for (i = 0; i < 16; ++i)
        if (GRID(1)[i * 4] & S_ON)
            CHECK(GRID(1)[i * 4 + 1] >= DEG(-1, 0) && GRID(1)[i * 4 + 1] < DEG(2, 0),
                  "acid degrees within an octave each way (step %d: 0x%02x)", i, GRID(1)[i * 4 + 1]);

    /* Density: fewer hits below 5, never more as it falls. */
    h = lane_setup(2, R_DRUM, 42, DS_TECHNO);
    prev = 99;
    for (d = 5; d != 0xFF; --d) {
        h[H_DENS] = d;
        gen_grid(2);
        c = hits(2);
        CHECK(c <= prev, "density %d has %d hits, no more than density %d", d, c, d + 1);
        prev = c;
    }
    h[H_DENS] = 9;
    gen_grid(2);
    CHECK(hits(2) > 8, "density 9 thickens the hats (%d hits)", hits(2));

    /* Euclid drum lane. */
    h = lane_setup(3, R_DRUM, 37, DS_EUCLID);
    h[H_EK] = 3;
    h[H_EN] = 8;
    h[H_LEN] = 8;
    gen_grid(3);
    for (i = 0; i < 8; ++i)
        if ((GRID(3)[i * 4] >> S_COND_SHIFT) != C_FILL)
            CHECK(!!(GRID(3)[i * 4] & S_ON) == euclid_hit(i, 3, 8, 0), "Euclid lane follows E(3,8) at %d", i);

    /* Arp up over a lane of 6: chord tones root, third, fifth, then an octave up. */
    h = lane_setup(4, R_LEAD, 60, LS_UP);
    h[H_LEN] = 6;
    gen_grid(4);
    {
        static const uint8_t want[6] = {DEG(0, 0), DEG(0, 2), DEG(0, 4), DEG(1, 0), DEG(1, 2), DEG(1, 4)};
        for (i = 0; i < 6; ++i)
            CHECK(GRID(4)[i * 4 + 1] == want[i], "arp up step %d = 0x%02x", i, GRID(4)[i * 4 + 1]);
    }
    h = lane_setup(4, R_LEAD, 60, LS_CHORD);
    gen_grid(4);
    for (i = 0; i < 16; ++i) {
        uint8_t deg = GRID(4)[i * 4 + 1] & 31;
        CHECK(deg == 0 || deg == 2 || deg == 4 || deg == 6, "LS_CHORD step %d degree %d", i, deg);
    }

    /* Turing lane: deterministic, and a mutate/undo pair restores it. */
    h = lane_setup(5, R_LEAD, 72, LS_TURING);
    gen_grid(5);
    memcpy(a, GRID(5), 64);
    lane_mutate(5);
    gen_grid(5);
    CHECK(memcmp(a, GRID(5), 64), "mutate flips a Turing register bit");
    lane_unmutate(5);
    gen_grid(5);
    CHECK(!memcmp(a, GRID(5), 64), "undo restores the Turing motif");

    /* Seed history: eight mutations walk back to the start. */
    h = lane_setup(6, R_BASS, 36, BS_ACID);
    h[H_SEED] = 42;
    for (i = 0; i < 8; ++i)
        lane_mutate(6);
    CHECK(h[H_SEED] != 42, "mutate changes the seed");
    for (i = 0; i < 8; ++i)
        lane_unmutate(6);
    CHECK(h[H_SEED] == 42, "eight undos restore seed 42 (got %d)", h[H_SEED]);

    /* Every style of every role generates without leaving its grid. */
    {
        static const uint8_t counts[NROLES] = {DS_N, BS_N, CS_N, LS_N};
        uint8_t r, s;
        for (r = 0; r < NROLES; ++r) {
            CHECK(style_count[r] == counts[r], "role %d lists %d styles", r, style_count[r]);
            for (s = 0; s < counts[r]; ++s) {
                h = lane_setup(7, r, r == R_DRUM ? 36 : 48, s);
                h[H_LEN] = 64;
                memset(GRID(7) + 256, 0xEE, 4);
                gen_grid(7);
                CHECK(GRID(7)[256] == 0xEE, "role %d style %d stays in its grid", r, s);
                CHECK(hits(7) > 0, "role %d style %d plays something", r, s);
            }
        }
    }
}

static void test_step_encoding(void)
{
    uint8_t rat, cond, b0;
    for (rat = 1; rat <= 4; ++rat)
        for (cond = 0; cond < 8; ++cond) {
            b0 = (uint8_t)(S_ON | S_ACC | ((rat - 1) << S_RAT_SHIFT) | (cond << S_COND_SHIFT));
            CHECK((((b0 & S_RAT) >> S_RAT_SHIFT) + 1) == rat, "ratchet %d round-trips", rat);
            CHECK((b0 >> S_COND_SHIFT) == cond, "condition %d round-trips", cond);
            CHECK((b0 & S_ON) && (b0 & S_ACC) && !(b0 & S_TIE), "flags keep their bits");
        }
    CHECK(DEG(0, 0) == 0x80 && DEG(-4, 0) == 0 && DEG(3, 31) == 0xFF, "degree code octave range -4..+3");
    CHECK((DEG(-1, 5) & 31) == 5 && (DEG(-1, 5) >> 5) - 4 == -1, "degree code unpacks");
    CHECK(sizeof(cfg_t) <= 256, "settings fit their page");
    CHECK(H_HIST + 8 <= HDR_SIZE, "lane header fits 32 bytes");
    CHECK(CHAIN_BASE == 0xB000 && MEM_END <= 0xFF00, "scene bank and chain end below the MMIO page");
}

static void test_scenes(void)
{
    uint8_t *h, l;
    uint32_t i;
    memset(host_xram, 0, sizeof host_xram);
    scene_factory();
    CHECK(cfg.magic == JAM_MAGIC && cfg.scene == 0 && cur_base == BANK_BASE, "factory scenes loaded");
    CHECK(tempo_get() == 1240 && cfg.key == 9 && cfg.scale == 1, "factory 124.0 BPM in A minor");
    CHECK(HDR(0)[H_ROLE] == R_DRUM && HDR(0)[H_NOTE] == 36 && HDR(0)[H_CHAN] == 9, "scene 1 lane 1 kick on ch10");
    CHECK(HDR(5)[H_ROLE] == R_CHORD && HDR(5)[H_VOICES] == 4, "scene 1 lane 6 four-voice chords");
    scene_select(1);
    CHECK(HDR(4)[H_STYLE] == BS_ACID, "scene 2 acid bass");
    scene_select(2);
    CHECK(HDR(2)[H_EK] == 7 && HDR(2)[H_EN] == 12 && HDR(2)[H_LEN] == 12, "scene 3 hats E(7,12) over 12");
    CHECK(!memcmp(XMEM(SCENE_AT(3)), XMEM(SCENE_AT(0)), SCENE_SIZE) &&
              !memcmp(XMEM(SCENE_AT(15)), XMEM(SCENE_AT(0)), SCENE_SIZE),
          "scenes 4-16 start as copies of scene 1");
    CHECK(XMEM(CHAIN_BASE)[0] == 1 && XMEM(CHAIN_BASE)[6] == 3 && XMEM(CHAIN_BASE)[8] == 0, "factory chain 1 2 1 3");

    scene_select(4);
    lane_copy(0, 7);
    CHECK(!memcmp(HDR(7), HDR(0), HDR_SIZE) && !memcmp(GRID(7), GRID(0), GRID_SIZE), "lane copy");
    lane_erase(7);
    for (l = 0, i = 0; i < GRID_SIZE; ++i)
        l |= GRID(7)[i];
    CHECK(!l && (HDR(7)[H_FLAGS] & F_CAPT), "lane erase clears the grid and keeps it from the generator");
    scene_copy(1, 4);
    CHECK(!memcmp(XMEM(SCENE_AT(4)), XMEM(SCENE_AT(1)), SCENE_SIZE), "scene copy");
    scene_erase(4);
    h = XMEM(SCENE_AT(4));
    CHECK(h[H_ROLE] == R_DRUM && (h[H_FLAGS] & F_CAPT) && !XMEM(SCENE_AT(4) + SCENE_GRIDS)[0],
          "scene erase keeps the lane setup, clears the steps");
    scene_select(0);
    scene_snap_save();
    cfg.bars[0] = 9;
    scene_erase(0);
    CHECK(!XMEM(SCENE_AT(0) + SCENE_GRIDS)[0] && cfg.bars[0] == 9, "scene 0 modified before snapshot reload");
    CHECK(scene_snap_load() && XMEM(SCENE_AT(0) + SCENE_GRIDS)[0] && cfg.bars[0] == 4,
          "snapshot load restores scene 0 grid and bar count");
}

static void test_dump(void)
{
    static uint8_t buf[100000], save[0x10000];
    uint32_t i, total = 256u + (MEM_END - BANK_BASE);
    /* Send. */
    tx_buf = buf;
    tx_cap = sizeof buf;
    tx_n = 0;
    dump_start();
    while (dump_run)
        dump_slice();
    CHECK(tx_n == 5 + total * 2 + 3, "dump length %u bytes", tx_n);
    CHECK(buf[0] == 0xF0 && buf[1] == 0x7D && buf[2] == 0x4A && buf[3] == 0x38 && buf[4] == 0x01, "dump header");
    CHECK(buf[tx_n - 1] == 0xF7, "dump ends with F7");
    for (i = 1; i < tx_n - 1; ++i)
        if (buf[i] & 0x80)
            break;
    CHECK(i == tx_n - 1, "dump body is 7-bit");
    CHECK(buf[5] == (JAM_MAGIC >> 4) && buf[6] == (JAM_MAGIC & 15), "dump starts with the settings page");

    /* Load into cleared memory. */
    memcpy(save, host_xram, sizeof save);
    memset(XMEM(CFG_BASE), 0, 256);
    memset(XMEM(BANK_BASE), 0, MEM_END - BANK_BASE);
    playing = 1;
    for (i = 0; i < tx_n; ++i)
        dump_rx(buf[i]);
    CHECK(!playing, "a dump load stops playback");
    CHECK(last_msg && !strcmp(last_msg, "DUMP LOADED"), "dump loaded (%s)", last_msg ? last_msg : "-");
    CHECK(!memcmp(XMEM(CFG_BASE), save + CFG_BASE, 256) &&
              !memcmp(XMEM(BANK_BASE), save + BANK_BASE, MEM_END - BANK_BASE),
          "the load restores settings, scenes and chain");
    CHECK(tempo10 == 1240, "the loaded tempo is applied");

    /* A damaged dump is refused. */
    buf[1000] ^= 1;
    for (i = 0; i < tx_n; ++i)
        dump_rx(buf[i]);
    CHECK(last_msg && !strcmp(last_msg, "DUMP ERROR") && cfg.magic != JAM_MAGIC,
          "a bad checksum marks memory invalid (%s)", last_msg ? last_msg : "-");
    buf[1000] ^= 1;
    for (i = 0; i < 2000; ++i)
        dump_rx(buf[i]);
    dump_rx(0xF7);
    CHECK(!strcmp(last_msg, "DUMP ERROR"), "a short dump is refused");
    /* Other sysex is ignored. */
    last_msg = NULL;
    dump_rx(0xF0);
    dump_rx(0x43);
    dump_rx(0x10);
    dump_rx(0xF7);
    CHECK(!last_msg, "foreign sysex ignored");
}

int main(void)
{
    test_euclid();
    test_harmony();
    test_voice_leading();
    test_generator();
    test_step_encoding();
    test_scenes();
    test_dump();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
