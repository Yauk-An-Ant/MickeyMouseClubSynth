#ifndef SUPPORT_H
#define SUPPORT_H

#include <stdbool.h>

#define N 2048
#define MAX_VOICES 12
#define RATE 20000
#define FLANGER_BUF 1024
#define DELAY_BUF_SIZE 12000

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

typedef enum {
    IDLE,
    ATTACK,
    DECAY,
    SUSTAIN,
    RELEASE
} envelope_state_t;

typedef struct {
    int step;
    int offset;
    int active;
    char key;

    // asdr things
    envelope_state_t envelope_state;
    float envelope_level;
} voice_t;

// ---------------------------------------------------------------------------
// Shared synth state — DECLARATIONS only.  The actual storage lives in
// globals.c (exactly one definition per symbol).  Including this header
// from multiple .c files is safe because `extern` does not allocate.
// ---------------------------------------------------------------------------

extern voice_t voices[MAX_VOICES];

// ASDR settings (all 0..1)
extern float attack_time;
extern float decay_time;
extern float sustain_level;
extern float release_time;

extern float attack_inc;
extern float decay_dec;
extern float release_dec;

// Distortion
extern bool  distortion_enabled;
extern float distortion_volume;
extern float distortion;

// EQ
extern float lows;
extern float mids;
extern float highs;

// Flanger
extern bool  flanger_enabled;
extern float flanger_depth;
extern float flanger_rate;
extern float flanger_feedback;
extern float flanger_mix;

// Delay
extern bool  delay_enabled;
extern float delay_time;
extern float delay_mix;
extern float delay_feedback;

// Wavegen tables
extern short int   wavetable[N];
extern const float base_freqs[];
extern int         volume;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
void  init_asdr(float attack, float decay, float sustain, float release);

void  init_wavetable(wave_t wave);
void  set_note(int chan, note_t n, int octave);
float note_to_freq(note_t n, int octave);

void  init_distortion(bool enable, float dist, float dist_volume);
float apply_distortion(float x);

void  init_eq(float l, float m, float h);
float apply_eq(float x);

void  init_flanger(bool enabled, float depth, float rate, float feedback, float mix);
float apply_flanger(float x);

void  init_delay(bool enabled, float time, float mix, float feedback);
float apply_delay(float x);

#endif // SUPPORT_H