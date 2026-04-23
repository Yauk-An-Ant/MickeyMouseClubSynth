//============================================================================
// main.c  --  Full synth + drum machine + TFT menu + sequencer + EEPROM.
//
// Control surface
// ---------------
//   Keys 1-7:  depends on mode (see D below)
//     synth mode (default): piano notes C D E F G A B at the current octave
//     drum mode:            1=kick, 2=snare, 3=hihat, 4=clap,
//                           5=cowbell, 6=ride, 7=tom
//
//   D          toggle between synth mode and drum mode (press only)
//
//   Menu nav:
//     C        next menu / next row / +1 step
//     8        prev menu / prev row / -1 step
//     9        deeper / refresh
//     #        back one level
//
//   Octave:    A up, B down (synth mode only; ignored in drum mode).
//
//   Sequencer:
//     *        toggle RECORD.  Captures synth notes only.  Drum hits
//              during RECORD are heard live but NOT stored to the
//              pattern.  The sequencer playback is synth-only.
//     0        toggle PLAY.    Ignored if no pattern exists.
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
#include "drum.h"

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

// --- Audio state reset -----------------------------------------------------
// Called after any init step that might have scribbled on voice state.
static void reset_audio_state(void) {
    for (int i = 0; i < MAX_VOICES; i++) {
        voices[i].active         = 0;
        voices[i].step           = 0;
        voices[i].offset         = 0;
        voices[i].envelope_state = IDLE;
        voices[i].envelope_level = 0.0f;
    }
    for (int i = 0; i < 12; i++) key_voice[i] = -1;
}

// Kill any currently-sustaining synth voices.  Used when we toggle into
// drum mode so stuck notes don't drone through the drum performance.
static void release_all_synth_voices(void) {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].envelope_state != IDLE) {
            voices[i].envelope_state = RELEASE;
        }
    }
    for (int i = 0; i < 12; i++) key_voice[i] = -1;
}

int main(void) {
    stdio_init_all();
    sleep_ms(500);   // give USB stdio a chance to attach

    // ---- Keypad + audio ---------------------------------------------------
    keypad_init_pins();
    keypad_init_timer();
    init_pwm_audio();
    drum_init();

    init_asdr(0.05f, 0.2f, 0.8f, 0.3f);
    init_distortion(false, 0.3f, 0.8f);
    init_eq(0.5f, 0.5f, 0.5f);
    init_flanger(false, 0.6f, 0.003f, 0.4f, 0.5f);
    init_delay(false, 0.1f, 0.4f, 1.0f);

    // Force volume to max.  If globals.c left it at 0, every note plays
    // at silence and you'd hear only a click transient on press.
    volume = 1800;

    reset_audio_state();

    // ---- Sequencer --------------------------------------------------------
    sequencer_init();

    // ---- EEPROM + restore previous session --------------------------------
    if (eeprom_init()) {
        if (eeprom_load_sequencer()) {
            printf("main: restored %u-step pattern from EEPROM\n", length);
        }
    } else {
        printf("main: EEPROM offline — session-only sequencer\n");
    }
    sequencer_set_mode(SEQ_IDLE);
    reset_audio_state();

    // ---- TFT + menu -------------------------------------------------------
#ifdef USE_TFT_MENU
    init_spi_lcd();
    LCD_Setup();
    LCD_DMA_Init();
    pots_init();
    menu_init();
    init_asdr(attack_time, decay_time, sustain_level, release_time);
    if (volume <= 0) {
        printf("main: menu_init zeroed volume; forcing to max\n");
        volume = 1800;
    }
    reset_audio_state();
#endif

    printf("main: ready (synth mode)\n");

    uint octave     = 3;
    bool drum_mode  = false;    // false = synth (1..7 = piano), true = drums
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

            // -------- Mode toggle (D) ------------------------------------
            // Toggles 1..7 between piano and drums.  On leaving synth
            // mode we release any held voices so nothing drones into
            // the drum performance.
            if (key == 'D') {
                if (pressed) {
                    drum_mode = !drum_mode;
                    if (drum_mode) {
                        release_all_synth_voices();
                        printf("mode: DRUM (1=kick 2=snare 3=hihat "
                               "4=clap 5=cowbell 6=ride 7=tom)\n");
                    } else {
                        printf("mode: SYNTH (1..7 = piano, oct %u)\n",
                               octave);
                    }
                }
                break;
            }

            // -------- Octave controls ------------------------------------
            // Octave is only meaningful in synth mode, but it's fine to
            // adjust anywhere — just takes effect when you come back.
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

            // -------- Keys 1..7: route by mode ---------------------------
            int k = key_index(key);
            if (k < 0 || k > 6) break;   // only digits 1..7 route here

            if (drum_mode) {
                // Drums: only trigger on press (one-shot samples).
                // Releases ignored — the sample plays to completion.
                // Drums are live-only; the sequencer records synth
                // notes only.
                if (pressed) {
                    drum_trigger((uint8_t)k);
                }
                break;
            }

            // --- Synth (piano) path ---
            if (pressed) {
                // If the key somehow still holds a stale voice from a
                // missed release, clean it up before grabbing a new one.
                int old_voice = key_voice[k];
                if (old_voice >= 0 && old_voice < MAX_VOICES) {
                    voices[old_voice].envelope_state = IDLE;
                    voices[old_voice].active         = 0;
                    key_voice[k] = -1;
                }

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
        if (prev_mode == RECORD && mode == SEQ_IDLE) {
            printf("main: auto-saving pattern...\n");
            eeprom_save_sequencer();
        }
        prev_mode = mode;

        // ---- LCD refresh ----------------------------------------------
#ifdef USE_TFT_MENU
        menu_tick();
#endif
    }

    return 0;
}