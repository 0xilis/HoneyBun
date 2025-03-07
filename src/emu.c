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
char *vram;
int running = 1;
uint8_t *emuRAM;
uint16_t ri; /* The 16-bit register I */
uint16_t stack[16];
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

int draw_pixel(int x, int y) {
  /* Support 128x64 later */
  int SCREEN_WIDTH = 64;
  int SCREEN_HEIGHT = 64;
  /* Like a CRT, each line from left to right then another line */
  int pixelId = (y * SCREEN_WIDTH) + x;
  char pixel = vram[pixelId];
  int pixelDrew;
  if (pixel) {
    /* Pixel set on, turn it off! */
    SDL_SetRenderDrawColor(rend, 0, 0, 0, 255);
    pixelDrew = 0;
  } else {
    /* Pixel set off, turn it on! */
    SDL_SetRenderDrawColor(rend, 255, 255, 255, 255);
    pixelDrew = 1;
  }
  vram[pixelId] = pixelDrew;
  SDL_RenderDrawPoint(rend, x, y);
  return pixelDrew;
}

void cls(void) {
  /* Support 128x64 later */
  int SCREEN_WIDTH = 64;
  int SCREEN_HEIGHT = 64;
  memset(vram,0, SCREEN_WIDTH * SCREEN_HEIGHT);
  SDL_SetRenderDrawColor(rend, 0, 0, 0, 255);
  SDL_RenderClear(rend);
}

void redraw(void) {
  /* Support 128x64 later */
  int SCREEN_WIDTH = 64;
  int SCREEN_HEIGHT = 64;
  /* Like a CRT, each line from left to right then another line */
  int pixelId = 0;
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      if (vram[pixelId]) {
        SDL_SetRenderDrawColor(rend, 255, 255, 255, 255);
        SDL_RenderDrawPoint(rend, x, y);
      } else {
        SDL_SetRenderDrawColor(rend, 0, 0, 0, 255);
        SDL_RenderDrawPoint(rend, x, y);
      }
      pixelId++;
    }
  }
}

void render(void) {
  SDL_SetRenderDrawColor(rend, 0, 0, 0, 255);
  SDL_RenderClear(rend);
  redraw();
  SDL_RenderPresent(rend);
}

int interrupts_enabled = 1; // Global flag to enable/disable interrupts
int pending_vblank_interrupt = 0; // Example: V-Blank interrupt pending

void handle_vblank_interrupt(void) {
    printf("vblank interrupt\n");
    // Push PC onto the stack
    sp -= 2;
    emuRAM[sp] = (pc >> 8) & 0xFF;
    emuRAM[sp + 1] = pc & 0xFF;

    // Jump to the V-Blank interrupt handler (address 0x0040)
    pc = 0x0040;

    // Clear the pending interrupt
    pending_vblank_interrupt = 0;

    // Disable further interrupts until explicitly re-enabled
    interrupts_enabled = 0;
}

void check_interrupts(void) {
    if (interrupts_enabled) {
        // Check for pending interrupts and handle them
        if (pending_vblank_interrupt) {
            handle_vblank_interrupt();
        }
        // Add other interrupt checks here (e.g., LCD STAT, Timer, Serial, Joypad)
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
                // Handle key releases if needed
                if (keyfast == '1' && keyPressed == 1) keyPressed = 0;
                else if (keyfast == '2' && keyPressed == 2) keyPressed = 0;
                // Add other key mappings...
            }
        }
    }
}

int execute_instruction(void) {
    if (!cycle) {
        return 0; // Emulation is paused
    }

    /* Fetch and execute instruction */
    uint8_t instr = emuRAM[pc];
    pc++;
    printf("instr: %02x (%02x)\n", instr, pc);

    switch (instr) {
        case 0x00: /* NOP */
            return 4; // Return cycle count for NOP

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

        case 0x1A: /* LD A, [DE] */
            af = (af & 0x00FF) | (emuRAM[de] << 8);
            return 8;

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

        case 0x2A: /* LD A, [HL+] */
            af = (af & 0x00FF) | (emuRAM[hl] << 8);
            hl++;
            return 8;

        case 0x31: /* LD [HL+], A */
            emuRAM[hl] = (af >> 8) & 0xFF;
            hl++;
            return 8;

        case 0x32: /* LD [HL-], A */
            emuRAM[hl] = (af >> 8) & 0xFF;
            hl--;
            return 8;

        case 0x3E: /* LD A, n8 */
            af = (af & 0x00FF) | (emuRAM[pc] << 8);
            pc++;
            return 8;

        case 0x77: /* LD [HL], A */
            emuRAM[hl] = (af >> 8) & 0xFF;
            return 8;

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

        case 0xAF: /* XOR A, A */
            af = (af & 0x00FF) | 0x0000;
            set_flag(Z_FLAG, 1);
            set_flag(N_FLAG, 0);
            set_flag(H_FLAG, 0);
            set_flag(C_FLAG, 0);
            return 4;

        case 0xC3: /* JP a16 */
            pc = (emuRAM[pc + 1] << 8) | emuRAM[pc];
            return 16;

        case 0xE0: /* LDH [a8], A */
            {
                uint8_t a8 = emuRAM[pc]; // Read the 8-bit immediate value
                pc++;
                uint16_t addr = 0xFF00 + a8; // Calculate the address
                emuRAM[addr] = (af >> 8) & 0xFF; // Write A to [0xFF00 + a8]
            }
            return 12; // 12 cycles

        case 0xF0: /* LDH A, [a8] */
            {
                uint8_t a8 = emuRAM[pc]; // Read the 8-bit immediate value
                pc++;
                uint16_t addr = 0xFF00 + a8; // Calculate the address
                uint8_t value = emuRAM[addr]; // Read the value from [0xFF00 + a8]
                af = (af & 0x00FF) | (value << 8); // Load the value into A
            }
            return 12;

        case 0xF3: /* DI */
            // Disable interrupts (not implemented yet)
            interrupts_enabled = 0; // Disable interrupts
            return 4;

        case 0xFA: /* LD A, [a16] */
            {
                uint16_t a16 = (emuRAM[pc + 1] << 8) | emuRAM[pc]; // Read the 16-bit address
                pc += 2;
                uint8_t value = emuRAM[a16]; // Read the value from [a16]
                af = (af & 0x00FF) | (value << 8); // Load the value into A
            }
            return 16;

        case 0xFB: /* EI */
            interrupts_enabled = 1; // Enable interrupts
            return 4; // 4 cycles

        case 0xFE: /* CP A, n8 */
            {
                uint8_t a = (af >> 8) & 0xFF; // Get the value of A
                uint8_t n8 = emuRAM[pc]; // Read the 8-bit immediate value
                pc++;
                uint8_t result = a - n8; // Perform subtraction (A - n8)

                // Update flags
                set_flag(Z_FLAG, result == 0);
                set_flag(N_FLAG, 1);
                set_flag(H_FLAG, (a & 0x0F) < (n8 & 0x0F)); // Borrow from bit 4
                set_flag(C_FLAG, a < n8); // Borrow (A < n8)
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
  int SCREEN_WIDTH = 64;
  int SCREEN_HEIGHT = 64;
  SDL_RenderSetLogicalSize(rend, SCREEN_WIDTH, SCREEN_HEIGHT);
  vram = malloc(SCREEN_WIDTH * SCREEN_HEIGHT);
  if (!vram) {
    PMError("unable to allocate the 8KB VRAM\n");
    SDL_DestroyRenderer(rend);
    return;
  }
  /* initialize the initial vram to be all black at first. */
  memset(vram, 0, SCREEN_WIDTH * SCREEN_HEIGHT);
  
  /* Load ROM into 64KB memory */
  emuRAM = malloc(0xFFFF);
  if (!emuRAM) {
    PMError("unable to allocate the 64KB emuRAM\n");
    free(vram);
    SDL_DestroyRenderer(rend);
    return;
  }
  FILE *fp = fopen(romPath, "r");
  if (!fp) {
    PMError("unable to open file input\n");
    free(vram);
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
  const int FRAME_DELAY = 1000 / 60; // ~16.67ms per frame for 60 FPS
  const int CYCLES_PER_FRAME = 70224; // CPU cycles per frame (4.19 MHz / 60 FPS)
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
    }


    /* Render game state */
    render();

    /* Maintain consistent frame rate */
    frameTime = SDL_GetTicks() - frameStart;
    if (frameTime < FRAME_DELAY) {
      SDL_Delay(FRAME_DELAY - frameTime);
    }

    SDL_Delay(10);
  }

  /* Cleanup */
  cleanup:
  free(emuRAM);
  free(vram);
  SDL_DestroyRenderer(rend);
  printf("ended emulation.\n");
}