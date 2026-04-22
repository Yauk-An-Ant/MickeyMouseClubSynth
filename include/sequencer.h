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
#define SEQ_MAX_CHORD    4      // max notes per step

typedef struct {
    note_t  notes[SEQ_MAX_CHORD];
    uint8_t octaves[SEQ_MAX_CHORD];
    uint8_t num_notes;   // 0 = rest, 1 = single note, 2..SEQ_MAX_CHORD = chord
    bool    tie;         // true = don't retrigger; hold previous step's notes
    bool    is_rest;     // true when the step is silence (num_notes also 0)
} Step;

typedef enum {
    SEQ_IDLE,
    RECORD,
    PLAY
} sequencer_mode_t;

extern Step              steps[SEQ_MAX_STEPS];
extern uint8_t           length;
extern uint8_t           play_index;
extern uint32_t          tick_count;
extern sequencer_mode_t  mode;

void sequencer_init(void);
void sequencer_set_mode(sequencer_mode_t m);

// Call from main.c on every piano key press / release during RECORD.  The
// sequencer assembles chords (simultaneous presses within a ~50 ms window)
// and ties (keys held across step boundaries) automatically.  Outside of
// RECORD mode these calls are ignored.
void record_note_on(note_t n, uint8_t octave);
void record_note_off(note_t n, uint8_t octave);

void sequencer_next(void);

// Call from the audio IRQ (or any fixed-rate source).  Cheap: a few int ops.
void sequencer_process(void);

#endif // SEQUENCER_H