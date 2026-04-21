//============================================================================
// lcd_dma.c  --  DMA-accelerated LCD pushes (ILI9341 over SPI1).
//
// We reuse lcd.c's globals: the SPI instance pointer `SPI` and the window /
// register / CS helpers exposed via `lcddev`.  Pixel data is pushed in
// 16-bit mode; the SPI format is saved and restored so callers using 8-bit
// commands continue to work.
//
// Guarded by USE_TFT_MENU: compiles to nothing without it.
//============================================================================
#ifdef USE_TFT_MENU

#include "lcd_dma.h"
#include "lcd.h"

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"

// ---- Pulled from lcd.c ------------------------------------------------------
extern spi_inst_t *SPI;
extern lcd_dev_t   lcddev;

// LCD primitives we need (declared in lcd.c).  Declaring here avoids forcing
// a header change if lcd.h doesn't export these.
extern void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
extern void LCD_WriteData16_Prepare(void);
extern void LCD_WriteData16_End(void);

// ---- Local state ------------------------------------------------------------
static int lcd_dma_chan = -1;

void LCD_DMA_Init(void) {
    if (lcd_dma_chan < 0) {
        lcd_dma_chan = dma_claim_unused_channel(true);
    }
}

// Low-level: send `count` 16-bit words from `src` via DMA paced by SPI TX DREQ.
// If `src_incr` is false the same word is sent `count` times (block fill).
static void dma_push_16(const void *src, uint32_t count, bool src_incr) {
    // Make sure any previous blocking SPI activity is done.
    while (spi_is_busy(SPI)) { /* spin */ }

    dma_channel_config cfg = dma_channel_get_default_config(lcd_dma_chan);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg, src_incr);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, spi_get_dreq(SPI, true));  // TX DREQ

    dma_channel_configure(
        lcd_dma_chan, &cfg,
        &spi_get_hw(SPI)->dr,   // dst: SPI data register
        src,                    // src
        count,                  // count (in 16-bit transfers)
        true                    // start immediately
    );

    dma_channel_wait_for_finish_blocking(lcd_dma_chan);
    while (spi_is_busy(SPI)) { /* wait for last word to shift out */ }
}

void LCD_DMA_Fill(int x0, int y0, int x1, int y1, uint16_t color) {
    if (x1 < x0 || y1 < y0) return;
    uint32_t count = (uint32_t)(x1 - x0 + 1) * (uint32_t)(y1 - y0 + 1);

    lcddev.select(1);
    LCD_SetWindow((uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1);
    LCD_WriteData16_Prepare();

    // `color` lives on the stack; DMA reads from it with read-incr disabled.
    static volatile uint16_t fill_word;   // must persist across the call
    fill_word = color;
    dma_push_16((const void *)&fill_word, count, /*src_incr=*/false);

    LCD_WriteData16_End();
    lcddev.select(0);
}

void LCD_DMA_WritePixels(const uint16_t *pixels, uint32_t count) {
    if (!pixels || count == 0) return;
    dma_push_16(pixels, count, /*src_incr=*/true);
}

void LCD_DMA_DrawPicture(int x0, int y0, const Picture *pic) {
    if (!pic) return;
    int x1 = x0 + (int)pic->width  - 1;
    int y1 = y0 + (int)pic->height - 1;
    uint32_t count = (uint32_t)pic->width * (uint32_t)pic->height;

    lcddev.select(1);
    LCD_SetWindow((uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1);
    LCD_WriteData16_Prepare();
    dma_push_16((const void *)pic->pixel_data, count, /*src_incr=*/true);
    LCD_WriteData16_End();
    lcddev.select(0);
}

#endif // USE_TFT_MENU