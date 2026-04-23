//============================================================================
// drum.c  --  Drum sample playback implementation.
//
// Adapted from drum_machine2.c.  Differences vs. that file:
//   * Doesn't own the PWM slice / IRQ — audio.c does, and just calls us.
//   * drum_mix_sample() returns a SIGNED contribution centred on zero,
//     suitable for adding to the synth's int32_t `mix` accumulator before
//     the float conversion in audio.c.
//   * Accumulator scaling: drum samples are 16-bit unsigned centred at
//     32768; we subtract the bias and then halve (right-shift 1) so a
//     single drum contributes ±16384, which matches the voice-mix scale
//     in audio.c where each voice contributes roughly ±2048 (= 16384/8).
//     That makes one drum about as loud as 8 voices — roughly right for
//     punchy drums sitting on top of a chord.  Tunable below.
//============================================================================
#include "drum.h"
#include "samples.h"

typedef struct {
    const uint16_t *buf;    // sample data (16-bit unsigned PCM)
    uint32_t        len;    // number of samples in buf
    volatile uint32_t pos;  // current playback position
    volatile bool   active; // true while the sample is sounding
} DrumData;

static DrumData drums[NUM_DRUMS];

// Per-drum gain shift.  Increase to make drums quieter, decrease for louder.
// Shift of 1 = halve each drum's contribution.  If drums sound too loud
// compared to the synth, bump this to 2.  Too quiet, drop it to 0.
#define DRUM_OUTPUT_SHIFT  1

void drum_init(void) {
    drums[DRUM_KICK]    = (DrumData){ kicks01,  kicks01_len,  0, false };
    drums[DRUM_SNARE]   = (DrumData){ snare01,  snare01_len,  0, false };
    drums[DRUM_HIHAT]   = (DrumData){ hihats05, hihats05_len, 0, false };
    drums[DRUM_CLAP]    = (DrumData){ clap02,   clap02_len,   0, false };
    drums[DRUM_COWBELL] = (DrumData){ cowbell5, cowbell5_len, 0, false };
    drums[DRUM_RIDE]    = (DrumData){ ride1,    ride1_len,    0, false };
    drums[DRUM_TOM]     = (DrumData){ tom1,     tom1_len,     0, false };
}

void drum_trigger(uint8_t which) {
    if (which >= NUM_DRUMS) return;
    drums[which].pos    = 0;
    drums[which].active = true;
}

int32_t drum_mix_sample(void) {
    int32_t sum = 0;
    for (int d = 0; d < NUM_DRUMS; d++) {
        if (!drums[d].active) continue;

        // Convert unsigned 16-bit PCM (centred at 32768) to signed centred
        // at zero, then attenuate.
        int32_t s = (int32_t)drums[d].buf[drums[d].pos] - 32768;
        sum += s >> DRUM_OUTPUT_SHIFT;

        drums[d].pos++;
        if (drums[d].pos >= drums[d].len) {
            drums[d].active = false;
            drums[d].pos    = 0;
        }
    }
    return sum;
}