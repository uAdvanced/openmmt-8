/* OpenMMT-8 core: shared state and module interfaces. */
#ifndef OPENMMT8_CORE_H
#define OPENMMT8_CORE_H

#include "hw.h"

/* Build options (set by tools/build_openmmt8_core.sh from OPENMMT8_PROFILE):
 * OPENMMT8_TAPE   tape save/load on the TAPE key, as the original unit; without it the TAPE
 *                 key holds only the MIDI dump page.
 * OPENMMT8_EDITX  part EDIT extensions: MIDI note and step entry, data knob, audition, FF/REW
 *                 by the quantize grid, bar:beat:tick times and one-step undo. */

/* Panel key ids: row * 8 + column. */
enum {
    K_REW = 0, K_FF = 1, K_ERASE = 2, K_TRANS = 3, K_PLAY = 4, K_STOP = 5, K_COPY = 6, K_REC = 7,
    K_TRK1 = 8,
    K_TEMPO = 16, K_MINUS = 17, K_PLUS = 18, K_FOOT = 19, K_PGUP = 22, K_PGDN = 23,
    K_CLICK = 24, K_CHAN = 30, K_TAPE = 31,
    K_CLOCK = 32, K_SONG = 38, K_MERGE = 39,
    K_FILTER = 40, K_ECHO = 41, K_LOOP = 42, K_QUANT = 43, K_LENGTH = 44, K_PART = 45, K_EDIT = 46,
    K_NAME = 47,
    K_MEMORY = 48, /* virtual: LENGTH pressed while RECORD is held */
    K_NONE = 0xFF
};

#define TICKS_PER_BEAT 96u
#define MSG_PASSES 20u   /* "... COMPLETE" style messages, counted once RECORD is up */
/* Extra message passes of a part-wide ERASE of an open part (see part_open). */
#define ERASE_OPEN_PASSES 10u
#define ERASE_OPEN_SEL_PASSES 8u
/* The original unit's QUANTIZE time from the key action, and the extra when note starts move. */
#define QUANT_CYCLES 56460u
/* The original unit's time from a stopped PLAY key action to the restarted tick. */
#define PLAY_START_CYCLES 4293u
/* Stopped PLAY: extra machine cycles after PLAY_START_CYCLES before tick 0 (scan grid). */
#define PLAY_STOP_KEY_LAG 10000u
#define PLAY_REC_KEY_LAG 2500u
/* Record armed, the key action itself comes later by more than the original unit's. */
#define REC_START_EARLY 514u
/* The original unit's extra pass time storing an entered LENGTH. */
#define LENGTH_ENTRY_CYCLES 4120u
#define QUANT_MOVE_CYCLES 39900u

/* ---- main.c ---- */
extern volatile __data uint8_t g_ticks;
/* Ticks handled by the main loop, and their count when the last MIDI byte arrived. */
extern volatile __xdata uint8_t t_count, rx_t, rx_t_msg;
void timer_set_tempo(uint8_t bpm);
uint8_t mod96(uint16_t n);

/* ---- lcd.c ---- */
extern __xdata char lcd[32];
/* Cell 0-31 of the cursor. Without the tape code there is room to show it: bits 6-5 are the
 * HD44780 cursor and blink bits, so 0 hides it, LCD_UL is a steady underline and LCD_BLINK
 * a blinking block on the value being edited. */
extern __xdata uint8_t lcd_cursor;
#ifdef OPENMMT8_TAPE
#define LCD_UL 0
#define LCD_BLINK 0
#define CURSOR(v)
#else
#define LCD_UL 0x40
#define LCD_BLINK 0x60
#define CURSOR(v) (lcd_cursor = (v))
#endif
void lcd_init(void);
/* Returns the number of cells written. */
uint8_t lcd_flush(void);
#ifdef OPENMMT8_TAPE
void tape_lcd_part(uint8_t idx);
#endif
#ifdef OPENMMT8_TAPE
void lcd_hw_cell(uint8_t pos);
#endif
void lcd_text(uint8_t pos, const char __code *s, uint8_t n);
void lcd_num(uint8_t pos, uint16_t v, uint8_t width);
void lcd_num8(uint8_t pos, uint8_t v, uint8_t width);
#define LCD_ENTRY 0x80
extern __data uint8_t ent_n;        /* keypad digits entered so far */
extern __xdata char ent_c[4];
void lcd_fill(uint8_t pos, uint8_t n);

/* ---- keys.c ---- */
/* Returns 1 when sampled changes are due: the caller then runs keys_flush. */
uint8_t keys_scan(void);
void keys_flush(void);
void keys_init(void);
uint8_t row_read(uint8_t sel);
void keys_tick(uint16_t cycles);
uint8_t key_held(uint8_t k);
void keys_locate_start(void);
void keys_rescan(uint16_t sent);
void keys_restart(void);
void keys_rec_stopped(void);
extern __xdata uint8_t held_n;
extern __xdata uint16_t rb_start, rb_end;
void keys_act_wait(uint16_t n);
void keys_pass_extend(uint16_t cycles);
void keys_drawn(uint8_t cells);
uint16_t cycles_now(void);
void cycles_wait(uint16_t from, uint16_t n);

/* ---- midi.c ---- */
void midi_init(void);
void midi_tx(uint8_t b);
void midi_drain(void);
uint8_t midi_rx_ready(void);
uint8_t midi_rx(void);

/* ---- ui.c ---- */
extern __bit ui_dirty;
void ui_init(void);
void ui_key_down(uint8_t k);
void ui_key_up(uint8_t k);
void ui_locate_step(uint8_t fwd);
void ui_pass(void);
extern __data uint8_t fn;
uint16_t ui_cadence(uint8_t held);
uint16_t ui_draw_cycles(void);
void ui_leds(void);
void ui_led_rec_off(void);
void ui_render(void);
void ui_screen(uint8_t id);
extern __bit edit_on;
extern __bit shift_live;
/* Built with -DOPENMMT8_TAPE, TAPE saves, verifies and loads tape; otherwise it opens only
 * the MIDI dump page. Tape sync in/out is part of both builds. */
#ifdef OPENMMT8_TAPE
/* Tape save/load owns the display and the key-row latch until it finishes. */
extern __bit tape_run;
extern __bit tape_heard;
extern __data uint8_t tape_edges;
extern __bit tape_aborted;
void ui_tape(void);
void tape_heap_reset(void);
#endif

/* ---- seq.c: data model ---- */
typedef __xdata uint8_t *xptr;

/* Persistent settings (battery-backed). */
typedef struct {
    uint8_t magic;
    uint8_t mode_song;
    uint8_t part;
    uint8_t song;
    uint8_t tempo;
    uint8_t click_val;
    uint8_t rec_click;
    uint8_t play_click;
    uint8_t countdown;
    uint8_t quant_val;
    uint8_t quant_mode;
    uint8_t filt[6];
    uint8_t filt_chan;
    uint8_t clock_src;
    uint8_t clock_out;
    uint8_t auto_start;
    uint8_t loop;
    uint8_t echo;
    int8_t shift[8];
    uint16_t heap_end;
} cfg_t;
#define cfg (*(__xdata cfg_t *)0x04C8)
/* cfg.magic: settings valid; seq_init resets only the settings. */
#define CFG_MAGIC 0x5A
#define CFG_SETTINGS 0xA5
/* Copy scratch between cfg and the song table. Not part of the saved settings. */
#define SCRATCH_RAM (0x04C8u + sizeof(cfg_t))
/* cfg.clock_src */
#define CLK_MIDI 0
#define CLK_INTERNAL 1
#define CLK_TAPE 2

/* Part record layout (see seq.c). */
#define P_SIZE 0x00
#define P_OFF 0x02
#define P_LEN 0x12
#define P_CHAN 0x14
#define P_NAME 0x1C
#define P_DATA 0x2A
#define EV 7
/* Song record layout. */
#define S_TEMPO 0x02
#define S_NAME 0x03
#define S_STEPS 0x11

xptr part_rec(uint8_t n);          /* record address or 0 */
uint16_t part_len(xptr p);         /* beats, 0 = no length yet */
xptr cur_part(void);               /* record of the selected part or 0 */
uint16_t cur_len(void);
void part_set_len(xptr p, uint16_t beats);
void part_resize(uint16_t len);
void part_select(uint8_t n);
extern __bit part_open;
void part_release(void);
xptr song_rec(uint8_t n);
uint8_t song_steps(xptr s);
void song_insert_step(uint8_t n, uint8_t i, uint8_t part);
void song_erase_step(uint8_t n, uint8_t i);
void song_select(uint8_t n);
void song_release(void);
uint8_t song_copy(void); /* op.src -> op.dst */
void song_erase(uint8_t n);
/* COPY/ERASE operands: source part/track, destination part/track, data page, tracks. */
typedef struct {
    uint8_t src, src_t, dst, dst_t, sel, mask;
} op_t;
extern __xdata op_t op;
void part_erase(void);
void part_transpose(void);
uint8_t part_merge(void);
uint8_t part_quantize(void);
uint8_t part_copy(void);
uint16_t heap_free(void);
void mem_move(uint16_t dst, uint16_t src, uint16_t n);
void seq_init(void);
void clock_apply(void);
#ifdef OPENMMT8_TAPE
extern __bit tape_clk;
#endif

/* ---- seq.c: transport ---- */
enum { TP_STOP, TP_PLAY, TP_COUNTIN };
extern __data uint8_t tp_state;
extern __bit tp_recording;
extern __bit rec_screen;
extern __bit rec_armed;
extern __xdata uint8_t rec_track;  /* 0..7, 0xFF none */
extern __xdata uint8_t count_left; /* count-in beats remaining */
extern __xdata uint8_t count_ticks; /* ticks into the count-in beat, 0xFF before the first */
extern __xdata uint16_t tp_pos;    /* ticks from part start */
extern __xdata uint16_t tp_beat;   /* current beat, 0-based */
extern __xdata uint8_t next_part;  /* 0xFF none */
extern __xdata uint8_t mute;       /* bit set = track off */
extern __xdata uint8_t play_part;
extern __xdata uint8_t song_step;
void seq_play(void);
void seq_stop(void);
void seq_punch_in(void);
void seq_punch_out(void);
void seq_continue(void);
void seq_locate(uint16_t beat);
extern __xdata uint16_t stop_spp; /* continue position, 16th notes */
extern const uint8_t __code bit_tab[8];
xptr track(xptr p, uint8_t t);
uint8_t ev_end(xptr e);
xptr ev_next(xptr e);
xptr ev_body(xptr e);
uint8_t ev_kind(xptr e);
void seq_tick(void);
/* Part EDIT: the editor's event is ed_off bytes into track ed_trk of the current part. */
extern __bit ed_on;
extern __xdata uint8_t ed_trk;
extern __xdata uint16_t ed_off;
/* Editor work cells after the copy scratch: the event, its time, type, cursor field, data
 * byte and field count. */
typedef struct {
    xptr e;
    uint16_t time;
    uint8_t ty, f, k, n;
} ed_work_t;
#define ew (*(__xdata ed_work_t *)(SCRATCH_RAM + 16))
#define ev_sysex(e) (ev_kind(e) == 6)
xptr ed_next(xptr e);
xptr ed_base(void);
xptr ed_event(uint8_t back);
uint8_t ed_rebuild(xptr src);
uint8_t ed_insert(uint8_t kind);
#ifdef OPENMMT8_EDITX
void ed_seek(uint16_t t);
uint8_t ed_grid(void);
uint8_t ed_step(uint16_t t);
/* One-step undo of the last field change: the event before it is kept. */
extern __bit ed_undo_ok;
void ed_snap(void);
void ed_mark(void);
uint8_t ed_undo(void);
/* Part EDIT options (battery-backed, past the key scan state): the data knob controller
 * (0 = off), audition, FF/REW by the quantize grid, times in bars. */
typedef struct {
    uint8_t cc, aud, grid, bars;
} edopt_t;
#define edopt (*(__xdata edopt_t *)0x05F0)
uint8_t ui_midi(uint8_t st, uint8_t a, uint8_t b);
#endif
void seq_midi_in(void);
void dump_send(void);

#endif
