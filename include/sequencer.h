#include <stdint.h>
#include "support.h"
Step steps[128];
int length = 0;
int play_index = 0;
sequencer_mode_t mode = IDLE;
int tick_count = 0; 

typedef struct {
    note_t note;
    uint8_t octave;
    uint8_t channel;
} Step;

typedef enum { 
  IDLE, 
  RECORD, 
  PLAY 
} sequencer_mode_t;

void sequencer_init();
void sequencer_set_mode(sequencer_mode_t m);
void record(note_t n, uint8_t octave, uint8_t channel);
void sequencer_next();
void sequencer_process();