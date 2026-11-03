/* JAM-8: the second ROM in this project, a generative loop sequencer.
 * Shared constants, memory layout and module interfaces. */
#ifndef JAM8_H
#define JAM8_H

#include <stdint.h>

#ifdef JAM8_HOST
/* Host build of the pure modules (tests): SDCC storage classes vanish, xdata is an array. */
#define __xdata
#define __code
#define __data
#define __bit uint8_t
extern uint8_t host_xram[0x10000];
#define XMEM(a) (host_xram + (uint16_t)(a))
#else
#include "hw.h"
#define XMEM(a) ((__xdata uint8_t *)(uint16_t)(a))
#endif

/* x mod 12 for 0-255 in 8-bit arithmetic, without promotion to a 16-bit library call. */
#define MOD12(x) ((uint8_t)((uint8_t)(x) % (uint8_t)12u))

#define PPQN 96u
#define BAR_TICKS 384u
#define NLANES 8u
#define MAXLEN 64u
#define NSCENES 16u
#define CHAIN_LEN 32u

/* ---- battery-backed memory map ----
 * 0x0000-0x01FF MIDI rings, 0x0200-0x07FF compiler work area (cleared at boot),
 * 0x0800 settings, 0x1000 generator output, 0x2000 scene bank, then the chain. */
#define CFG_BASE 0x0800u
#define NEXT_BASE 0x1000u /* 8 lanes x 256 bytes */
#define BANK_BASE 0x2000u
#define HDR_SIZE 32u
#define GRID_SIZE 256u /* 64 steps x 4 bytes */
#define SCENE_SIZE 0x0900u /* 8 headers, then 8 grids */
#define SCENE_GRIDS 0x0100u
#define CHAIN_BASE (BANK_BASE + NSCENES * SCENE_SIZE) /* 0xB000 */
#define MEM_END (CHAIN_BASE + CHAIN_LEN * 2u)
#define SNAP_SIGN_ADDR 0xB0FEu
#define SNAP_BARS_ADDR 0xB0FFu
#define SNAP_BASE 0xB100u /* 0x0900 bytes: full scene snapshot */
#define CCAP_BASE 0xBA00u /* 16 scenes x 64 steps x 4 chord notes (0x1000 bytes) */
#define CCAP(s, i) XMEM(CCAP_BASE + ((uint16_t)(s) << 8) + ((uint16_t)(i) << 2))

/* ---- step: 4 bytes ----
 * b0: bit0 on, bit1 accent, bit2 tie/slide, bits3-4 ratchet-1, bits5-7 condition
 * b1: drum lanes the MIDI note (0 = lane note); melodic lanes the degree code
 *     (bits 7-5 octave + 4, bits 4-0 scale degree above the chord root)
 * b2: velocity (0 = lane velocity)
 * b3: high nibble probability (15 = always), low nibble delay in ticks (0-15) */
#define S_ON 0x01u
#define S_ACC 0x02u
#define S_TIE 0x04u
#define S_RAT 0x18u
#define S_RAT_SHIFT 3
#define S_COND_SHIFT 5
enum { C_ALWAYS, C_FILL, C_NOFILL, C_1OF2, C_2OF2, C_1OF4, C_4OF4, C_FIRST };
#define DEG(oct, d) ((uint8_t)((((oct) + 4) << 5) | (d)))

/* ---- lane header: 32 bytes ---- */
enum {
    H_ROLE, H_CHAN, H_NOTE, H_LEN, H_DIV, H_GATE, H_STYLE, H_SEED,
    H_DENS, H_EK, H_EN, H_EROT, H_EVOL, H_FLAGS, H_VEL, H_HUM,
    H_TUR, H_VOICES = 18, H_TRANS, H_ROT, H_HIDX, H_HIST, /* 8 bytes, to 29 */
    H_MCC = 30
};
#define F_MUTE 0x01u
#define F_FOLLOW 0x02u
#define F_CAPT 0x04u /* grid is played or edited in: the generator leaves it */
#define F_SEV 0x08u  /* chords of four notes */
#define F_OPEN 0x10u /* Drop-2 open chord voicing */
enum { R_DRUM, R_BASS, R_CHORD, R_LEAD, NROLES };

/* Styles per role. */
enum { DS_HOUSE, DS_TECHNO, DS_BREAKS, DS_BOOMBAP, DS_GARAGE, DS_DEMBOW, DS_TRAP, DS_ELECTRO, DS_EUCLID, DS_N };
enum { BS_ROOT8, BS_OFFBEAT, BS_OCTAVE, BS_ACID, BS_WALK, BS_EUCLID, BS_N };
enum { CS_STAB, CS_PAD, CS_STRUM, CS_EUCLID, CS_N };
enum { LS_UP, LS_DOWN, LS_UPDN, LS_RAND, LS_CONVERGE, LS_CHORD, LS_TURING, LS_EUCLID, LS_N };
enum { DIV_32, DIV_16T, DIV_16, DIV_8T, DIV_8, DIV_4, DIV_N };
enum { Q_STEP, Q_BEAT, Q_BAR };
#define NSCALES 10u
#define NPROGS 8u

/* ---- settings at CFG_BASE ---- */
typedef struct {
    uint8_t magic;
    uint8_t tempo_hi, tempo_lo; /* tempo in tenths of a BPM, big-endian */
    uint8_t swing;              /* 50-75 % */
    uint8_t key, scale, prog;
    uint8_t ctrl_ch;   /* 0 = off, 1-16 */
    uint8_t ctrl_mode; /* 0 key, 1 chord */
    uint8_t knob;      /* CC number, 0 = off */
    uint8_t sync;      /* 0 internal, 1 MIDI clock */
    uint8_t clk_out, click, quant, scene, thru, chain_on;
    uint8_t bars[NSCENES]; /* scene loop length in bars */
    uint8_t sig[4];        /* "JAM8": with magic, tells a JAM-8 bank from another ROM's data */
} cfg_t;
#define JAM_MAGIC 0x4Au
#define cfg (*(__xdata cfg_t *)XMEM(CFG_BASE))

/* ---- tables.c ---- */
extern const uint8_t __code div_ticks[DIV_N];
extern const uint16_t __code scale_masks[NSCALES];
extern const uint8_t __code prog_len[NPROGS];
extern const uint8_t __code prog_deg[NPROGS][12];
extern const uint8_t __code style_count[NROLES];
extern const uint8_t __code metric_w[16];
/* Drum style rows: per style, per drum class (kick, snare, closed hat, open hat, perc):
 * hit mask, accent mask (16 steps, bit 0 = step 1). */
#define DCLASSES 5u
enum { DC_KICK, DC_SNARE, DC_CHAT, DC_OHAT, DC_PERC };
extern const uint16_t __code drum_hits[DS_EUCLID][DCLASSES];
extern const uint16_t __code drum_acc[DS_EUCLID][DCLASSES];
extern const uint16_t __code drum_fill[DCLASSES];
extern const uint8_t __code drum_ghost[DCLASSES];
extern const uint8_t __code acid_deg[8];
extern const uint8_t __code walk_deg[8];
uint8_t drum_class(uint8_t note);

/* ---- gen.c ---- */
uint8_t rng8(__xdata uint16_t *s);
uint16_t mul8(uint8_t a, uint8_t b); /* a * b with MUL AB */
uint8_t euclid_hit(uint8_t i, uint8_t k, uint8_t n, uint8_t rot);
void gen_begin(__xdata uint8_t *h); /* seeds the generator from a lane header */
void gen_grid(uint8_t lane); /* the whole grid of a lane in the current scene */
void gen_request(uint8_t lane);
void gen_cancel(void);
void gen_slice(void);
extern __xdata uint8_t gen_ready; /* lanes whose next grid waits for the loop point */
void lane_mutate(uint8_t lane);
void lane_unmutate(uint8_t lane);

/* ---- harmony.c ---- */
extern __xdata uint8_t h_semis[12], h_n, h_deg, h_id, h_key;
void harm_init(void);
void harm_set_scale(void);
void harm_bar(uint16_t bar);
uint8_t harm_note(uint8_t base, uint8_t code, uint8_t follow, int8_t trans);
uint8_t harm_code(uint8_t base, uint8_t note, uint8_t follow);
uint16_t harm_chord_pcs(uint8_t sev);
uint8_t harm_root_pc(void);
uint8_t harm_minor(void);
void harm_ctrl_note(uint8_t note);
void harm_ctrl_off(uint8_t note);
void harm_apply_pending(uint8_t at);
extern __xdata uint8_t vl_base, vl_sev; /* chord lane's base note and seventh flag */
void vl_init(__xdata uint8_t *v, uint8_t nv);
void vl_lead(__xdata uint8_t *v, uint8_t nv);

/* ---- scene.c ---- */
extern __xdata uint16_t cur_base;
#define HDR(l) XMEM(cur_base + (uint16_t)(l) * HDR_SIZE)
#define GRID(l) XMEM(cur_base + SCENE_GRIDS + (uint16_t)(l) * GRID_SIZE)
#define NEXTG(l) XMEM(NEXT_BASE + (uint16_t)(l) * GRID_SIZE)
#define SCENE_AT(s) (BANK_BASE + (uint16_t)(s) * SCENE_SIZE)
void scene_select(uint8_t s);
void scene_factory(void);
uint8_t mem_signed(void);
void mem_sign(void);
void mem_sanitize(void);
void lane_defaults(__xdata uint8_t *h, uint8_t role, uint8_t note, uint8_t style);
void lane_copy(uint8_t from, uint8_t to);
void lane_erase(uint8_t lane);
void scene_copy(uint8_t from, uint8_t to);
void scene_erase(uint8_t s);
void scene_snap_save(void);
uint8_t scene_snap_load(void);
void xcopy(uint16_t dst, uint16_t src, uint16_t n);
void xfill(uint16_t dst, uint8_t v, uint16_t n);
void dump_start(void);
void dump_slice(void);
extern __bit dump_run;
void dump_rx(uint8_t b);
extern __xdata uint8_t dump_rx_state;

/* ---- main.c ---- */
extern volatile __data uint8_t t0_ticks;
extern __xdata uint16_t tick_cycles;
void timer_set_tempo(uint16_t tempo10);
uint16_t tempo_get(void);
void tempo_set(uint16_t t10);

/* ---- midi.c ---- */
void midi_init(void);
void midi_tx(uint8_t b);
void midi_rt(void);
uint8_t midi_tx_free(void);
uint8_t midi_rx_ready(void);
uint8_t midi_rx(void);

/* ---- lcd.c ---- */
extern __xdata char lcd[32];
void lcd_init(void);
uint8_t lcd_flush(uint8_t budget);
void lcd_text(uint8_t pos, const char __code *s, uint8_t n);
void lcd_num(uint8_t pos, uint16_t v, uint8_t width);
void lcd_fill(uint8_t pos, uint8_t n);

/* ---- keys.c ---- */
enum {
    K_REW = 0, K_FF = 1, K_ERASE = 2, K_TRANS = 3, K_PLAY = 4, K_STOP = 5, K_COPY = 6, K_REC = 7,
    K_TRK1 = 8,
    K_TEMPO = 16, K_MINUS = 17, K_PLUS = 18, K_FOOT = 19, K_PGUP = 22, K_PGDN = 23,
    K_CLICK = 24, K_CHAN = 30, K_TAPE = 31,
    K_CLOCK = 32, K_SONG = 38, K_MERGE = 39,
    K_FILTER = 40, K_ECHO = 41, K_LOOP = 42, K_QUANT = 43, K_LENGTH = 44, K_PART = 45, K_EDIT = 46,
    K_NAME = 47, K_NONE = 0xFF
};
void keys_init(void);
void keys_scan(void);
uint8_t key_held(uint8_t k);
uint8_t row_read(uint8_t sel);
extern __xdata uint16_t scan_n; /* scans since power-on (4 ms each) */

/* ---- engine.c ---- */
typedef struct {
    uint8_t cnt;  /* ticks to the next step boundary */
    uint8_t step; /* current step, 0xFF before the first */
    uint8_t loops;
    uint8_t cap;  /* capture steps left; 0xFF armed while stopped */
    uint8_t vid;  /* chord id the voices follow */
    uint8_t nv;
    uint8_t v[4];
    uint8_t pstat; /* 0 = prev step deterministic, 1 = prev prob fired, 2 = prev prob skipped */
    uint8_t cap_st; /* last captured step index, or 0xFF */
    uint8_t cap_held; /* active captured notes held */
} lane_rt;
extern __xdata lane_rt lr[NLANES];
extern __bit playing, fill;
extern __xdata uint16_t tp_tick, tp_bar, scene_bar;
extern __xdata uint8_t tp_bt, mute_pend, queued, beat_flag, chain_pos, chain_rep;
extern __xdata uint8_t mom_mute, mom_solo, solo_latch, solo_pend, stutter, stutter_anchor;
void engine_init(void);
void engine_tick(void);
void engine_slice(void);
void engine_start(uint8_t cont);
void engine_stop(void);
uint8_t engine_note_in(uint8_t note, uint8_t vel);
void engine_note_off(uint8_t note);
uint8_t lane_audible(uint8_t lane);
void engine_toggle_mute(uint8_t lane);
void engine_toggle_solo(uint8_t lane);
void engine_set_mom_mute(uint8_t mask);
void engine_set_mom_solo(uint8_t mask);
void engine_set_stutter(uint8_t mode);
void engine_arm(uint8_t lane);
void engine_queue_scene(uint8_t s);
void engine_swing(void);
void eq_flush(void);
void note_now(uint8_t st, uint8_t n, uint8_t v);

/* ---- clock.c ---- */
extern __xdata uint8_t tp_ticks;
void clock_poll(void);
void clock_thru(uint8_t st, uint8_t a, uint8_t b, uint8_t len);

/* ---- ui.c ---- */
extern __bit ui_dirty;
extern __xdata uint8_t focus;
void ui_init(void);
void ui_key(uint8_t k, uint8_t down);
void ui_scan(void);
void ui_render(void);
void ui_leds(void);
void ui_cc(uint8_t num, uint8_t val);
void ui_msg(const char __code *s, uint8_t lane, uint16_t v);

#endif
