//============================================================================
// sequencer.c  --  128-step sequencer with chords, ties, and rests.
//
// Data model
// ----------
//   A Step holds 0..SEQ_MAX_CHORD notes plus two flags:
//     num_notes = 0, is_rest = true       → rest (silence for this step)
//     num_notes ≥ 1, tie    = false       → fresh attack on all notes
//     num_notes ≥ 1, tie    = true        → continue prior step (no retrigger)
//
// Recording
// ---------
//   Main.c calls record_note_on() on key-down and record_note_off() on
//   key-up.  This module handles:
//
//   * Chord grouping.  Presses within CHORD_WINDOW_TICKS of each other are
//     merged into one step.  The first press opens the window; every
//     additional press inside the window gets appended.  The step is only
//     committed to the steps[] array when the window expires (so we can
//     wait for chord-mates to arrive).
//
//   * Ties.  While any key is held across a step boundary (i.e. every
//     SEQ_STEP_TICKS in RECORD mode), we emit a tie step containing the
//     set of currently-held notes.
//
//   * Rests.  If the window expires, no keys are held, and a full step of
//     silence passes, emit a rest step.
//
// Playback
// --------
//   sequencer_next() runs every SEQ_STEP_TICKS.  Non-tie note steps release
//   whatever voices the previous step allocated, then fire the current
//   chord on freshly-allocated voices.  Tie steps skip the
//   release/reallocate and just keep the previous voices ringing.  Rest
//   steps release previous voices and sit silent.
//============================================================================
#include "sequencer.h"
#include <string.h>
#include "audio.h"          // allocate_voice(), set_note(), voices[]

// ---- Timing constants -------------------------------------------------------
// CHORD_WINDOW_TICKS: how long we wait after the first keydown of a step
// for additional chord-mate keydowns.  At 20 kHz audio IRQ, 1000 ticks =
// 50 ms — tight enough that "simultaneous" presses land together, loose
// enough that fumbling fingers still capture as a chord.
#define CHORD_WINDOW_TICKS  1000u

// ---- Public state -----------------------------------------------------------
uint8_t           length      = 0;
uint8_t           play_index  = 0;
uint32_t          tick_count  = 0;
Step              steps[SEQ_MAX_STEPS];
sequencer_mode_t  mode        = SEQ_IDLE;

// ---- Playback-side private state --------------------------------------------
// Voices currently driven by the sequencer (one per chord-note).  Indexed
// 0..SEQ_MAX_CHORD-1; unused slots = -1.  Tracked separately from the
// voices[] array so we can release them cleanly on step boundaries.
static int       seq_active_voices[SEQ_MAX_CHORD];
static uint8_t   seq_active_count = 0;

// ---- Record-side private state ----------------------------------------------
// "Pending" step — being assembled during the chord window.
static Step      pending_step;
static bool      pending_step_valid     = false;
static uint32_t  pending_step_age_ticks = 0;

// Notes currently held down (parallel to pending_step's chord once armed,
// but also tracks keys held across the window closing — those are what
// produce ties).  Note+octave pair uniquely identifies a held key.
static note_t    held_notes[SEQ_MAX_CHORD];
static uint8_t   held_octaves[SEQ_MAX_CHORD];
static uint8_t   held_count = 0;

// Silence tracking (for rest emission while no keys held, no pending step).
static uint32_t  record_silence_ticks = 0;

// Forward decls for internal helpers.
static void append_rest_step(void);
static void append_pending_step(bool tie);
static void append_tie_step(void);
static void seq_release_active_voices(void);
static int  seq_fire_note(note_t n, uint8_t octave);

// ============================================================================
// Init / mode
// ============================================================================
static void reset_record_state(void) {
    pending_step_valid     = false;
    pending_step_age_ticks = 0;
    held_count             = 0;
    record_silence_ticks   = 0;
    memset(&pending_step, 0, sizeof(pending_step));
}

void sequencer_init(void) {
    memset(steps, 0, sizeof(steps));
    length     = 0;
    play_index = 0;
    tick_count = 0;
    seq_active_count = 0;
    for (int i = 0; i < SEQ_MAX_CHORD; i++) seq_active_voices[i] = -1;
    reset_record_state();
    mode = SEQ_IDLE;
}

void sequencer_set_mode(sequencer_mode_t m) {
    if (m == PLAY && length == 0) return;

    if (m == RECORD) {
        seq_release_active_voices();
        length     = 0;
        play_index = 0;
        tick_count = 0;
        reset_record_state();
    } else if (m == PLAY) {
        play_index = 0;
        tick_count = 0;
        // Fire step 0 immediately so the user hears something on press.
        seq_release_active_voices();  // safety: clear any stale state
        if (!steps[0].is_rest) {
            for (uint8_t i = 0; i < steps[0].num_notes && i < SEQ_MAX_CHORD; i++) {
                int v = seq_fire_note(steps[0].notes[i], steps[0].octaves[i]);
                if (v >= 0 && seq_active_count < SEQ_MAX_CHORD) {
                    seq_active_voices[seq_active_count++] = v;
                }
            }
        }
    } else if (m == SEQ_IDLE) {
        // Leaving RECORD: commit any pending step, flush trailing silence.
        if (mode == RECORD) {
            if (pending_step_valid) {
                append_pending_step(/*tie=*/false);
            }
            while (record_silence_ticks >= SEQ_STEP_TICKS
                   && length < SEQ_MAX_STEPS) {
                append_rest_step();
                record_silence_ticks -= SEQ_STEP_TICKS;
            }
            record_silence_ticks = 0;
        }
        seq_release_active_voices();
        tick_count = 0;
    }

    mode = m;
}

// ============================================================================
// Helpers — step table appends
// ============================================================================
static void append_rest_step(void) {
    if (length >= SEQ_MAX_STEPS) return;
    memset(&steps[length], 0, sizeof(Step));
    steps[length].is_rest = true;
    length++;
}

static void append_pending_step(bool tie) {
    if (length >= SEQ_MAX_STEPS || !pending_step_valid) return;
    pending_step.tie = tie;
    pending_step.is_rest = (pending_step.num_notes == 0);
    steps[length++] = pending_step;
    pending_step_valid = false;
    memset(&pending_step, 0, sizeof(pending_step));
    pending_step_age_ticks = 0;
}

// Emit a tie step containing whatever notes are currently held.  Called at
// step boundaries during RECORD when keys are still down.
static void append_tie_step(void) {
    if (length >= SEQ_MAX_STEPS || held_count == 0) return;
    Step s = {0};
    s.num_notes = held_count;
    for (uint8_t i = 0; i < held_count; i++) {
        s.notes[i]   = held_notes[i];
        s.octaves[i] = held_octaves[i];
    }
    s.tie     = true;
    s.is_rest = false;
    steps[length++] = s;
}

// ============================================================================
// Helpers — held-notes bookkeeping
// ============================================================================
static int find_held(note_t n, uint8_t octave) {
    for (uint8_t i = 0; i < held_count; i++) {
        if (held_notes[i] == n && held_octaves[i] == octave) return (int)i;
    }
    return -1;
}

static void add_held(note_t n, uint8_t octave) {
    if (find_held(n, octave) >= 0) return;          // already tracked
    if (held_count >= SEQ_MAX_CHORD) return;        // over polyphony cap
    held_notes  [held_count] = n;
    held_octaves[held_count] = octave;
    held_count++;
}

static void remove_held(note_t n, uint8_t octave) {
    int idx = find_held(n, octave);
    if (idx < 0) return;
    // Compact.
    for (uint8_t i = idx; i + 1 < held_count; i++) {
        held_notes  [i] = held_notes  [i + 1];
        held_octaves[i] = held_octaves[i + 1];
    }
    held_count--;
}

// Add a note to the pending step (creating it if needed).
static void add_to_pending_step(note_t n, uint8_t octave) {
    if (!pending_step_valid) {
        memset(&pending_step, 0, sizeof(pending_step));
        pending_step_valid     = true;
        pending_step_age_ticks = 0;
    }
    if (pending_step.num_notes >= SEQ_MAX_CHORD) return;
    // Dedupe if already in pending.
    for (uint8_t i = 0; i < pending_step.num_notes; i++) {
        if (pending_step.notes[i]   == n
         && pending_step.octaves[i] == octave) return;
    }
    pending_step.notes  [pending_step.num_notes] = n;
    pending_step.octaves[pending_step.num_notes] = octave;
    pending_step.num_notes++;
}

// ============================================================================
// Record API (called from main.c on every piano keydown/keyup)
// ============================================================================
void record_note_on(note_t n, uint8_t octave) {
    if (mode != RECORD) return;

    // Any pile-up of silence since the last event becomes rest steps — has
    // to happen before we start a new chord window, so the rhythm is
    // preserved.
    while (record_silence_ticks >= SEQ_STEP_TICKS && length < SEQ_MAX_STEPS) {
        append_rest_step();
        record_silence_ticks -= SEQ_STEP_TICKS;
    }
    record_silence_ticks = 0;

    add_held(n, octave);
    add_to_pending_step(n, octave);
}

void record_note_off(note_t n, uint8_t octave) {
    if (mode != RECORD) return;
    remove_held(n, octave);
}

// ============================================================================
// Playback
// ============================================================================
static int seq_fire_note(note_t n, uint8_t octave) {
    int voice = allocate_voice();
    if (voice < 0) return -1;
    set_note(voice, n, octave);
    voices[voice].envelope_state = ATTACK;
    voices[voice].envelope_level = 0.0f;
    return voice;
}

static void seq_release_active_voices(void) {
    for (uint8_t i = 0; i < seq_active_count; i++) {
        int v = seq_active_voices[i];
        if (v >= 0 && v < MAX_VOICES) {
            voices[v].envelope_state = RELEASE;
        }
        seq_active_voices[i] = -1;
    }
    seq_active_count = 0;
}

void sequencer_next(void) {
    if (mode != PLAY || length == 0) return;

    uint8_t next_index = (play_index + 1) % length;
    const Step *next   = &steps[next_index];

    if (next->is_rest) {
        seq_release_active_voices();
        play_index = next_index;
        return;
    }

    if (!next->tie) {
        // Non-tie note step: release outgoing voices, fire a fresh chord.
        seq_release_active_voices();
        play_index = next_index;
        for (uint8_t i = 0; i < next->num_notes && i < SEQ_MAX_CHORD; i++) {
            int v = seq_fire_note(next->notes[i], next->octaves[i]);
            if (v >= 0 && seq_active_count < SEQ_MAX_CHORD) {
                seq_active_voices[seq_active_count++] = v;
            }
        }
    } else {
        // Tie: advance index, keep the current voices sounding.
        play_index = next_index;
    }
}

// ============================================================================
// Per-sample tick (called from the audio IRQ)
// ============================================================================
void sequencer_process(void) {
    if (mode == PLAY) {
        tick_count++;
        if (tick_count >= SEQ_STEP_TICKS) {
            tick_count = 0;
            sequencer_next();
        }
        return;
    }

    if (mode != RECORD) return;

    // --- RECORD mode tick bookkeeping ---------------------------------------
    // Close the chord window if it's been open long enough.  "Long enough"
    // means a little more than CHORD_WINDOW_TICKS, but crucially also any
    // time a step boundary arrives — we can't let a pending step stay
    // uncommitted across a boundary, because boundary behavior depends on
    // whether keys are still held.
    if (pending_step_valid) {
        pending_step_age_ticks++;
        if (pending_step_age_ticks >= CHORD_WINDOW_TICKS) {
            append_pending_step(/*tie=*/false);
        }
    }

    // --- Step boundary in RECORD: emit tie step if any notes still held. ----
    tick_count++;
    if (tick_count >= SEQ_STEP_TICKS) {
        tick_count = 0;

        // If a pending step slipped past the boundary (e.g. chord window
        // still open), commit it now before deciding about ties / rests.
        if (pending_step_valid) {
            append_pending_step(/*tie=*/false);
        }

        if (held_count > 0) {
            // Notes held across the boundary → tie step.
            append_tie_step();
            // Not silent, so don't bank silence.
            record_silence_ticks = 0;
        } else {
            // No keys held, no pending step → count this boundary as
            // silence.  record_note_on() will convert banked silence into
            // rest steps the next time a note arrives; leaving RECORD
            // mode flushes trailing silence.
            record_silence_ticks += SEQ_STEP_TICKS;
        }
    } else if (held_count == 0 && !pending_step_valid) {
        // Between boundaries with no activity — bank silence by the sample.
        record_silence_ticks++;
    }
}