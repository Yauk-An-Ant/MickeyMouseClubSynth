//============================================================================
// main.c  --  Full synth + TFT menu + sequencer with EEPROM persistence.
//
// Control surface
// ---------------
//   Piano keys (7 available): 1 2 3 4 5 6 7
//
//   Menu nav:
//     C   next menu / next row / +1 step
//     8   prev menu / prev row / -1 step
//     9   deeper / refresh
//     #   back one level
//
//   Octave:  A = up, B = down (clamped 2..6)
//
//   Sequencer:
//     *   toggle RECORD.  Starting a new recording wipes the in-memory
//                         pattern; stopping recording writes it to EEPROM.
//     0   toggle PLAY.    Ignored if no pattern exists.
//
// Persistence
// -----------
//   At boot, if the EEPROM contains a valid saved pattern, it is loaded
//   into the sequencer automatically.  After every recording session
//   (when you press '*' to stop), the pattern is saved to EEPROM.  Power
//   cycles and resets preserve your last recording.
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
#include "eeprom.h"

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
    sequencer_init();

    // ---- EEPROM + restore previous session --------------------------------
    // eeprom_init() verifies the chip responds AND writes work by doing a
    // quick write/readback on the last EEPROM byte.  If it fails we carry
    // on without persistence rather than refusing to boot.
    if (eeprom_init()) {
        if (eeprom_load_sequencer()) {
            printf("main: restored %u-step pattern from EEPROM\n", length);
        }
    } else {
        printf("main: EEPROM offline — session-only sequencer\n");
    }

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

    // Tracks the previous sequencer mode so we can detect the RECORD →
    // SEQ_IDLE transition and trigger an auto-save.
    sequencer_mode_t prev_mode = mode;

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
                    init_asdr(attack_time, decay_time,
                              sustain_level, release_time);
                }
                break;
            }
#endif

            // -------- Octave controls ------------------------------------
            if (key == 'A') {
                if (pressed && octave < 6) {
                    octave++;
                    printf("octave: %d\n", octave);
                }
                break;
            }
            if (key == 'B') {
                if (pressed && octave > 2) {
                    octave--;
                    printf("octave: %d\n", octave);
                }
                break;
            }

            // -------- Sequencer mode toggles -----------------------------
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

            if (pressed) {
                int voice = allocate_voice();
                if (voice >= 0) {
                    key_voice[k] = voice;
                    set_note(voice, (note_t)k, octave);
                    voices[voice].envelope_state = ATTACK;
                    voices[voice].envelope_level = 0.0f;
                    if (mode == RECORD) {
                        record_note_on((note_t)k, (uint8_t)octave);
                    }
                }
            } else {
                int voice = key_voice[k];
                if (voice >= 0 && voice < MAX_VOICES) {
                    voices[voice].envelope_state = RELEASE;
                }
                key_voice[k] = -1;
                if (mode == RECORD) {
                    record_note_off((note_t)k, (uint8_t)octave);
                }
            }
        } while (0);

        // ---- Auto-save on RECORD → SEQ_IDLE transition -------------------
        // Done here (not inside sequencer_set_mode) so the multi-ms
        // EEPROM write never runs from an IRQ.
        if (prev_mode == RECORD && mode == SEQ_IDLE) {
            printf("main: auto-saving pattern...\n");
            eeprom_save_sequencer();   // best-effort; errors printed inside
        }
        prev_mode = mode;

        // ---- LCD refresh ----------------------------------------------
#ifdef USE_TFT_MENU
        menu_tick();
#endif
    }

    return 0;
}