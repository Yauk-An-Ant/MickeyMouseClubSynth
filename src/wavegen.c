#include <stdio.h>
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif
#include <math.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "support.h"

short int wavetable[N];
//here its from 0 to 1800
int volume = 1200;
int distortion_on = 0;

const float base_freqs[] = {
    16.35f, 17.32f, 18.35f, 19.45f,
    20.60f, 21.83f, 23.12f, 24.50f,
    25.96f, 27.50f, 29.14f, 30.87f
};


void init_wavetable(wave_t wave) {
    if(wave == SINE) {
        for(int i=0; i < N; i++)
            wavetable[i] = (16383 * sin(2 * M_PI * i / N)) + 16384;

    } else if(wave == TRIANGLE) {
        for(int i = 0; i < N; i++) {
            if(i < N/2)
                wavetable[i] = (short int)(1 + (4 * 16383 * i) / N);
            else
                wavetable[i] = (short int)(32767 - (4 * 16383 * (i - N/2)) / N);
        }
    } else if(wave == SAWTOOTH) {
        for(int i = 0; i < N; i++) {
            wavetable[i] = (short int)((2 * 16383 * i) / N) + 1;
        }
    } else if(wave == SQUARE) {
        for(int i = 0; i < N; i++) {
            if(i < N/2)
                wavetable[i] = 32767;
            else
                wavetable[i] = 1;
        }
    }
}

float note_to_freq(note_t n, int octave) {
    return (base_freqs[n] * (1 << octave));
}

void set_note(int voice, note_t n, int octave) {

    float freq = note_to_freq(n, octave);

    if(freq == 0.0f) {
        voices[voice].step = 0;
        //voices[voice].offset = 0;
        voices[voice].active = 0;
    } else {
        voices[voice].step = (int)((freq * N / RATE) * (1u << 16));
        voices[voice].active = 1;
    }
}

void init_asdr(float attack, float decay, float sustain, float release) {
    attack_time   = attack;
    decay_time    = decay;
    sustain_level = sustain;
    release_time  = release;

    attack_inc  = 1.0f / (attack_time * RATE);
    decay_dec   = (1.0f - sustain_level) / (decay_time * RATE);
    release_dec = 1.0f / (release_time * RATE);
}