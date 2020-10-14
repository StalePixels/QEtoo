
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <ctype.h>
#include <limits.h>

#include <z80.h>
#include <arch/zxn.h>
#include <arch/zx/esxdos.h>
#include <arch/zxn/esxdos.h>
#include <errno.h>
#include <input.h>
#include <compress/zx7.h>

#include "qe_banked.h"
#include "esxcuss/libcuss.h"
#include "esxcuss/textmode.h"

extern void at_exit();

unsigned char orig_cpu_speed;

/* move these to a NextZXOS allocated memory bank, later */
static unsigned char bankedShadowTilemap[sizeof(tilemap)];
static unsigned char bankedShadowTiles[sizeof(tiles)];

extern uint8_t orig_mmu6, orig_mmu7, top_page, btm_page, file_handle;

uint8_t tilemap_background[16] = {
        0xE3,0x01,     // Transparent
        0xA0,0x00,     // Red
        0x14,0x00,     // Green
        0xAC,0x00,     // Yellow
        0x02,0x01,     // Blue
        0xA2,0x01,     // Magenta
        0x16,0x01,     // Cyan
        0xB6,0x01      // White
};

uint8_t tilemap_foreground[32] = {            // 0xE3 = 277
//      TRANS,     Red,       Green,     Yellow,    Blue,      Magenta,   Cyan,      White,
        0xE3,0x01, 0xA0,0x00, 0x14,0x00, 0xAC,0x00, 0x02,0x01, 0xA2,0x01, 0x16,0x01, 0xB6,0x01, // (normal)
        0x6D,0x01, 0xED,0x01, 0x7D,0x01, 0xFD,0x01, 0x77,0x01, 0xEE,0x01, 0x7F,0x01, 0xFF,0x01  // (bright)
};

void bankedTextmodeBackup() {
    memcpy(bankedShadowTiles,   tiles, sizeof(tiles));
    memcpy(bankedShadowTilemap,   tilemap, sizeof(tilemap));
}

void bankedTextmodeRestore() {
    memcpy(tiles, bankedShadowTiles, sizeof(tiles));
    memcpy(tilemap, bankedShadowTilemap, sizeof(tilemap));
}

void banked_init() {
    // Store CPU speed
    orig_cpu_speed = ZXN_READ_REG(REG_TURBO_MODE);

    // Set CPU speed to 28Mhz
    ZXN_NEXTREG(REG_TURBO_MODE, 3);

    // Ensure we clean up as we shut down...
    atexit(at_exit);

    // Get 16k of scratch RAM for text editing
    top_page = esx_ide_bank_alloc(0);
    btm_page = esx_ide_bank_alloc(0);

    // We're going to trash this area for the Editor canvas, so let's back it up so we can restore it
    bankedTextmodeBackup();

    // Load Cinema.ch8 font, taken from https://damieng.com/typography/zx-origins/cinema
    dzx7_standard(((unsigned int *)font), ((unsigned char *)0x5D00));
    memset(0x5C00, 0, 8);               // Make val 0 also blank

    // 0x6E (110) R/W =>  Tilemap Base Address
    //  bits 7-6 = Read back as zero, write values ignored
    //  bits 5-0 = MSB of address of the tilemap in Bank 5
    ZXN_NEXTREG(0x6e, 0x6C);                                    // tilemap base address is 0x6C00

    // 0x6F (111) R/W => Tile Definitions Base Address
    //  bits 7-6 = Read back as zero, write values ignored
    //  bits 5-0 = MSB of address of tile definitions in Bank 5
    ZXN_NEXTREG(0x6f, 0x5C);                                    // base address 0x5c00 (vis.chars(32+) at 0x5D00)

    ZXN_NEXTREG(REG_GLOBAL_TRANSPARENCY_COLOR, 0xE3);
    ZXN_NEXTREG(REG_FALLBACK_COLOR, 0x00);

    // Select ULA palette
    ZXN_NEXTREG(0x43, 0x00);                                    // 0x43 (67) => Palette Control, 00 is ULA
    // Set Magenta back to proper E3
    ZXN_NEXTREGA(REG_PALETTE_INDEX, 27);
    ZXN_NEXTREGA(REG_PALETTE_VALUE_8, 0xE3);

    ZXN_NEXTREG(REG_PALETTE_CONTROL, 0x30);                     // 0x43 (67) => Palette Control
    ZXN_NEXTREG(REG_PALETTE_INDEX, 0);
    uint8_t i = 0;
    do {
//BG
        ZXN_NEXTREGA(0x44, tilemap_background[2*(i/32)]);       // 0x44 (68) => 9 bit colour) autoinc after TWO writes
        ZXN_NEXTREGA(0x44, tilemap_background[2*(i/32)+1]);     // 0x44 (68) => 9 bit colour) autoinc after TWO writes
//FG
        ZXN_NEXTREGA(0x44, tilemap_foreground[(i%32)]);         // 0x44 (68) => 9 bit colour) autoinc after TWO writes
        ZXN_NEXTREGA(0x44, tilemap_foreground[(i%32)+1]);       // 0x44 (68) => 9 bit colour) autoinc after TWO writes
    } while ((i = i + 2) != 0);

    zx_border(INK_BLACK);
    ZXN_NEXTREG(0x6b, /*0b11001000*/ 0xC8);                     // enable tilemap, 80x32 mode, 1bit palette

    zx_cls(PAPER_MAGENTA|BRIGHT);

}

void banked_help() {
    uint16_t oldx = screenx;
    uint16_t oldy = screeny;
    screenx = 14;   screeny = 13;
    con_puts("QE, a VI adjacent, by David Given, part of his CPMISH");
    screenx = 18;   screeny = 14;
    con_puts(    "NextZXOS port D. Rimron-Soutter, Stale Pixels");
    screenx = 27;   screeny = 15;
    con_puts(            "Cinema font by Damien Guard");
    screenx = 26;   screeny = 17;
    con_puts(            "Version 11h2 - Build 20201013");
    screenx = 17;   screeny = 19;
    con_puts(   "Here by accident?  Hold CAPS SHIFT and press ZZ");
    screenx = oldx; screeny = oldy;
}

void banked_beep() {
    zx_border(INK_RED);
    ZXN_NEXTREG(REG_TURBO_MODE, 0);
    printf("\x07");
    ZXN_NEXTREG(REG_TURBO_MODE, 3);
    for(uint8_t delay = 60; delay;delay--) {
        WAIT_FOR_SCANLINE(239);
    }
    zx_border(INK_BLACK);
}

void banked_exit() {
    zx_cls(PAPER_WHITE);
    zx_border(INK_WHITE);
    // Files
    esxdos_f_close(file_handle);

    // Free buffers
    esx_ide_bank_free(0, top_page);
    esx_ide_bank_free(0, btm_page);

    // disable textmode
    ZXN_NEXTREG(0x6b, 0);                                    // disable tilemap

    // Restore Textmode tiles
    bankedTextmodeRestore();

    // Finally, restore the original CPU speed
    ZXN_NEXTREGA(REG_TURBO_MODE, orig_cpu_speed);
}