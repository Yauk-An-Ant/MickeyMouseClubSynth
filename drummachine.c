#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "support.h"   /* wavetable[], N, RATE, wave_t, note_t … */
#include "queue.h"     /* KeyEvents kev, key_push(), key_pop()    */


void keypad_init_pins();
void keypad_init_timer();

#define AUDIO_PIN   36
#define AUDIO_SLICE (8u + (((AUDIO_PIN) >> 1u) & 3u))
#define AUDIO_CHAN  ((AUDIO_PIN) & 1u)
 
/* ── sequencer ───────────────────────────────────────────────────────── */
#define NUM_DRUMS    4
#define NUM_STEPS   16
#define NUM_PATTERNS 4
#define DEFAULT_BPM 120
#define MIN_BPM      60
#define MAX_BPM     200
 
#define DRUM_KICK   0
#define DRUM_SNARE  1
#define DRUM_HIHAT  2
#define DRUM_CLAP   3



typedef struct
{
    short int *buf;
    uint32_t len;
    volatile uint32_t pos;
    volatile bool active;
} DrumVoice;

static short int kick_buf [RATE / 2];    /* 0.5 s  */
static short int snare_buf[RATE / 8];    /* 125 ms */
static short int hihat_buf[RATE / 16];   /*  62 ms */
static short int clap_buf [RATE / 8];    /* 125 ms */
 
static DrumVoice voices[NUM_DRUMS];

static bool pat[NUM_PATTERNS][NUM_DRUMS][NUM_STEPS];
static uint8_t current_pat= 0;
static uint8_t current_step = 0;
static bool seq_playing = false;
static int bpm = DEFAULT_BPM;
static uint8_t edit_drum = DRUM_KICK;
static uint8_t step_bank = 0;