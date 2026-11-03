/* Style, scale and progression tables. */
#include "jam.h"

/* Step lengths in ticks: 1/32, 1/16 triplet, 1/16, 1/8 triplet, 1/8, 1/4. */
const uint8_t __code div_ticks[DIV_N] = {12, 16, 24, 32, 48, 96};

/* Bit n = semitone n above the key. */
const uint16_t __code scale_masks[NSCALES] = {
    0xAB5, /* major */
    0x5AD, /* natural minor */
    0x6AD, /* dorian */
    0x5AB, /* phrygian */
    0x6B5, /* mixolydian */
    0x9AD, /* harmonic minor */
    0x4A9, /* minor pentatonic */
    0x295, /* major pentatonic */
    0x4E9, /* blues */
    0xFFF, /* chromatic */
};

/* Chord roots per bar, as scale degrees (0 = the key). */
const uint8_t __code prog_len[NPROGS] = {1, 4, 4, 4, 4, 12, 4, 2};
const uint8_t __code prog_deg[NPROGS][12] = {
    {0},                                  /* vamp */
    {0, 5, 2, 6},                         /* i VI III VII */
    {0, 4, 5, 3},                         /* I V vi IV */
    {1, 4, 0, 0},                         /* ii V I I */
    {0, 3, 4, 0},                         /* i iv v i */
    {0, 0, 0, 0, 3, 3, 0, 0, 4, 3, 0, 4}, /* 12-bar blues */
    {0, 6, 5, 6},                         /* i VII VI VII */
    {0, 3},                               /* two chords */
};

const uint8_t __code style_count[NROLES] = {DS_N, BS_N, CS_N, LS_N};

/* Metric weight of each 16th: the downbeat survives thinning longest. */
const uint8_t __code metric_w[16] = {4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0};

/* Kick, snare/clap, closed hat, open hat, perc. */
const uint16_t __code drum_hits[DS_EUCLID][DCLASSES] = {
    {0x1111, 0x1010, 0xEEEE, 0x4444, 0x2088}, /* house */
    {0x1111, 0x1010, 0xFFFF, 0x4444, 0x0848}, /* techno */
    {0x0C05, 0x9290, 0x5555, 0x4000, 0x0000}, /* breaks */
    {0x0481, 0x1010, 0x5555, 0x0000, 0x0100}, /* boom-bap */
    {0x0401, 0x1010, 0x4C4C, 0x0000, 0x8080}, /* UK garage */
    {0x1111, 0x4848, 0x5555, 0x0000, 0x2222}, /* dembow */
    {0x0801, 0x0100, 0x5555, 0x0000, 0x0000}, /* trap (half time) */
    {0x0441, 0x1010, 0xFFFF, 0x0000, 0x2000}, /* electro */
};
const uint16_t __code drum_acc[DS_EUCLID][DCLASSES] = {
    {0x1111, 0x1010, 0x4444, 0x4444, 0x0000},
    {0x1111, 0x1010, 0x4444, 0x0000, 0x0040},
    {0x0001, 0x1010, 0x1111, 0x0000, 0x0000},
    {0x0001, 0x1010, 0x1111, 0x0000, 0x0000},
    {0x0001, 0x1010, 0x4444, 0x0000, 0x0000},
    {0x1111, 0x4848, 0x0000, 0x0000, 0x0000},
    {0x0001, 0x0100, 0x1111, 0x0000, 0x0000},
    {0x0041, 0x1010, 0x4444, 0x0000, 0x0000},
};
/* Extra hits while FILL is held; hats drop out instead (see gen.c). */
const uint16_t __code drum_fill[DCLASSES] = {0x1111, 0xFFFF, 0x0000, 0x0000, 0x5555};
/* Chance per 256 and density step above 5 of a ghost hit on an empty step. */
const uint8_t __code drum_ghost[DCLASSES] = {10, 24, 48, 12, 32};

/* Acid bass degrees (octave in the high bits). */
const uint8_t __code acid_deg[8] = {
    DEG(0, 0), DEG(0, 0), DEG(0, 0), DEG(1, 0), DEG(0, 2), DEG(0, 4), DEG(-1, 4), DEG(1, 2),
};
/* Walking bass tones, one per beat. */
const uint8_t __code walk_deg[8] = {
    DEG(0, 0), DEG(0, 2), DEG(0, 4), DEG(0, 5), DEG(0, 4), DEG(0, 2), DEG(0, 6), DEG(-1, 4),
};

uint8_t drum_class(uint8_t note)
{
    switch (note) {
    case 35:
    case 36:
        return DC_KICK;
    case 38:
    case 39:
    case 40:
        return DC_SNARE;
    case 42:
    case 44:
        return DC_CHAT;
    case 46:
        return DC_OHAT;
    default:
        return DC_PERC;
    }
}
