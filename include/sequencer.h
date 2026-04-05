#include <stdio.h>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>
#include "support.h"

using namespace std;

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
