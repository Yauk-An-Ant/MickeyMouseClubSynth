//============================================================================
// lcd_dma.h  --  DMA helpers for the ILI9341 TFT on SPI1.
//
// Piggy-backs on the SPI setup already done by lcd.c (SPI1, 16-bit mode for
// pixel pushes).  All helpers wait for any in-flight blocking SPI to drain
// before starting, and wait for their own transfer to drain before returning.
//
// Guarded by USE_TFT_MENU: compiles to nothing without it.
//============================================================================
#ifndef LCD_DMA_H
#define LCD_DMA_H

#ifdef USE_TFT_MENU

#include <stdint.h>
#include "lcd.h"   // u16, Picture

// Claim a DMA channel for LCD transfers.  Call once, after LCD_Setup().
void LCD_DMA_Init(void);

// Fill the rectangle (x0,y0)-(x1,y1) inclusive with a single color.
// Uses a read-fixed / write-fixed DMA with a local color register.
void LCD_DMA_Fill(int x0, int y0, int x1, int y1, uint16_t color);

// Push `count` 16-bit pixel words from `pixels[]` to the currently open
// display window.  Caller must have set the window + issued WriteRAM already.
void LCD_DMA_WritePixels(const uint16_t *pixels, uint32_t count);

// DrawPicture equivalent using DMA for the pixel push.
void LCD_DMA_DrawPicture(int x0, int y0, const Picture *pic);

#endif // USE_TFT_MENU
#endif // LCD_DMA_H