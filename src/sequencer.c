#include "sequencer.h"

void sequencer_init() {
    memset((void *)steps, 0, sizeof(steps)); // always clear to not get fucky with previous samples
    length = 0;
    play_index = 0;
    tick_count = 0;
    mode = IDLE;
}

void sequencer_set_mode(sequencer_mode_t m) {
    if (m == PLAY && length == 0)
        return;
    if (m == RECORD) {
        length = 0;
        play_index = 0;
        tick_count = 0;
    }
    if (m == PLAY) {
        play_index = 0;
        tick_count = 0;
        set_note(steps[0].channel, steps[0].note, steps[0].octave);
    }
    if (m == IDLE) {
        stop_current_step_voice();
        tick_count = 0;
    }   
  mode = m;
}

void record(note_t n, uint8_t octave, uint8_t channel, bool tie) {
    if (mode != RECORD || length >= 128)
        return;
    steps[length].note = n;
    steps[length].octave = octave;
    steps[length].channel = channel;
    length++;

}

void sequencer_next() {
    if (mode != PLAY || length == 0)
        return;
    
    play_index = (play_index + 1) % length;
    set_note(steps[play_index].channel, steps[play_index].note,
           steps[play_index].octave);
}
void sequencer_process() {
    if (mode == PLAY) {
        tick_count++;
        if (tick_count >= 8000) {
            tick_count = 0;
            sequencer_next();
        }
    }
}