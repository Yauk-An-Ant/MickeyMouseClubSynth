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
float eq_low_state = 0.0f;
float eq_high_state = 0.0f;

float flanger_buf[FLANGER_BUF];
int flanger_idx = 0;
float flanger_lfo = 0.0f;

float delay_buf[DELAY_BUF_SIZE];
int delay_write = 0;

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

void set_note(int voice, note_t n, int octave, bool tie) {

    float freq = note_to_freq(n, octave);

    if(freq == 0.0f) {
        voices[voice].step = 0;
        //voices[voice].offset = 0;
        voices[voice].active = 0;
    } else {
        voices[voice].step = (int)((freq * N / RATE) * (1u << 16));
        voices[voice].active = 1;
    }
   // if(voices[i].envelope_state == ATTACK)



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

void init_distortion(bool enable, float dist, float dist_volume) {
    distortion_enabled = enable;
    distortion = dist;
    distortion_volume = dist_volume;
}

float apply_distortion(float x) {
    //bypass
    if(distortion <= 0.001f) {
        return x;
    }

    float drive = 1.0f + (distortion * distortion) * 19.0f;

    float y = x * drive;

    // soft clip
    if (y > 1.0f) y = 1.0f;
    else if (y < -1.0f) y = -1.0f;

    // prevent signal collapse
    return y * (0.5f + 0.5f * distortion_volume);
}

void init_eq(float l, float m, float h) {
    lows = l;
    mids = m;
    highs = h;
}

float apply_eq(float x) {
    float low_gain  = 2.0f * lows;
    float mid_gain  = 2.0f * mids;
    float high_gain = 2.0f * highs;

    float low_alpha = 0.02f;
    float high_alpha = 0.02f;

    //lowpass
    eq_low_state += low_alpha * (x - eq_low_state);
    float low = eq_low_state;

    //highpass
    eq_high_state += high_alpha * (x - eq_high_state);
    float high = x - eq_high_state;

    // MID = safe remainder (NO cancellation chain)
    float mid = x - low - high;

    // recombine WITHOUT normalization (important)
    float y = (low * low_gain) +
              (mid * mid_gain) +
              (high * high_gain);

    return y;
}

void init_flanger(bool enable, float depth, float rate, float feedback, float mix) {
    flanger_enabled = enable;
    flanger_depth = depth;
    flanger_rate = rate;
    flanger_feedback = feedback;
    flanger_mix = mix;
}

float apply_flanger(float x) {

    // LFO (triangle-ish using sine is fine)
    flanger_lfo += flanger_rate;
    if (flanger_lfo > 1.0f) flanger_lfo -= 1.0f;

    float lfo = 0.5f + 0.5f * sinf(2.0f * 3.14159f * flanger_lfo);

    // delay time in samples (2 to ~20 samples = classic flanger)
    float delay = 2.0f + lfo * (20.0f * flanger_depth);

    int read_idx = flanger_idx - (int)delay;
    if (read_idx < 0) read_idx += FLANGER_BUF;

    float delayed = flanger_buf[read_idx];

    // feedback write
    flanger_buf[flanger_idx] = x + delayed * flanger_feedback;

    // advance buffer
    flanger_idx = (flanger_idx + 1) % FLANGER_BUF;

    // mix dry/wet
    return (x * (1.0f - flanger_mix)) + (delayed * flanger_mix);
}

void init_delay(bool enabled, float time, float mix, float feedback) {
    delay_enabled = enabled;
    delay_time = time;
    delay_mix = mix;
    delay_feedback = feedback;
}

float apply_delay(float x) {

    // map 0..1 → 100..12000 samples (log-like feel)
    int delay_time_s = 100 + (int)(delay_time * delay_time * 11900.0f);

    if (delay_time_s >= DELAY_BUF_SIZE)
        delay_time_s = DELAY_BUF_SIZE - 1;

    int read_index = delay_write - delay_time_s;
    if (read_index < 0)
        read_index += DELAY_BUF_SIZE;

    float delayed = delay_buf[read_index];

    // feedback (0..1 mapped to safe range)
    float fb = delay_feedback * 0.7f;  // prevent runaway

    delay_buf[delay_write] = x + delayed * fb;

    delay_write++;
    if (delay_write >= DELAY_BUF_SIZE)
        delay_write = 0;

    // mix
    float wet = delay_mix;
    float dry = 1.0f - wet;

    return x * dry + delayed * wet;
}