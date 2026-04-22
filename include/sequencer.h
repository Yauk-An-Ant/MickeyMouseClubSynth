#include <stdint.h>
#include "audio.h"
#include <string.h>
#include <stdbool.h>
extern uint8_t length;
extern uint8_t play_index;
extern uint8_t tick_count; 

typedef struct {
    note_t note;
    uint8_t octave;
    uint8_t channel;
    bool tie;
} Step;
extern Step steps[128];


typedef enum { 
  SEQ_IDLE, 
  RECORD, 
  PLAY 
} sequencer_mode_t;

extern sequencer_mode_t mode;


void sequencer_init();
void sequencer_set_mode(sequencer_mode_t);
void record(note_t, uint8_t, uint8_t, bool);
void sequencer_next();
void sequencer_process();