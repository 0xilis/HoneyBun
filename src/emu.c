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
uint16_t af;
uint16_t bc;
uint16_t de;
uint16_t hl;
uint16_t sp; /* stack pointer */
uint16_t pc = 0x100;

/* Condition codes */
int isZero = 1;

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

void handle_events(void) {
  SDL_Event event;
  SDL_PollEvent(&event);
  /* This should probably be a switch case... */
  if (event.type == SDL_QUIT) {
    running = 0;
  } else if (event.type == SDL_KEYDOWN) {
    cycle = 1;
    const char *key = SDL_GetKeyName(event.key.keysym.sym);
    const char keyfast = *key;
    /* imagine using switch cases smh */
    if (keyfast == '1') {
      keyPressed = 1;
    } else if (keyfast == '2') {
      keyPressed = 2;
    } else if (keyfast == '3') {
      keyPressed = 3;
    } else if (keyfast == '4') {
      keyPressed = 12;
    } else if (keyfast == 'q') {
      keyPressed = 4;
    } else if (keyfast == 'w') {
      keyPressed = 5;
    } else if (keyfast == 'e') {
      keyPressed = 6;
    } else if (keyfast == 'r') {
      keyPressed = 13;
    } else if (keyfast == 'a') {
      keyPressed = 7;
    } else if (keyfast == 's') {
      keyPressed = 8;
    } else if (keyfast == 'd') {
      keyPressed = 9;
    } else if (keyfast == 'f') {
      keyPressed = 14;
    } else if (keyfast == 'z') {
      keyPressed = 10;
    } else if (keyfast == 'x') {
      keyPressed = 0;
    } else if (keyfast == 'c') {
      keyPressed = 11;
    } else if (keyfast == 'v') {
      keyPressed = 15;
    }
  }
  if (!cycle) {
    /* Paused; return */
    return;
  }

  /* execute instruction */
  /* https://gbdev.io/gb-opcodes/optables/ */
  uint8_t instr = emuRAM[pc];
  pc++;
  if (!instr) {
    /* nop */
  } else if (instr == 0x05) {
    /* DEC B */
    uint8_t b = (bc >> 8) & 0xFF;
    b--;
    if (b) {
      isZero = 0;
    } else {
      isZero = 1;
    }
    bc = (b << 8) | (bc & 0x00FF);
  } else if (instr == 0x06) {
    /* LD B, n8 */
    bc &= 0x00FF;
    bc += (emuRAM[pc] << 8);
    pc++;
  } else if (instr == 0x0E) {
    /* LD C, n8 */
    bc &= 0xFF00;
    bc += emuRAM[pc];
    pc++;
  } else if (instr == 0x0D) {
    /* DEC C */
    uint8_t c = bc & 0xFF;
    c--;
    if (c) {
      isZero = 0;
    } else {
      isZero = 1;
    }
    bc = (bc & 0xFF00) | c;
  } else if (instr == 0x20) {
    /* JR nz, e8 */
    /* Jump if not zero */
    if (!isZero) {
      uint8_t jp = emuRAM[pc];
      if (jp & 0x80) {
        jp = (jp ^ 0xFF) + 1;
        pc -= jp;
      } else {
        pc += jp;
      }
    }
  } else if (instr == 0x21) {
    /* LD HL, n16 */
    uint16_t addr = (emuRAM[pc+1] << 8) | emuRAM[pc];
    hl = emuRAM[addr];
    pc += 2;
  } else if (instr == 0x31) {
    /* LD [HL+], a / LDI [HL], A */
    emuRAM[hl] = ((af >> 8) & 0x00FF);
    hl++;
  } else if (instr == 0x32) {
    /* LD [HL-], a / LDD [HL], A */
    emuRAM[hl] = ((af >> 8) & 0x00FF);
    hl--;
  } else if (instr == 0x3E) {
    /* LD A, n8 */
    af &= 0x00FF;
    uint16_t addr = emuRAM[pc] + 0xFF00;
    af += (emuRAM[addr] << 8);
    pc++;
  } else if (instr == 0xAF) {
    /* XOR A, A */
    af &= 0x00FF;
  } else if (instr == 0xC3) {
    /* JP a16 */
    uint16_t addr = (emuRAM[pc+1] << 8) | emuRAM[pc];
    pc = addr;
  } else if (instr == 0xF3) {
    /* DI */

    /* 
     * Interrupts are not present on this emulator yet.
     * No code for disabling them is here.
     */
  } else {
    printf("Unrecognized opcode: %02x at %02x\n", instr, pc);
    exit(1);
  }
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
  memset(emuRAM + binarySize, 0, 0xFFFF);
  fclose(fp);
  if (bytesRead < binarySize) {
    PMError("failed to read entire file, read %zd, expected %zu\n",bytesRead,binarySize);
    goto cleanup;
  }
  /* Main emu loop */
  while (running) {
    /* Handle events */
    handle_events();

    /* Render game state */
    render();

    SDL_Delay(10);
  }

  /* Cleanup */
  cleanup:
  free(emuRAM);
  free(vram);
  SDL_DestroyRenderer(rend);
  printf("ended emulation.\n");
}