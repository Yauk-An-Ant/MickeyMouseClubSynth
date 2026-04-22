//============================================================================
// sequencer.c  --  Implementation for the 128-step sequencer.
//
// Fixes vs. the original
// ----------------------
//  * tick_count widened to uint32_t (was uint8_t — overflowed at 256, which
//    meant sequencer_process() never actually reached the 8000-threshold).
//    This was the "why isn't my sequencer playing" bug waiting to happen.
//  * set_note() call in sequencer_set_mode(PLAY) now checks the recorded
//    channel against MAX_VOICES before indexing voices[].
//  * sequencer_next() checks the new step's channel too.
//  * Uses SEQ_MAX_STEPS / SEQ_STEP_TICKS macros from the header instead of
//    hardcoded 128 / 8000.
//============================================================================
#include "sequencer.h"
#include <string.h>
#include "audio.h"          // for set_note() and the voices[] table

uint8_t           length      = 0;
uint8_t           play_index  = 0;
uint32_t          tick_count  = 0;
Step              steps[SEQ_MAX_STEPS];
sequencer_mode_t  mode        = SEQ_IDLE;

// Track which voice each playing step is using — separate from the recorded
// "channel" field, which is a historical relic from when the note was
// captured.  Resolved fresh on each step via allocate_voice().
static int seq_active_voice = -1;

// Record-mode silence tracker.  Counts ticks since the last recorded event
// (note or rest).  Every SEQ_STEP_TICKS of silence while no note was pressed
// emits a single rest step.  Reset to 0 on every note capture.
static uint32_t record_silence_ticks = 0;

// Forward decls so functions can call each other regardless of file order.
static void append_rest_step(void);

// Silence the voice that the sequencer is currently driving (if any).
static void stop_current_step_voice(void) {
    if (seq_active_voice >= 0 && seq_active_voice < MAX_VOICES) {
        voices[seq_active_voice].envelope_state = RELEASE;
    }
    seq_active_voice = -1;
}

// Pick a free voice, program the note, kick the envelope.  Returns -1 on
// voice exhaustion or if the step is a rest (in which case the caller
// should treat it as "no voice is currently driving a step").
static int seq_fire_step(uint8_t step_idx) {
    if (steps[step_idx].is_rest) return -1;
    int voice = allocate_voice();
    if (voice < 0) return -1;
    set_note(voice, steps[step_idx].note, steps[step_idx].octave);
    voices[voice].envelope_state = ATTACK;
    voices[voice].envelope_level = 0.0f;
    return voice;
}

void sequencer_init(void) {
    memset(steps, 0, sizeof(steps));
    length               = 0;
    play_index           = 0;
    tick_count           = 0;
    seq_active_voice     = -1;
    record_silence_ticks = 0;
    mode                 = SEQ_IDLE;
}

void sequencer_set_mode(sequencer_mode_t m) {
    // Can't play an empty pattern.
    if (m == PLAY && length == 0) return;

    if (m == RECORD) {
        // Wipe the previous pattern.  If you'd rather keep it and append,
        // delete the three resets below.
        stop_current_step_voice();
        length               = 0;
        play_index           = 0;
        tick_count           = 0;
        record_silence_ticks = 0;
    } else if (m == PLAY) {
        play_index = 0;
        tick_count = 0;
        seq_active_voice = seq_fire_step(0);
    } else if (m == SEQ_IDLE) {
        // If we were recording, flush any trailing silence into rest steps
        // so the loop's end-to-start gap preserves whatever pause the user
        // left at the end of the pattern.
        if (mode == RECORD) {
            while (record_silence_ticks >= SEQ_STEP_TICKS
                   && length < SEQ_MAX_STEPS) {
                append_rest_step();
                record_silence_ticks -= SEQ_STEP_TICKS;
            }
            record_silence_ticks = 0;
        }
        stop_current_step_voice();
        tick_count = 0;
    }

    mode = m;
}

// Append a single rest step.  Used internally by record() and by
// sequencer_process() when record-mode silence crosses a step boundary.
static void append_rest_step(void) {
    if (length >= SEQ_MAX_STEPS) return;
    steps[length].is_rest = true;
    steps[length].tie     = false;
    // note/octave/channel ignored for rests; zero them for hygiene.
    steps[length].note    = 0;
    steps[length].octave  = 0;
    steps[length].channel = 0;
    length++;
}

void record(note_t n, uint8_t octave, uint8_t channel, bool tie) {
    if (mode != RECORD || length >= SEQ_MAX_STEPS) return;

    // Any full step-duration of silence that piled up since the last
    // captured event needs to be preserved as rest steps first, so the
    // played-back rhythm matches what the user actually played.
    while (record_silence_ticks >= SEQ_STEP_TICKS && length < SEQ_MAX_STEPS) {
        append_rest_step();
        record_silence_ticks -= SEQ_STEP_TICKS;
    }
    // Partial-step residue is dropped — playback granularity is one step.
    record_silence_ticks = 0;

    if (length >= SEQ_MAX_STEPS) return;

    steps[length].note    = n;
    steps[length].octave  = octave;
    steps[length].channel = channel;
    steps[length].tie     = tie;
    steps[length].is_rest = false;
    length++;
}

void sequencer_next(void) {
    if (mode != PLAY || length == 0) return;

    uint8_t next_index = (play_index + 1) % length;
    const Step *next   = &steps[next_index];

    if (next->is_rest) {
        // Rest: release the outgoing voice and sit silent for the step.
        if (seq_active_voice >= 0 && seq_active_voice < MAX_VOICES) {
            voices[seq_active_voice].envelope_state = RELEASE;
        }
        seq_active_voice = -1;
        play_index = next_index;
        return;
    }

    if (!next->tie) {
        // Release the voice driving the outgoing step (if any), then fire
        // the new step on a freshly-allocated voice.
        if (seq_active_voice >= 0 && seq_active_voice < MAX_VOICES) {
            voices[seq_active_voice].envelope_state = RELEASE;
        }
        play_index = next_index;
        seq_active_voice = seq_fire_step(play_index);
    } else {
        // Tie: just advance the index; the previous voice keeps ringing.
        play_index = next_index;
    }
}

void sequencer_process(void) {
    if (mode == PLAY) {
        tick_count++;
        if (tick_count >= SEQ_STEP_TICKS) {
            tick_count = 0;
            sequencer_next();
        }
    } else if (mode == RECORD) {
        // Count time between note captures so record() can convert gaps
        // into rest steps.  We don't emit rests here (record() does that
        // atomically when the next note arrives, or on mode exit) — we
        // just let the counter grow.  The counter is uint32_t; at 20 kHz
        // it wraps after ~60 hours, well beyond any realistic recording.
        record_silence_ticks++;
    }
}