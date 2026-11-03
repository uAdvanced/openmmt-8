/* JAM-8 hardware map (8031 @ 12 MHz, 1 MHz machine cycle). */
#ifndef JAM8_HW_H
#define JAM8_HW_H

#include <8051.h>
#include <stdint.h>

#define XREG(a) (*(volatile __xdata uint8_t *)(a))

/* Track LEDs 1-8 (active low). */
#define LED_DATA XREG(0xFF02)
/* Mode LEDs (active low): PLAY 0, REC 1, PART 2, EDIT 3, SONG 4, ECHO 5, LOOP 6. */
#define LED_BANK XREG(0xFF04)
/* Key row select (bits 0..5 active low); P1 returns the columns (active low). */
#define KEY_ROWS XREG(0xFF06)
#define LCD_CMD XREG(0xFF08)
#define LCD_DATA XREG(0xFF09)

#define L_PLAY 0x01u
#define L_REC 0x02u
#define L_PART 0x04u
#define L_EDIT 0x08u
#define L_SONG 0x10u
#define L_ECHO 0x20u
#define L_LOOP 0x40u

#endif
