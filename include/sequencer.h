//============================================================================
// sequencer.h  --  Simple 128-step note sequencer.
//
// Usage model
// -----------
//   1. sequencer_init() once at startup.
//   2. sequencer_set_mode(RECORD) to start a new pattern (wipes previous).
//      Call record() for each note the user plays.  Call sequencer_set_mode(
//      SEQ_IDLE) to finish recording.
//   3. sequencer_set_mode(PLAY) to loop the recorded pattern.
//   4. sequencer_process() must be called repeatedly from a fixed-rate
//      source (the PWM audio IRQ in this project) for tempo to be stable.
//
// Tempo
//   Hardcoded at 8000 process() calls per step.  At 20 kHz (the audio IRQ
//   rate), that's 400 ms / step = 150 BPM.  Adjust STEP_TICKS below to
//   change.
//============================================================================
#ifndef SEQUENCER_H
#define SEQUENCER_H

#include <stdint.h>
#include <stdbool.h>
#include "support.h"        // note_t + MAX_VOICES (doesn't pull in audio.h)

#define SEQ_MAX_STEPS    128
#define SEQ_STEP_TICKS   8000   // at 20 kHz IRQ: 8000 ticks = 400 ms = 150 BPM

typedef struct {
    note_t  note;
    uint8_t octave;
    uint8_t channel;    // voice slot (0..MAX_VOICES-1)
    bool    tie;        // if true, inherits the previous step's note (no retrigger)
    bool    is_rest;    // if true, this step is silence — `note` and `octave` ignored
} Step;

typedef enum {
    SEQ_IDLE,
    RECORD,
    PLAY
} sequencer_mode_t;

extern Step              steps[SEQ_MAX_STEPS];
extern uint8_t           length;
extern uint8_t           play_index;
extern uint32_t          tick_count;      // was uint8_t — overflowed every 256 ticks!
extern sequencer_mode_t  mode;

void sequencer_init(void);
void sequencer_set_mode(sequencer_mode_t m);
void record(note_t n, uint8_t octave, uint8_t channel, bool tie);
void sequencer_next(void);

// Call from the audio IRQ (or any fixed-rate source).  Cheap: two int ops.
void sequencer_process(void);

#endif // SEQUENCER_H