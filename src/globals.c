//============================================================================
// globals.c  --  The single definition of every shared synth variable.
//
// support.h only declares these as `extern`; this file is where the storage
// actually lives.  Every other .c file picks them up by including support.h.
//
// If you get a "multiple definition" linker error for any of these symbols,
// it means another file defined the same variable — search for a line like
// `float delay_feedback;` or `int volume = 1200;` elsewhere and remove it.
// The definition should exist in this file and nowhere else.
//============================================================================
#include "support.h"

voice_t voices[MAX_VOICES];

// ASDR
float attack_time    = 0.20f;
float decay_time     = 0.30f;
float sustain_level  = 0.70f;
float release_time   = 0.40f;

float attack_inc     = 0.0f;   // recomputed by init_asdr()
float decay_dec      = 0.0f;
float release_dec    = 0.0f;

// Distortion
bool  distortion_enabled = false;
float distortion         = 0.00f;
float distortion_volume  = 0.80f;

// EQ
float lows  = 0.50f;
float mids  = 0.50f;
float highs = 0.50f;

// Flanger
bool  flanger_enabled  = false;
float flanger_depth    = 0.50f;
float flanger_rate     = 0.25f;
float flanger_feedback = 0.30f;
float flanger_mix      = 0.50f;

// Delay
bool  delay_enabled  = false;
float delay_time     = 0.30f;
float delay_mix      = 0.50f;
float delay_feedback = 0.40f;

// Master volume (kept at wavegen.c's original default)
int volume = 1200;