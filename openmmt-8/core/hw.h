/* OpenMMT-8 hardware map (8031 @ 12 MHz, 1 MHz machine cycle). */
#ifndef OPENMMT8_HW_H
#define OPENMMT8_HW_H

#include <8051.h>
#include <stdint.h>

#define XREG(a) (*(volatile __xdata uint8_t *)(a))

/* Panel LED data (active low) latched into the bank selected by FF04. */
#define LED_DATA XREG(0xFF02)
#define LED_BANK XREG(0xFF04)
/* Key row select (bits 0..5 active low); 0x7E while the tape output runs. */
#define KEY_ROWS XREG(0xFF06)
#define LCD_CMD XREG(0xFF08)
#define LCD_DATA XREG(0xFF09)

/* Battery-backed RAM: part table, settings, song table, then the record heap. */
#define PART_TABLE 0x0400u /* 100 big-endian part pointers, hi byte 0 = empty */
#define SONG_TABLE 0x0502u /* 100 big-endian song pointers */
#define KEYS_RAM 0x05CAu  /* key-scan state (42 bytes) */
#define DUMP_RAM 0x05F4u  /* sysex dump state (11 bytes), up to the heap */
#define HEAP_BASE 0x0600u
#define HEAP_TOP 0xFF00u

#endif
