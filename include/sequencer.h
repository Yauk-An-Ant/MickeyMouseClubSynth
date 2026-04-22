#include <stdint.h>
#include "audio.h"
#include <string.h>
#include <stdbool.h>
uint8_t length = 0;
uint8_t play_index = 0;
uint8_t tick_count = 0; 

typedef struct {
    note_t note;
    uint8_t octave;
    uint8_t channel;
    bool tie;
} Step;
Step steps[128];


typedef enum { 
  SEQ_IDLE, 
  RECORD, 
  PLAY 
} sequencer_mode_t;

sequencer_mode_t mode = IDLE;


void sequencer_init();
void sequencer_set_mode(sequencer_mode_t);
void record(note_t, uint8_t, uint8_t, bool);
void sequencer_next();
void sequencer_process();