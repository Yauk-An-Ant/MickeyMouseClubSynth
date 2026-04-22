//============================================================================
// main.c  --  Full synth + TFT menu.
//
// Control surface
// ---------------
//   Piano keys (play notes, octave-dependent):
//     1  2  3  4  5  6  7  *  0
//   (keys 8, 9, C, #, A, B are reserved — see below.  D is unused.)
//
//   Menu nav (always active):
//     C   next menu / next row / +1 step
//     8   prev menu / prev row / -1 step
//     9   deeper / refresh (enter EDIT, drill into row, or refresh pots on Base)
//     #   back one level   (EDIT_VALUE → EDIT → BROWSE)
//
//   Octave:  A = up, B = down (clamped 2..6)
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
// -----------
//   USE_TFT_MENU must be defined for every TU.  In platformio.ini:
//     build_flags = -DUSE_TFT_MENU
//   Drop it to build a headless synth (no LCD/menu code linked in).
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

#ifdef USE_TFT_MENU
  #include "hardware/spi.h"
  #include "lcd.h"
  #include "lcd_dma.h"
  #include "menu.h"
  #include "pots.h"

  // LCD SPI pin map (must match lcd.c's baked-in pin numbers).
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
      // NOTE: LCD SPI speed was originally 100 MHz, but the current spikes
      // from full-screen DMA fills (triggered on every menu switch) were
      // coupling into the 3V3 rail and appearing as static on the PWM audio
      // output.  Dropping to 25 MHz makes the spikes ~4x smaller and
      // proportionally longer, which measurably reduces the audible pop.
      // A full-screen fill at 25 MHz takes ~45 ms instead of ~11 ms, which
      // is still fast enough that you can't see it.
      spi_init(spi1, 25 * 1000 * 1000);
      spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  }
#endif

int main(void) {
    stdio_init_all();

    // ---- Keypad + audio (unchanged from your original main.c) -------------
    keypad_init_pins();
    keypad_init_timer();
    init_pwm_audio();

    // Effect defaults.  You had these in your original main — still works,
    // but note that init_asdr() also recomputes attack_inc/decay_dec/
    // release_dec from the time constants, so always re-call it after
    // changing those four values from the menu (see the 9-key handler).
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

    // ---- TFT + menu -------------------------------------------------------
#ifdef USE_TFT_MENU
    init_spi_lcd();
    LCD_Setup();
    LCD_DMA_Init();
    pots_init();                 // ADC on, GPIO muxed; sampling is on-demand

    // menu_init() writes its own defaults into the synth globals, clobbering
    // the init_* calls above.  If you want to preserve your exact defaults
    // instead, delete the writes at the top of menu_init() in menu.c.
    menu_init();

    // menu_init() may have changed attack_time/decay_time/etc. — recompute
    // the audio-IRQ-facing increments so the sound matches the displayed
    // values.
    init_asdr(attack_time, decay_time, sustain_level, release_time);
#endif

    uint octave  = 3;

    // ----------------------------------------------------------------- loop
    for (;;) {
        // ---- Keypad dispatch ------------------------------------------
        // Wrapped so we can `break` out to reach menu_tick() below.  Using
        // `continue` here would skip the LCD refresh and the menu's screen
        // would never update.
        do {
            uint16_t keyevent = key_pop();
            if (!keyevent) break;

            bool pressed = (keyevent & 0x100) != 0;
            char key     = (char)(keyevent & 0xFF);

            // Menu-reserved keys: 8, 9, C, # (menu navigation).  Releases
            // are ignored.
#ifdef USE_TFT_MENU
            if (key == '8' || key == '9' || key == 'C' || key == '#') {
                if (!pressed) break;
                menu_handle_key(key);
                // The menu may have rewritten attack/decay/sustain/release
                // (pot refresh on '9', or Dist/EQ/Flanger/Delay EDIT_VALUE
                // on 8/C).  Re-derive the ADSR increments so the audio IRQ
                // stays in sync.
                init_asdr(attack_time, decay_time, sustain_level, release_time);
                break;
            }
#endif

            // Octave controls.
            if (key == 'A') {
                if (pressed && octave < 6) { octave++; printf("octave: %d\n", octave); }
                break;
            }
            if (key == 'B') {
                if (pressed && octave > 2) { octave--; printf("octave: %d\n", octave); }
                break;
            }

            // Piano keys.  (D is available — key_index returns -1 for it,
            // so it falls through harmlessly.  The Waveform menu covers
            // wave-type changes.)
            int k = key_index(key);
            if (k < 0) break;

            if (pressed) {
                int voice = allocate_voice();
                if (voice >= 0) {
                    key_voice[k] = voice;
                    set_note(voice, (note_t)k, octave);
                    voices[voice].envelope_state = ATTACK;
                    voices[voice].envelope_level = 0.0f;
                }
            } else {
                int voice = key_voice[k];
                if (voice >= 0 && voice < MAX_VOICES) {
                    voices[voice].envelope_state = RELEASE;
                }
                key_voice[k] = -1;
            }
        } while (0);

        // ---- LCD refresh ---------------------------------------------
        // Diff-based — cheap when nothing changed.  Must run every loop so
        // pot-driven value changes on Base (triggered by the '9' refresh)
        // actually make it onto the screen.
#ifdef USE_TFT_MENU
        menu_tick();
#endif
    }

    return 0;
}