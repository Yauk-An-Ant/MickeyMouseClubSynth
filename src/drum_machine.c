/*
 * drum_machine.c — 808 Drum Machine for RP2350
 *
 * Uses your exact:
 *   - keypad.c  (pull-down rows GPIO 2-5, cols GPIO 6-9, timer0_hw alarms)
 *   - queue.c   (key_push / key_pop, KeyEvents kev)
 *   - support.c (wavetable[], N, RATE, step0/1, offset0/1, wave_t, note_t)
 *   - PWM on GPIO 36, output scaled to pwm top (same as your synth)
 *
 * Key bindings:
 *   1       — Kick   live trigger + select for step-edit
 *   2       — Snare  live trigger + select for step-edit
 *   3       — Hi-Hat live trigger + select for step-edit
 *   4       — Clap   live trigger + select for step-edit
 *   5 6 7 8
 *   9 0 * # — toggle steps 0-7 for the selected drum
 *   A       — swap step bank (steps 0-7  ↔  steps 8-15)
 *   B       — BPM + 10
 *   C       — Play / Stop
 *   D       — BPM - 10
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "support.h"   /* wavetable[], N, RATE, wave_t, note_t … */
#include "queue.h"     /* KeyEvents kev, key_push(), key_pop()    */

/* ── forward declarations from keypad.c ─────────────────────────────── */
void keypad_init_pins(void);
void keypad_init_timer(void);

/* ── audio pin (matches your init_pwm_audio exactly) ────────────────── */
#define AUDIO_PIN   36
#define AUDIO_SLICE (8u + (((AUDIO_PIN) >> 1u) & 3u))
#define AUDIO_CHAN  ((AUDIO_PIN) & 1u)

/* ── sequencer ───────────────────────────────────────────────────────── */
#define NUM_DRUMS    4
#define NUM_STEPS   16
#define NUM_PATTERNS 4
#define DEFAULT_BPM 120
#define MIN_BPM      60
#define MAX_BPM     200

#define DRUM_KICK   0
#define DRUM_SNARE  1
#define DRUM_HIHAT  2
#define DRUM_CLAP   3

/* ── drum voice: each is a short PCM buffer in RAM ──────────────────── */
typedef struct {
    short int       *buf;
    uint32_t         len;
    volatile uint32_t pos;
    volatile bool    active;
} DrumVoice;

static short int kick_buf [RATE / 2];    /* 0.5 s  */
static short int snare_buf[RATE / 8];    /* 125 ms */
static short int hihat_buf[RATE / 16];   /*  62 ms */
static short int clap_buf [RATE / 8];    /* 125 ms */

static DrumVoice voices[NUM_DRUMS];

/* ── sequencer state ─────────────────────────────────────────────────── */
static bool    pat[NUM_PATTERNS][NUM_DRUMS][NUM_STEPS];
static uint8_t current_pat  = 0;
static uint8_t current_step = 0;
static bool    seq_playing  = false;
static int     bpm          = DEFAULT_BPM;
static uint8_t edit_drum    = DRUM_KICK;
static uint8_t step_bank    = 0;    /* 0 = steps 0-7,  1 = steps 8-15 */

/* ── non-blocking key_pop ────────────────────────────────────────────── */
/*
 * queue.c's key_pop() blocks with sleep_ms(10).
 * We need non-blocking so seq_tick() keeps running every loop iteration.
 * kev is defined (not just declared) in queue.c, extern here.
 */
extern KeyEvents kev;

static bool key_pop_nb(uint16_t *out) {
    if (kev.head == kev.tail) return false;
    *out = kev.q[kev.tail];
    kev.tail = (kev.tail + 1) % 32;
    return true;
}

/* ═══════════════════════════════════════════════════════════════════════
   DRUM SYNTHESIS
   Buffers use the same 0..32767 range as your wavetable[].
   Generated once at boot — no runtime MP3 decoder needed.
   ═══════════════════════════════════════════════════════════════════════ */

static void synth_kick(void) {
    uint32_t len = RATE / 2;
    for (uint32_t i = 0; i < len; i++) {
        float t    = (float)i / (float)RATE;
        float freq = 150.0f * powf(40.0f / 150.0f, t * 2.0f);  /* 150→40 Hz */
        float env  = expf(-t * 8.0f);
        float s    = sinf(2.0f * (float)M_PI * freq * t) * env;
        kick_buf[i] = (short int)((s * 16383.0f) + 16384.0f);
    }
}

static void synth_snare(void) {
    uint32_t len = RATE / 8;
    for (uint32_t i = 0; i < len; i++) {
        float t     = (float)i / (float)RATE;
        float env   = expf(-t * 30.0f);
        float noise = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        float tone  = sinf(2.0f * (float)M_PI * 200.0f * t);
        float s     = (noise * 0.7f + tone * 0.3f) * env;
        snare_buf[i] = (short int)((s * 16383.0f) + 16384.0f);
    }
}

static void synth_hihat(void) {
    uint32_t len = RATE / 16;
    for (uint32_t i = 0; i < len; i++) {
        float t   = (float)i / (float)RATE;
        float env = expf(-t * 120.0f);
        float s   = (((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f) * env;
        hihat_buf[i] = (short int)((s * 16383.0f) + 16384.0f);
    }
}

static void synth_clap(void) {
    uint32_t len = RATE / 8;
    for (uint32_t i = 0; i < len; i++) {
        float t   = (float)i / (float)RATE;
        float env = expf(-t * 40.0f)
                  + 0.5f * expf(-(t - 0.008f) * 40.0f)
                  + 0.3f * expf(-(t - 0.016f) * 40.0f);
        float s = (((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f) * env * 0.5f;
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        clap_buf[i] = (short int)((s * 16383.0f) + 16384.0f);
    }
}

static void init_drums(void) {
    synth_kick();
    synth_snare();
    synth_hihat();
    synth_clap();

    voices[DRUM_KICK]  = (DrumVoice){ kick_buf,  RATE/2,  0, false };
    voices[DRUM_SNARE] = (DrumVoice){ snare_buf, RATE/8,  0, false };
    voices[DRUM_HIHAT] = (DrumVoice){ hihat_buf, RATE/16, 0, false };
    voices[DRUM_CLAP]  = (DrumVoice){ clap_buf,  RATE/8,  0, false };
}

/* ═══════════════════════════════════════════════════════════════════════
   PWM AUDIO ISR
   Replaces your synth's pwm_audio_handler.
   Mixes all active drum voices, scales to PWM top exactly as your
   original handler does.
   ═══════════════════════════════════════════════════════════════════════ */

void pwm_audio_handler(void) {
    uint slice = AUDIO_SLICE;
    pwm_hw->intr = 1ul << slice;

    int32_t mix   = 0;
    int     count = 0;

    for (int d = 0; d < NUM_DRUMS; d++) {
        if (!voices[d].active) continue;
        mix += (int32_t)voices[d].buf[voices[d].pos] - 16384; /* centre at 0 */
        voices[d].pos++;
        if (voices[d].pos >= voices[d].len) {
            voices[d].active = false;
            voices[d].pos    = 0;
        }
        count++;
    }

    uint32_t samp;
    if (count == 0) {
        samp = 16384;                  /* silence = midpoint */
    } else {
        mix /= count;                  /* average active voices */
        mix += 16384;                  /* re-bias to 0..32767  */
        if (mix < 0)     mix = 0;
        if (mix > 32767) mix = 32767;
        samp = (uint32_t)mix;
    }

    /* scale to PWM top — identical to your original synth handler */
    samp = samp * pwm_hw->slice[slice].top / (1ul << 16);

    hw_write_masked(
        &pwm_hw->slice[slice].cc,
        samp << (AUDIO_CHAN ? PWM_CH0_CC_B_LSB : PWM_CH0_CC_A_LSB),
        AUDIO_CHAN ? PWM_CH0_CC_B_BITS : PWM_CH0_CC_A_BITS
    );
}

/* ═══════════════════════════════════════════════════════════════════════
   PWM INIT — identical pattern to your init_pwm_audio()
   ═══════════════════════════════════════════════════════════════════════ */

static void init_pwm_audio(void) {
    uint slice   = AUDIO_SLICE;
    uint channel = AUDIO_CHAN;

    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);
    pwm_set_clkdiv(slice, 150);
    pwm_hw->slice[slice].top = ((clock_get_hz(clk_sys) / 150) / RATE) - 1;

    hw_write_masked(
        &pwm_hw->slice[slice].cc,
        ((uint)0) << (channel ? PWM_CH0_CC_B_LSB : PWM_CH0_CC_A_LSB),
        channel ? PWM_CH0_CC_B_BITS : PWM_CH0_CC_A_BITS
    );
    hw_write_masked(
        &pwm_hw->slice[slice].csr,
        1ul << PWM_CH0_CSR_EN_LSB,
        PWM_CH0_CSR_EN_BITS
    );

    pwm_hw->intr = 1u << slice;
    pwm_irqn_set_slice_enabled(0, slice, true);
    pwm_set_irq_enabled(slice, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP_0, pwm_audio_handler);
    irq_set_enabled(PWM_IRQ_WRAP_0, true);
}

/* ═══════════════════════════════════════════════════════════════════════
   TRIGGER A DRUM
   Sets active last so the ISR never sees a stale pos.
   ═══════════════════════════════════════════════════════════════════════ */

static void play_drum(uint8_t d) {
    if (d >= NUM_DRUMS) return;
    voices[d].pos    = 0;       /* reset position first */
    voices[d].active = true;    /* ISR picks up on next wrap */
}

/* ═══════════════════════════════════════════════════════════════════════
   SEQUENCER TICK
   us_per_step = 60_000_000 / bpm / 4  (sixteenth notes)
   ═══════════════════════════════════════════════════════════════════════ */

static uint32_t last_step_us = 0;

static void seq_tick(void) {
    if (!seq_playing) return;

    uint32_t now     = timer_hw->timerawl;
    uint32_t step_us = 15000000u / (uint32_t)bpm;

    if ((now - last_step_us) < step_us) return;
    last_step_us = now;

    for (int d = 0; d < NUM_DRUMS; d++) {
        if (pat[current_pat][d][current_step])
            play_drum(d);
    }

    current_step = (current_step + 1) % NUM_STEPS;
}

/* ═══════════════════════════════════════════════════════════════════════
   KEY PROCESSING
   ═══════════════════════════════════════════════════════════════════════ */

static void process_keys(void) {
    uint16_t ev;
    while (key_pop_nb(&ev)) {
        if (!(ev & 0x100)) continue;    /* keydown events only */
        char key = (char)(ev & 0xFF);

        switch (key) {

            /* ── live trigger + select drum for step-edit ── */
            case '1': play_drum(DRUM_KICK);  edit_drum = DRUM_KICK;  break;
            case '2': play_drum(DRUM_SNARE); edit_drum = DRUM_SNARE; break;
            case '3': play_drum(DRUM_HIHAT); edit_drum = DRUM_HIHAT; break;
            case '4': play_drum(DRUM_CLAP);  edit_drum = DRUM_CLAP;  break;

            /* ── step toggle (8 keys × 2 banks = all 16 steps) ── */
            case '5': case '6': case '7': case '8':
            case '9': case '0': case '*': case '#': {
                const char keys[8] = {'5','6','7','8','9','0','*','#'};
                for (int k = 0; k < 8; k++) {
                    if (key == keys[k]) {
                        int step = k + step_bank * 8;
                        pat[current_pat][edit_drum][step] ^= true;
                        printf("drum=%d step=%2d %s\n",
                               edit_drum, step,
                               pat[current_pat][edit_drum][step] ? "ON" : "OFF");
                        break;
                    }
                }
                break;
            }

            /* ── A: swap step bank 0-7 ↔ 8-15 ── */
            case 'A':
                step_bank ^= 1;
                printf("Bank -> %d  (steps %d-%d)\n",
                       step_bank, step_bank*8, step_bank*8+7);
                break;

            /* ── B: BPM up ── */
            case 'B':
                bpm = (bpm + 10 > MAX_BPM) ? MAX_BPM : bpm + 10;
                printf("BPM -> %d\n", bpm);
                break;

            /* ── C: play / stop ── */
            case 'C':
                seq_playing = !seq_playing;
                if (seq_playing) {
                    current_step = 0;
                    last_step_us = timer_hw->timerawl;
                    printf("PLAY  bpm=%d  pat=%d\n", bpm, current_pat);
                } else {
                    printf("STOP\n");
                }
                break;

            /* ── D: BPM down ── */
            case 'D':
                bpm = (bpm - 10 < MIN_BPM) ? MIN_BPM : bpm - 10;
                printf("BPM -> %d\n", bpm);
                break;

            default: break;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════
   DEFAULT PATTERN  (classic 808 groove)
   ═══════════════════════════════════════════════════════════════════════ */

static void load_default_pattern(void) {
    /* 4-on-the-floor kick */
    pat[0][DRUM_KICK][0]  = true;
    pat[0][DRUM_KICK][4]  = true;
    pat[0][DRUM_KICK][8]  = true;
    pat[0][DRUM_KICK][12] = true;

    /* snare on beats 2 and 4 */
    pat[0][DRUM_SNARE][4]  = true;
    pat[0][DRUM_SNARE][12] = true;

    /* 8th-note hi-hats */
    for (int s = 0; s < NUM_STEPS; s += 2)
        pat[0][DRUM_HIHAT][s] = true;

    /* clap on beat 3 */
    pat[0][DRUM_CLAP][8] = true;
}

/* ═══════════════════════════════════════════════════════════════════════
   MAIN
   ═══════════════════════════════════════════════════════════════════════ */

int main(void) {
    stdio_init_all();
    printf("808 Drum Machine — RP2350\n");

    init_drums();
    printf("Drum buffers synthesized\n");

    init_pwm_audio();
    printf("PWM audio ready  GPIO=%d  RATE=%d\n", AUDIO_PIN, RATE);

    keypad_init_pins();
    keypad_init_timer();
    printf("Keypad ready\n");

    load_default_pattern();
    printf("Default pattern loaded\n\n");

    printf("1/2/3/4 = Kick/Snare/HiHat/Clap  (live + select)\n");
    printf("5-#     = toggle steps 0-7 for selected drum\n");
    printf("A       = swap step bank (0-7 / 8-15)\n");
    printf("B / D   = BPM +10 / -10\n");
    printf("C       = Play / Stop\n\n");

    for (;;) {
        process_keys();
        seq_tick();
    }

    return 0;
}
