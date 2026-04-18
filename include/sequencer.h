#include <stdio.h>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>
#include "support.h"

#include <stdint.h>
#include <unordered_map>

int offset = 0;
int step = 493.883*N / RATE * (1<<16);
uint8_t buffer[1024]; 
vector<Track> tracks;
int *mpindex = 0;
static Step steps[128]; //?
static int length  = 0;
static int play_index = 0;
static sequencer_mode_t mode = IDLE;


using namespace std;

struct currNote {
    note_t *note;
    int *octave;
    bool *on;
};
struct Step {
    vector<currNote> all{12};
    int *duration; //tempo/240
};
// struct Patch{
//     //4 things
//     //envelope waveform effects
// }

struct Track{
    vector<Step> steps;
};

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
void record(note_t n, uint8_t octave, uint8_t channel);
void sequencer_next();         
void set_mode(sequencer_mode_t m);