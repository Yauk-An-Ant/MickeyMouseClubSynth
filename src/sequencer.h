#include <stdio.h>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>

using namespace std;

#define N 1000
short int wavetable[N];

#define RATE 20000

struct currNote {
    note_t note;
    int octave;
    bool on;
};
struct Step {
    vector<currNote> all{12};
    int duration; //tempo/240
};

struct Track{
    vector<Step> steps;
};

typedef enum {
    SINE,
    TRIANGLE,
    SAWTOOTH,
    SQUARE
} wave_t;

typedef enum {
    C,
    CS,
    D,
    DS,
    E,
    F,
    FS,
    G,
    GS,
    A,
    AS,
    B
} note_t;

const float base_freqs[] = {
    16.35f, 17.32f, 18.35f, 19.45f,
    20.60f, 21.83f, 23.12f, 24.50f,
    25.96f, 27.50f, 29.14f, 30.87f
};