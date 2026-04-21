//============================================================================
// main.c  --  Interactive TFT menu test (no audio).
//
// Brings up LCD + pots + keypad, then hands control to the menu.  Audio is
// not initialised in this test — we're focused on display + inputs.
//
// Controls
//   C   — browse: next menu   | edit: next row (Dist/EQ) | adj: +1 step
//   8   — browse: prev menu   | edit: prev row (Dist/EQ) | adj: -1 step
//   9   — go deeper  (enter edit, or drill into a Dist/EQ row)
//   #   — back one level (universal cancel — always gets you out)
//   A   — octave up           (kept for consistency with the full build)
//   B   — octave down
//
// Base menu
//   Has no EDIT mode.  Pot values are sampled only when you switch TO Base
//   or when you press '9' while on Base (manual refresh).  No background
//   polling, no timer IRQ — values hold still between refreshes.
//
// Build
//   USE_TFT_MENU must be visible to every TU.  In platformio.ini:
//     build_flags = -DUSE_TFT_MENU
//============================================================================
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/spi.h"

#include "lcd.h"
#include "lcd_dma.h"
#include "menu.h"
#include "pots.h"
#include "queue.h"

// ---- LCD SPI pin map (must match lcd.c's baked-in pin numbers) -------------
#define PIN_SDI    27
#define PIN_CS     29
#define PIN_SCK    30
#define PIN_DC     32
#define PIN_nRESET 31

extern menu_t current_menu;

// Synth globals — we only read them for the status dump.
extern float attack_time, decay_time, sustain_level, release_time;
extern float distortion, distortion_volume;
extern int   distortion_on;

extern int   flanger_on;
extern float flanger_depth, flanger_rate, flanger_feedback, flanger_mix;

extern int   delay_on;
extern float delay_time, delay_mix, delay_feedback;

extern float lows, mids, highs;
extern int   volume;

// ============================================================================
// Bring-up helpers
// ============================================================================
static void init_spi_lcd(void) {
    gpio_set_function(PIN_CS,     GPIO_FUNC_SIO);
    gpio_set_function(PIN_DC,     GPIO_FUNC_SIO);
    gpio_set_function(PIN_nRESET, GPIO_FUNC_SIO);

    gpio_set_dir(PIN_CS,     GPIO_OUT);
    gpio_set_dir(PIN_DC,     GPIO_OUT);
    gpio_set_dir(PIN_nRESET, GPIO_OUT);

    gpio_put(PIN_CS,     1);
    gpio_put(PIN_DC,     0);
    gpio_put(PIN_nRESET, 1);

    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SDI, GPIO_FUNC_SPI);
    spi_init(spi1, 100 * 1000 * 1000);
    spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

static const char *menu_name(menu_t m) {
    switch (m) {
        case MENU_BASE:       return "Base";
        case MENU_WAVEFORM:   return "Waveform";
        case MENU_DISTORTION: return "Distortion";
        case MENU_FLANGER:    return "Flanger";
        case MENU_DELAY:      return "Delay";
        case MENU_EQUALIZER:  return "Equalizer";
        default:              return "?";
    }
}

// Dump every menu value over USB serial.  Useful for verifying the menu is
// actually writing the synth globals — call it whenever a menu key fires.
static void print_state(void) {
    printf("  [%s]  A=%.2f D=%.2f S=%.2f R=%.2f  vol=%d\n"
           "         dist=%s drive=%.2f dvol=%.2f\n"
           "         flg=%s  dep=%.2f rate=%.2f fb=%.2f mix=%.2f\n"
           "         dly=%s  time=%.2f mix=%.2f fb=%.2f\n"
           "         lo=%.2f mid=%.2f hi=%.2f\n",
           menu_name(current_menu),
           attack_time, decay_time, sustain_level, release_time, volume,
           distortion_on ? "ON" : "OFF",
           distortion, distortion_volume,
           flanger_on ? "ON" : "OFF",
           flanger_depth, flanger_rate, flanger_feedback, flanger_mix,
           delay_on ? "ON" : "OFF",
           delay_time, delay_mix, delay_feedback,
           lows, mids, highs);
}

// ============================================================================
// Main
// ============================================================================
int main(void) {
    stdio_init_all();
    sleep_ms(500);   // give USB stdio a moment to enumerate
    printf("\n\n============================================\n");
    printf("  TFT Menu Interactive Test (no audio)\n");
    printf("============================================\n");
    printf("Keys:  C=next   8=prev   9=deeper/refresh   #=back\n");
    printf("       A=oct+  B=oct-\n");
    printf("Base menu: press 9 to sample the pots (GPIO 41..45).\n\n");

    // ---- LCD first so menu_init() has a live screen.
    init_spi_lcd();
    LCD_Setup();
    LCD_DMA_Init();

    // ---- Pots: turn on the ADC and wire up the pin muxes.  No DMA / IRQ
    //      yet — sampling is driven by the menu's 1 ms Base timer.
    pots_init();

    // ---- Keypad: pins + timer ISRs (owns timer alarms 0 and 1).
    keypad_init_pins();
    keypad_init_timer();

    // ---- Menu: paints initial Base page, starts the 1ms pot timer IRQ
    //            (alarm 2) because Base is the opening page.
    menu_init();
    print_state();

    uint octave = 3;

    // ------------------------------------------------------------ main loop
    for (;;) {
        uint16_t keyevent = key_pop();
        if (keyevent) {
            bool pressed = (keyevent & 0x100) != 0;
            char key     = (char)(keyevent & 0xFF);

            // -------- Menu-reserved keys (presses only).
            if (key == '8' || key == '9' || key == 'C' || key == '#') {
                if (pressed) {
                    printf("KEY '%c' (press)\n", key);
                    menu_handle_key(key);
                    print_state();
                }
            }
            // -------- Octave controls (kept so the key roles match the
            //          production build even though we're not making sound).
            else if (key == 'A') {
                if (pressed && octave < 6) {
                    octave++;
                    printf("KEY 'A' → octave up  = %u\n", octave);
                }
            } else if (key == 'B') {
                if (pressed && octave > 2) {
                    octave--;
                    printf("KEY 'B' → octave dn  = %u\n", octave);
                }
            }
            // -------- Everything else: just log (no audio in this test).
            else {
                if (pressed) printf("KEY '%c' (press, ignored)\n", key);
            }
        }

        // Refresh LCD (diff-based; cheap when nothing moved).  The Base
        // pot IRQ writes to the float globals directly, and menu_tick
        // notices the change and redraws the affected rows.
        menu_tick();
    }

    return 0;
}