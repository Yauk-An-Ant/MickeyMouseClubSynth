#include <stdint.h>
#include "support.h"
static int tick_count = 0;

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
sequencer_mode_t sequencer_get_mode();
void record(note_t n, uint8_t octave, uint8_t channel);
void sequencer_next();
uint32_t sequencer_process(uint32_t samp);