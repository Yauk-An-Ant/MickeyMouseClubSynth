#include "sequencer.h"

uint8_t length = 0;
uint8_t play_index = 0;
uint8_t tick_count = 0;
Step steps[128];
sequencer_mode_t mode = SEQ_IDLE;

void sequencer_init() {
    memset((void *)steps, 0, sizeof(steps)); // always clear to not get fucky with previous samples
    length = 0;
    play_index = 0;
    tick_count = 0;
    mode = SEQ_IDLE;
}

void sequencer_set_mode(sequencer_mode_t m) {
    if (m == PLAY && length == 0)
        return;
    if (m == RECORD) {
        //stop_current_step_voice();
        length = 0;
        play_index = 0;
        tick_count = 0;
    }
    if (m == PLAY) {
        play_index = 0;
        tick_count = 0;
        set_note(steps[0].channel, steps[0].note, steps[0].octave);
    }
    if (m == SEQ_IDLE) {
       // stop_current_step_voice();
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
    steps[length].tie = tie;
    length++;
}

void sequencer_next() {
    if (mode != PLAY || length == 0)
        return;

    uint8_t next_index = (play_index + 1) % length;
    if (!steps[next_index].tie) {
        uint8_t prev_ch = steps[play_index].channel;
        if (prev_ch < MAX_VOICES)
            voices[prev_ch].active = 0;
        play_index = next_index;
        set_note(steps[play_index].channel, steps[play_index].note, steps[play_index].octave);
    } else {
        play_index = next_index;
    }
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