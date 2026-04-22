//============================================================================
// main.c  --  Full synth + TFT menu + sequencer.
//
// Control surface
// ---------------
//   Piano keys (7 available — * and 0 were given to the sequencer):
//     1  2  3  4  5  6  7
//
//   Menu nav:
//     C   next menu / next row / +1 step
//     8   prev menu / prev row / -1 step
//     9   deeper / refresh (enter EDIT, drill into row, or refresh pots on Base)
//     #   back one level   (EDIT_VALUE → EDIT → BROWSE)
//
//   Octave:  A = up, B = down (clamped 2..6)
//
//   Sequencer:
//     *   toggle RECORD   (press once to start, again to stop.  Starting
//                         recording wipes the previous pattern.)
//     0   toggle PLAY     (press once to loop the pattern, again to stop.
//                         Ignored if no pattern has been recorded.)
//
// Menus (left → right):
//   Base        — ASDR + master volume (pots on GPIO 41..45, press 9 to refresh)
//   Waveform    — shows current wave + preview
//   Distortion  — Power / Drive / Volume
//   Flanger     — Power / Depth / Rate / Feedback / Mix
//   Delay       — Power / Time / Mix / Feedback
//   Equalizer   — Low / Mid / High
//
// Build flags
//   USE_TFT_MENU must be defined for every TU.  In platformio.ini:
//     build_flags = -DUSE_TFT_MENU
//============================================================================
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#include "queue.h"
#include "audio.h"
#include "support.h"
#include "sequencer.h"

#ifdef USE_TFT_MENU
  #include "hardware/spi.h"
  #include "lcd.h"
  #include "lcd_dma.h"
  #include "menu.h"
  #include "pots.h"

  #define PIN_SDI    27
  #define PIN_CS     29
  #define PIN_SCK    30
  #define PIN_DC     32
  #define PIN_nRESET 31

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
      // 25 MHz — slower than 100 MHz but the current spikes from full-screen
      // fills are ~4x smaller, reducing PWM audio noise on menu switches.
      spi_init(spi1, 25 * 1000 * 1000);
      spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  }
#endif

int main(void) {
    stdio_init_all();

    // ---- Keypad + audio ---------------------------------------------------
    keypad_init_pins();
    keypad_init_timer();
    init_pwm_audio();

    // Effect defaults.  The menu will overwrite these in menu_init() below.
    init_asdr(0.01f, 0.1f, 1.0f, 0.2f);
    init_distortion(true,  0.3f, 0.8f);
    init_eq(0.5f, 0.5f, 0.5f);
    init_flanger(false, 0.6f, 0.003f, 0.4f, 0.5f);
    init_delay(false, 0.1f, 0.4f, 1.0f);

    for (int i = 0; i < MAX_VOICES; i++) {
        voices[i].active         = 0;
        voices[i].step           = 0;
        voices[i].offset         = 0;
        voices[i].envelope_state = IDLE;
        voices[i].envelope_level = 0.0f;
    }

    // ---- Sequencer --------------------------------------------------------
    // Must come AFTER init_pwm_audio() since the sequencer tick clock is
    // driven from the PWM IRQ.  sequencer_init() itself just zeroes state —
    // the IRQ calls sequencer_process() which checks mode and does nothing
    // in SEQ_IDLE.
    sequencer_init();

    // ---- TFT + menu -------------------------------------------------------
#ifdef USE_TFT_MENU
    init_spi_lcd();
    LCD_Setup();
    LCD_DMA_Init();
    pots_init();
    menu_init();
    init_asdr(attack_time, decay_time, sustain_level, release_time);
#endif

    uint octave = 3;

    // ----------------------------------------------------------------- loop
    for (;;) {
        do {
            uint16_t keyevent = key_pop();
            if (!keyevent) break;

            bool pressed = (keyevent & 0x100) != 0;
            char key     = (char)(keyevent & 0xFF);

            // -------- Menu-reserved keys ---------------------------------
#ifdef USE_TFT_MENU
            if (key == '8' || key == '9' || key == 'C' || key == '#') {
                if (pressed) {
                    menu_handle_key(key);
                    init_asdr(attack_time, decay_time, sustain_level, release_time);
                }
                // Releases ignored — no key auto-repeat in this build.
                break;
            }
#endif

            // -------- Octave controls ------------------------------------
            if (key == 'A') {
                if (pressed && octave < 6) { octave++; printf("octave: %d\n", octave); }
                break;
            }
            if (key == 'B') {
                if (pressed && octave > 2) { octave--; printf("octave: %d\n", octave); }
                break;
            }

            // -------- Sequencer mode toggles -----------------------------
            // '*' toggles RECORD on/off.  Starting record wipes the pattern.
            // '0' toggles PLAY   on/off.  Ignored if pattern is empty.
            // Both handle press-only; releases are harmless (fall through).
            if (key == '*') {
                if (pressed) {
                    if (mode == RECORD) {
                        sequencer_set_mode(SEQ_IDLE);
                        printf("seq: stop record (length=%d)\n", length);
                    } else {
                        sequencer_set_mode(RECORD);
                        printf("seq: record\n");
                    }
                }
                break;
            }
            if (key == '0') {
                if (pressed) {
                    if (mode == PLAY) {
                        sequencer_set_mode(SEQ_IDLE);
                        printf("seq: stop play\n");
                    } else if (length > 0) {
                        sequencer_set_mode(PLAY);
                        printf("seq: play (length=%d)\n", length);
                    } else {
                        printf("seq: nothing recorded\n");
                    }
                }
                break;
            }

            // -------- Piano keys -----------------------------------------
            int k = key_index(key);
            if (k < 0) break;
            // key_index() returns slots 9 and 10 for '*' and '0', but we've
            // already handled those above and broken out.  Same for 8/9/#
            // in the menu branch.  So by the time we reach here, k maps to
            // one of the 1..7 note slots.

            if (pressed) {
                int voice = allocate_voice();
                if (voice >= 0) {
                    key_voice[k] = voice;
                    set_note(voice, (note_t)k, octave);
                    voices[voice].envelope_state = ATTACK;
                    voices[voice].envelope_level = 0.0f;

                    // If we're recording, capture this note into the pattern.
                    // tie=false means every note retriggers (no ties from
                    // the keypad — you'd add a modifier key for that).
                    if (mode == RECORD) {
                        record((note_t)k, (uint8_t)octave,
                               (uint8_t)voice, /*tie=*/false);
                    }
                }
            } else {
                int voice = key_voice[k];
                if (voice >= 0 && voice < MAX_VOICES) {
                    voices[voice].envelope_state = RELEASE;
                }
                key_voice[k] = -1;
            }
        } while (0);

        // ---- LCD refresh ----------------------------------------------
#ifdef USE_TFT_MENU
        menu_tick();
#endif
    }

    return 0;
}