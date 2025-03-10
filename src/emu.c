/*
 * Copyright (C) 2024 Snoolie K / 0xilis. All rights reserved.
 *
 * This document is the property of Snoolie K / 0xilis.
 * It is considered confidential and proprietary.
 *
 * This document may not be reproduced or transmitted in any form,
 * in whole or in part, without the express written permission of
 * Snoolie K / 0xilis.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_image.h>
#include <inttypes.h>
#include "resource_management.h"
#include "defs.h"

/* Global variables */
SDL_Renderer* rend;
int running = 1;
uint8_t *emuRAM;
uint16_t ri; /* The 16-bit register I */
uint8_t keyPressed = 0;
int cycle = 1;

/* Registers */
uint16_t af = 0;
uint16_t bc = 0;
uint16_t de = 0;
uint16_t hl = 0;
uint16_t sp = 0xFFFE; /* stack pointer */
uint16_t pc = 0x100;

/* Condition codes */
#define Z_FLAG 0x80
#define N_FLAG 0x40
#define H_FLAG 0x20
#define C_FLAG 0x10

/* LCD Status Registers */
uint8_t ly = 0; /* Current scanline (LY register) */
int ly_counter = 0; /* Counter to track cycles per scanline */

void set_flag(uint8_t flag, int condition) {
    if (condition) {
        af |= flag;
    } else {
        af &= ~flag;
    }
}

int get_flag(uint8_t flag) {
    return (af & flag) ? 1 : 0;
}

void render(void) {
    SDL_SetRenderDrawColor(rend, 0, 0, 0, 255);
    SDL_RenderClear(rend);

    /* Game Boy screen dimensions */
    const int SCREEN_WIDTH = 160;
    const int SCREEN_HEIGHT = 144;

    /* Tile dimensions */
    const int TILE_SIZE = 8;

    /* Background map dimensions (32x32 tiles) */
    const int MAP_WIDTH = 32;
    const int MAP_HEIGHT = 32;

    /* Iterate over the background map (visible area: 20x18 tiles) */
    for (int mapY = 0; mapY < 18; mapY++) {
        for (int mapX = 0; mapX < 20; mapX++) {
            /* Calculate the tile index in the background map */
            uint16_t mapAddr = 0x9800 + (mapY * MAP_WIDTH) + mapX;
            uint8_t tileIndex = emuRAM[mapAddr];

            /* Correct address calculation for tile data */
            uint16_t tileAddr;
            if (tileIndex < 128) {
                tileAddr = 0x9000 + (tileIndex * 16); /* For tiles 0-127 */
            } else {
                tileAddr = 0x8000 + ((tileIndex - 128) * 16); /* For tiles 128-255 (signed) */
            }

            /* Render the tile */
            for (int tileY = 0; tileY < TILE_SIZE; tileY++) {
                uint8_t byte1 = emuRAM[tileAddr + (tileY * 2)];
                uint8_t byte2 = emuRAM[tileAddr + (tileY * 2) + 1];

                for (int tileX = 0; tileX < TILE_SIZE; tileX++) {
                    /* Extract the color index for the pixel */
                    uint8_t bit1 = (byte1 >> (7 - tileX)) & 1;
                    uint8_t bit2 = (byte2 >> (7 - tileX)) & 1;
                    uint8_t colorIndex = (bit2 << 1) | bit1;

                    /* Map the color index to an actual color */
                    uint8_t r, g, b;
                    switch (colorIndex) {
                        case 0: r = 255; g = 255; b = 255; break; /* White */
                        case 1: r = 192; g = 192; b = 192; break; /* Light gray */
                        case 2: r = 96;  g = 96;  b = 96;  break; /* Dark gray */
                        case 3: r = 0;   g = 0;   b = 0;   break; /* Black */
                    }

                    /* Calculate the screen position */
                    int screenX = mapX * TILE_SIZE + tileX;
                    int screenY = mapY * TILE_SIZE + tileY;

                    /* Draw the pixel */
                    SDL_SetRenderDrawColor(rend, r, g, b, 255);
                    SDL_RenderDrawPoint(rend, screenX, screenY);
                }
            }
        }
    }

    SDL_RenderPresent(rend);
}

int interrupts_enabled = 1;
int pending_vblank_interrupt = 0;

void handle_vblank_interrupt(void) {
    printf("vblank interrupt\n");
    /* Push PC onto the stack */
    sp -= 2;
    emuRAM[sp] = (pc >> 8) & 0xFF;
    emuRAM[sp + 1] = pc & 0xFF;

    /* Jump to the V-Blank interrupt handler (address 0x0040) */
    pc = 0x0040;

    /* Clear the pending interrupt */
    pending_vblank_interrupt = 0;

    /* Disable further interrupts until explicitly re-enabled */
    interrupts_enabled = 0;
}

void check_interrupts(void) {
    if (interrupts_enabled) {
        /* Check for pending interrupts and handle them */
        if (pending_vblank_interrupt) {
            handle_vblank_interrupt();
        }
        /* Add other interrupt checks here (e.g., LCD STAT, Timer, Serial, Joypad) */
    }
}

void update_ly(int cycles) {
    ly_counter += cycles;
    if (ly_counter >= 456) { /* Each scanline takes 456 cycles */
        ly_counter -= 456;
        ly++;
        if (ly > 153) { /* Wrap around after 153 */
            ly = 0;
        }
        emuRAM[0xFF44] = ly; /* Update LY register in memory */
    }
}

void handle_events(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running = 0;
        } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            const char *key = SDL_GetKeyName(event.key.keysym.sym);
            const char keyfast = *key;

            if (event.type == SDL_KEYDOWN) {
                cycle = 1; /* Resume emulation if paused */
                if (keyfast == '1') keyPressed = 1;
                else if (keyfast == '2') keyPressed = 2;
                else if (keyfast == '3') keyPressed = 3;
                else if (keyfast == '4') keyPressed = 12;
                else if (keyfast == 'q') keyPressed = 4;
                else if (keyfast == 'w') keyPressed = 5;
                else if (keyfast == 'e') keyPressed = 6;
                else if (keyfast == 'r') keyPressed = 13;
                else if (keyfast == 'a') keyPressed = 7;
                else if (keyfast == 's') keyPressed = 8;
                else if (keyfast == 'd') keyPressed = 9;
                else if (keyfast == 'f') keyPressed = 14;
                else if (keyfast == 'z') keyPressed = 10;
                else if (keyfast == 'x') keyPressed = 0;
                else if (keyfast == 'c') keyPressed = 11;
                else if (keyfast == 'v') keyPressed = 15;
            } else if (event.type == SDL_KEYUP) {
                /* Handle key releases if needed */
                if (keyfast == '1' && keyPressed == 1) keyPressed = 0;
                else if (keyfast == '2' && keyPressed == 2) keyPressed = 0;
                /* Add other key mappings... */
            }
        }
    }
}

/* Dumb hack */
uint16_t lastpc[64] = {0};
int lastpccount;
void signal_function_call(uint16_t pc) {
    uint16_t lastpc2[64];
    lastpc2[0] = pc;
    memcpy((uint8_t *)lastpc2 + 16, lastpc, sizeof(lastpc2) - 16);
    memcpy(lastpc, lastpc2, 128);
}
uint16_t signal_function_ret() {
    uint16_t ret = lastpc[0];
    memcpy(lastpc, lastpc + 16, sizeof(lastpc) - 16);
    lastpc[63] = 0;
    return ret;
}

int execute_instruction(void) {
    if (!cycle) {
        return 0; // Emulation is paused
    }

    /* Fetch and execute instruction */
    /* https://gbdev.io/gb-opcodes/optables/ */
    uint8_t instr = emuRAM[pc];
    pc++;
    /*printf("instr: %02x (%02x)\n", instr, pc);*/

    switch (instr) {
        case 0x00: /* NOP */
            return 4;

        case 0x01: /* LD BC, n16 */
            {
                uint16_t n16 = (emuRAM[pc + 1] << 8) | emuRAM[pc]; /* Read the 16-bit immediate value */
                pc += 2;
                bc = n16; /* Load n16 into BC */
            }
            return 12;

        case 0x02: /* LD [BC], A */
            {
                emuRAM[bc] = (af >> 8) & 0xFF; /* Store A at the address in BC */
            }
            return 8;

        case 0x03: /* INC BC */
            bc++;
            return 8;

        case 0x04: /* INC B */
            {
                uint8_t b = ((bc >> 8) & 0xFF) + 1;
                set_flag(Z_FLAG, b == 0);
                set_flag(N_FLAG, 0);
                set_flag(H_FLAG, (b & 0x0F) == 0);
                bc = (b << 8) | (bc & 0x00FF);
            }
            return 4;

        case 0x05: /* DEC B */
            {
                uint8_t b = (bc >> 8) & 0xFF;
                b--;
                set_flag(Z_FLAG, b == 0);
                set_flag(N_FLAG, 1);
                set_flag(H_FLAG, (b & 0x0F) == 0x0F);
                bc = (b << 8) | (bc & 0x00FF);
            }
            return 4;

        case 0x06: /* LD B, n8 */
            bc = (bc & 0x00FF) | (emuRAM[pc] << 8);
            pc++;
            return 8;

        case 0x0B: /* DEC BC */
            bc--;
            return 8;

        case 0x0C: /* INC C */
            {
                uint8_t c = (bc & 0xFF) + 1;
                set_flag(Z_FLAG, c == 0);
                set_flag(N_FLAG, 0);
                set_flag(H_FLAG, (c & 0x0F) == 0);
                bc = (bc & 0xFF00) | c;
            }
            return 4;

        case 0x0D: /* DEC C */
            {
                uint8_t c = bc & 0xFF;
                c--;
                set_flag(Z_FLAG, c == 0);
                set_flag(N_FLAG, 1);
                set_flag(H_FLAG, (c & 0x0F) == 0x0F);
                bc = (bc & 0xFF00) | c;
            }
            return 4;

        case 0x0E: /* LD C, n8 */
            bc = (bc & 0xFF00) | emuRAM[pc];
            pc++;
            return 8;

        case 0x11: /* LD DE, n16 */
            de = (emuRAM[pc + 1] << 8) | emuRAM[pc];
            pc += 2;
            return 12;

        case 0x13: /* INC DE */
            de++;
            return 8;

        case 0x14: /* INC D */
            {
                uint8_t d = ((de >> 8) & 0xFF) + 1;
                set_flag(Z_FLAG, d == 0);
                set_flag(N_FLAG, 0);
                set_flag(H_FLAG, (d & 0x0F) == 0);
                de = (d << 8) | (de & 0x00FF);
            }
            return 4;

        case 0x15: /* DEC D */
            {
                uint8_t d = ((de >> 8) & 0xFF) - 1;
                set_flag(Z_FLAG, d == 0);
                set_flag(N_FLAG, 1);
                set_flag(H_FLAG, (d & 0x0F) == 0x0F);
                de = (d << 8) | (de & 0x00FF);
            }
            return 4;

        case 0x16: /* LD D, n8 */
            {
                uint8_t n8 = emuRAM[pc]; /* Read the 8-bit immediate value */
                pc++;
                de = (de & 0x00FF) | (n8 << 8); /* Load n8 into D (upper 8 bits of DE) */
            }
            return 8;

        case 0x18: /* JR e8 */
            {
                int8_t e8 = emuRAM[pc]; /* Read the signed 8-bit offset */
                pc++; /* Move past the offset byte */
        
                /* Add the signed offset to the current pc */
                pc += e8; /* This will jump relative to the current program counter */
            }
            return 12;

        case 0x1A: /* LD A, [DE] */
            af = (af & 0x00FF) | (emuRAM[de] << 8);
            return 8;

        case 0x1B: /* DEC DE */
            de--;
            return 8;

        case 0x1C: /* INC E */
            {
                uint8_t e = (de & 0xFF) + 1;
                set_flag(Z_FLAG, e == 0);
                set_flag(N_FLAG, 0);
                set_flag(H_FLAG, (e & 0x0F) == 0);
                de = (de & 0xFF00) | e;
            }
            return 4;

        case 0x1D: /* DEC E */
            {
                uint8_t e = (de & 0xFF) - 1;
                set_flag(Z_FLAG, e == 0);
                set_flag(N_FLAG, 1);
                set_flag(H_FLAG, (e & 0x0F) == 0x0F);
                de = (de & 0xFF00) | e;
            }
            return 4;

        case 0x20: /* JR NZ, e8 */
            if (!get_flag(Z_FLAG)) {
                int8_t offset = (int8_t)emuRAM[pc];
                pc += offset;
            }
            pc++;
            return 12;

        case 0x21: /* LD HL, n16 */
            hl = (emuRAM[pc + 1] << 8) | emuRAM[pc];
            pc += 2;
            return 12;

        case 0x22: /* LD [HL+], A */
            emuRAM[hl] = (af >> 8) & 0xFF;
            hl++;
            return 8;

        case 0x23: /* INC HL */
            hl++;
            return 8;

        case 0x24: /* INC H */
            {
                uint8_t h = ((hl >> 8) & 0xFF) + 1;
                set_flag(Z_FLAG, h == 0);
                set_flag(N_FLAG, 0);
                set_flag(H_FLAG, (h & 0x0F) == 0);
                hl = (h << 8) | (hl & 0x00FF);
            }
            return 4;

        case 0x25: /* DEC H */
            {
                uint8_t h = ((hl >> 8) & 0xFF) - 1;
                set_flag(Z_FLAG, h == 0);
                set_flag(N_FLAG, 1);
                set_flag(H_FLAG, (h & 0x0F) == 0x0F);
                hl = (h << 8) | (hl & 0x00FF);
            }
            return 4;

        case 0x26: /* LD H, n8 */
            {
                uint8_t n8 = emuRAM[pc];
                pc++;
                hl = (hl & 0x00FF) | (n8 << 8);
            }
            return 8;

        case 0x2A: /* LD A, [HL+] */
            af = (af & 0x00FF) | (emuRAM[hl] << 8);
            hl++;
            return 8;

        case 0x2B: /* DEC HL */
            hl--;
            return 8;

        case 0x2C: /* INC L */
            {
                uint8_t l = (hl & 0xFF) + 1;
                set_flag(Z_FLAG, l == 0);
                set_flag(N_FLAG, 0);
                set_flag(H_FLAG, (l & 0x0F) == 0);
                hl = (hl & 0xFF00) | l;
            }
            return 4;

        case 0x2D: /* DEC L */
            {
                uint8_t l = (hl & 0xFF) - 1; /* Decrement L */
                set_flag(Z_FLAG, l == 0);
                set_flag(N_FLAG, 1);
                set_flag(H_FLAG, (l & 0x0F) == 0x0F); /* Set if borrow from bit 4 */
                hl = (hl & 0xFF00) | l; /* Update L in HL */
            }
            return 4;

        case 0x31: /* LD [HL+], A */
            emuRAM[hl] = (af >> 8) & 0xFF;
            hl++;
            pc += 2;
            return 8;

        case 0x32: /* LD [HL-], A */
            emuRAM[hl] = (af >> 8) & 0xFF;
            hl--;
            return 8;

        case 0x33: /* INC SP */
            sp++;
            return 8;

        case 0x34: /* INC [HL] */
            {
                uint8_t value = emuRAM[hl] + 1; /* Increment the value at [HL] */
                set_flag(Z_FLAG, value == 0);
                set_flag(N_FLAG, 0);
                set_flag(H_FLAG, (value & 0x0F) == 0); /* Set if carry from bit 3 */
                emuRAM[hl] = value; /* Store the updated value back to [HL] */
            }
            return 12;

        case 0x35: /* DEC [HL] */
            {
                uint8_t value = emuRAM[hl] - 1;
                set_flag(Z_FLAG, value == 0);
                set_flag(N_FLAG, 1);
                set_flag(H_FLAG, (value & 0x0F) == 0x0F);
                emuRAM[hl] = value;
            }
            return 12;

        case 0x36: /* LD [HL], n8 */
            {
                uint8_t n8 = emuRAM[pc];
                pc++;
                emuRAM[hl] = n8;
            }
            return 12;

        case 0x38: /* JR C, e8 */
            {
                uint8_t e8 = emuRAM[pc];
                pc++;
        
                if (get_flag(C_FLAG)) {
                    pc += (int8_t)e8;
                }
            }
            return 12;

        case 0x3B: /* DEC SP */
            sp--;
            return 8;

        case 0x3C: /* INC A */
            {
                uint8_t a = ((af >> 8) & 0xFF) + 1;
                set_flag(Z_FLAG, a == 0);
                set_flag(N_FLAG, 0);
                set_flag(H_FLAG, (a & 0x0F) == 0);
                af = (a << 8) | (af & 0x00FF);
            }
            return 4;

        case 0x3D: /* DEC A */
            {
                uint8_t a = ((af >> 8) & 0xFF) - 1;
                set_flag(Z_FLAG, a == 0);
                set_flag(N_FLAG, 1);
                set_flag(H_FLAG, (a & 0x0F) == 0x0F);
                af = (a << 8) | (af & 0x00FF);
            }
            return 4;

        case 0x3E: /* LD A, n8 */
            af = (af & 0x00FF) | (emuRAM[pc] << 8);
            pc++;
            return 8;

        case 0x77: /* LD [HL], A */
            emuRAM[hl] = (af >> 8) & 0xFF;
            return 8;

        case 0x78: /* LD A, B */
            {
                uint8_t b = (bc >> 8) & 0xFF;
                af = (af & 0x00FF) | (b << 8);
            }
            return 4;

        case 0x7B: /* LD A, E */
            af = (af & 0x00FF) | ((de & 0xFF) << 8);
            return 4;

        case 0x90: /* SUB B */
            {
                uint8_t a = (af >> 8) & 0xFF;
                uint8_t b = (bc >> 8) & 0xFF;
                uint8_t result = a - b;
                set_flag(Z_FLAG, result == 0);
                set_flag(N_FLAG, 1);
                set_flag(H_FLAG, (a & 0x0F) < (b & 0x0F));
                set_flag(C_FLAG, a < b);
                af = (af & 0x00FF) | (result << 8);
            }
            return 4;

        case 0xA7: /* AND A, A */
            {
                uint8_t a = (af >> 8) & 0xFF;
                uint8_t result = a & a;

                af = (af & 0xFF00) | result;

                /* Update flags */
                set_flag(Z_FLAG, result == 0);
                set_flag(N_FLAG, 0);
                set_flag(H_FLAG, 1);
                set_flag(C_FLAG, 0);
            }
            return 4;

        case 0xAF: /* XOR A, A */
            af = (af & 0x00FF) | 0x0000;
            set_flag(Z_FLAG, 1);
            set_flag(N_FLAG, 0);
            set_flag(H_FLAG, 0);
            set_flag(C_FLAG, 0);
            return 4;

        case 0xB0: /* OR A, B */
            af = (af & 0x00FF) | ((af >> 8 | (bc >> 8) & 0xFF) & 0xFF);
            set_flag(Z_FLAG, (af >> 8) == 0);
            set_flag(N_FLAG, 0);
            set_flag(H_FLAG, 0);
            set_flag(C_FLAG, 0);
            return 4;

        case 0xB1: /* OR A, C */
            {
                uint8_t a = (af >> 8) & 0xFF;
                uint8_t c = bc & 0xFF;
                uint8_t result = a | c;
                set_flag(Z_FLAG, result == 0);
                set_flag(N_FLAG, 0);
                set_flag(H_FLAG, 0);
                set_flag(C_FLAG, 0);
                af = (af & 0x00FF) | (result << 8);
            }
            return 4;

        case 0xB2: /* OR A, D */
            af = (af & 0x00FF) | ((af >> 8 | (de >> 8) & 0xFF) & 0xFF);
            set_flag(Z_FLAG, (af >> 8) == 0);
            set_flag(N_FLAG, 0);
            set_flag(H_FLAG, 0);
            set_flag(C_FLAG, 0);
            return 4;

        case 0xB3: /* OR A, E */
            af = (af & 0x00FF) | ((af >> 8 | (de & 0xFF)) & 0xFF);
            set_flag(Z_FLAG, (af >> 8) == 0);
            set_flag(N_FLAG, 0);
            set_flag(H_FLAG, 0);
            set_flag(C_FLAG, 0);
            return 4;

        case 0xB4: /* OR A, H */
            af = (af & 0x00FF) | ((af >> 8 | (hl >> 8) & 0xFF) & 0xFF);
            set_flag(Z_FLAG, (af >> 8) == 0);
            set_flag(N_FLAG, 0);
            set_flag(H_FLAG, 0);
            set_flag(C_FLAG, 0);
            return 4;

        case 0xC3: /* JP a16 */
            pc = (emuRAM[pc + 1] << 8) | emuRAM[pc];
            return 16;

        case 0xC9: /* RET */
            {
                /* Pop the return address from the stack */
                uint16_t return_addr = (emuRAM[sp + 1] << 8) | emuRAM[sp];
                sp += 2;

                /* Jump to the return address */
                pc = return_addr;
                /* TODO: implement this instruction rather than this hack */
                pc = signal_function_ret();
            }
            return 16;

        case 0xCD: /* CALL a16 */
            {
                /* Read the 16-bit address */
                uint16_t a16 = (emuRAM[pc + 1] << 8) | emuRAM[pc];
                pc += 2;
                signal_function_call(pc);

                /* Push the return address (current PC) onto the stack */
                sp -= 2;
                emuRAM[sp] = (pc >> 8) & 0xFF;
                emuRAM[sp + 1] = pc & 0xFF;

                /* Jump to the address */
                pc = a16;
            }
            return 24;

        case 0xE0: /* LDH [a8], A */
            {
                uint8_t a8 = emuRAM[pc]; /* Read the 8-bit immediate value */
                pc++;
                uint16_t addr = 0xFF00 + a8; /* Calculate the address */
                emuRAM[addr] = (af >> 8) & 0xFF; /* Write A to [0xFF00 + a8] */
            }
            return 12;

        case 0xE2: /* LDH [C], A */
            {
                uint16_t addr = 0xFF00 + (bc & 0xFF); /* Calculate the address (0xFF00 + C) */
                emuRAM[addr] = (af >> 8) & 0xFF; /* Store A at the address */
            }
            return 8;

        case 0xEA: /* LD [a16], A */
            {
                uint16_t a16 = (emuRAM[pc + 1] << 8) | emuRAM[pc]; /* Read the 16-bit address */
                pc += 2;
                emuRAM[a16] = (af >> 8) & 0xFF; /* Store A at the address */
            }
            return 16;

        case 0xF0: /* LDH A, [a8] */
            {
                uint8_t a8 = emuRAM[pc]; /* Read the 8-bit immediate value */
                pc++;
                uint16_t addr = 0xFF00 + a8; /* Calculate the address */
                uint8_t value = emuRAM[addr]; /* Read the value from [0xFF00 + a8] */
                af = (af & 0x00FF) | (value << 8); /* Load the value into A */
            }
            return 12;

        case 0xF3: /* DI */
            /* Disable interrupts (not implemented yet) */
            interrupts_enabled = 0;
            return 4;

        case 0xFA: /* LD A, [a16] */
            {
                uint16_t a16 = (emuRAM[pc + 1] << 8) | emuRAM[pc];
                pc += 2;
                uint8_t value = emuRAM[a16];
                af = (af & 0x00FF) | (value << 8);
            }
            return 16;

        case 0xFB: /* EI */
            interrupts_enabled = 1;
            return 4;

        case 0xFE: /* CP A, n8 */
            {
                uint8_t a = (af >> 8) & 0xFF; /* Get the value of A */
                uint8_t n8 = emuRAM[pc]; /* Read the 8-bit immediate value */
                pc++;
                uint8_t result = a - n8; /* Perform subtraction (A - n8) */

                /* Update flags */
                set_flag(Z_FLAG, result == 0);
                set_flag(N_FLAG, 1);
                set_flag(H_FLAG, (a & 0x0F) < (n8 & 0x0F));
                set_flag(C_FLAG, a < n8);
            }
            return 8;

        default:
            printf("Unrecognized opcode: %02x at %04x\n", instr, pc - 1);
            exit(1);
    }

    check_interrupts();
}

void emulator(SDL_Window *win, const char *romPath) {
    printf("starting emulator...\n");
    /*
     * According to https://nullprogram.com/blog/2023/01/08/
     * SDL2 already tries to create an accelerated renderer
     * and not specifying allows for a software renderer fallback
     */
    Uint32 render_flags = SDL_RENDERER_PRESENTVSYNC;
    rend = SDL_CreateRenderer(win, -1, render_flags);
    if (!rend) {
        PMError("error creating renderer: %s\n",SDL_GetError());
        return;
    }
    SDL_SetRenderDrawColor(rend, 0, 0, 0, 0);
    SDL_RenderClear(rend);
    /* Support 128x64 later */
    int SCREEN_WIDTH = 160;
    int SCREEN_HEIGHT = 144;
    SDL_RenderSetLogicalSize(rend, SCREEN_WIDTH, SCREEN_HEIGHT);
  
    /* Load ROM into 64KB memory */
    emuRAM = malloc(0xFFFF);
    if (!emuRAM) {
        PMError("unable to allocate the 64KB emuRAM\n");
        SDL_DestroyRenderer(rend);
        return;
    }
    FILE *fp = fopen(romPath, "r");
    if (!fp) {
        PMError("unable to open file input\n");
        SDL_DestroyRenderer(rend);
        return;
    }
    fseek(fp, 0, SEEK_END);
    size_t binarySize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    /* int fd = fp->_file;
    ssize_t bytesRead = read(fd, emuRAM, binarySize); */
    size_t bytesRead = fread(emuRAM, 1, binarySize, fp);
    if (binarySize != 0xFFFF) {
        memset(emuRAM + binarySize, 0, 0xFFFF - binarySize);
    }
    fclose(fp);
    if (bytesRead < binarySize) {
        PMError("failed to read entire file, read %zd, expected %zu\n",bytesRead,binarySize);
        goto cleanup;
    }

    /* Timing and frame rate control */
    const int FRAME_DELAY = 1000 / 60; /* ~16.67ms per frame for 60 FPS */
    const int CYCLES_PER_FRAME = 70224; /* CPU cycles per frame (4.19 MHz / 60 FPS) */
    Uint32 frameStart;
    int frameTime;

    /* Main emu loop */
    while (running) {
        frameStart = SDL_GetTicks();

        /* Handle events */
        handle_events();

        /* Execute a batch of CPU instructions */
        int cyclesThisFrame = 0;
        while (cyclesThisFrame < CYCLES_PER_FRAME) {
            int cycles = execute_instruction();
            cyclesThisFrame += cycles;
            update_ly(cycles); /* Update LY register */
        }


        /* Render game state */
        render();

        /* Maintain consistent frame rate */
        frameTime = SDL_GetTicks() - frameStart;
        if (frameTime < FRAME_DELAY) {
            SDL_Delay(FRAME_DELAY - frameTime);
        }

        /* SDL_Delay(10); */
    }

    /* Cleanup */
    cleanup:
    free(emuRAM);
    SDL_DestroyRenderer(rend);
    printf("ended emulation.\n");
}