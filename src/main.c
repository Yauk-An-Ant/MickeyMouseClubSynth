#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "support.h"
#include "queue.h"

int key_voice[12];

void init_pwm_audio();
void pwm_audio_handler();

void pwm_audio_handler() {
    uint slice = 8u + (((36) >> 1u) & 3u);
    uint32_t mix = 0;
    int active_count = 0;

    for(int i = 0; i < MAX_VOICES; i++) {
        if(voices[i].active) {
            voices[i].offset += voices[i].step;

            if(voices[i].offset >= (N << 16))
                voices[i].offset -= (N << 16);

            mix += wavetable[voices[i].offset >> 16];
            active_count++;
        }
    }

    if(active_count > 0)
        mix /= active_count;
    else
        mix = 0;

    uint samp = mix;

    samp = samp * pwm_hw->slice[slice].top / (1ul << 16);
    hw_write_masked(
        &pwm_hw->slice[slice].cc,
        samp << ((36 & 1u) ? PWM_CH0_CC_B_LSB : PWM_CH0_CC_A_LSB),
        (36 & 1u) ? PWM_CH0_CC_B_BITS : PWM_CH0_CC_A_BITS
    );
}

void init_pwm_audio() {
    uint slice = 8u + (((36) >> 1u) & 3u);
    uint channel = 36 & 1u;

    gpio_set_function(36, GPIO_FUNC_PWM);
    pwm_set_clkdiv(slice, 150);
    pwm_hw->slice[slice].top = ((clock_get_hz(clk_sys) / 150)/RATE) - 1;
    hw_write_masked(&pwm_hw->slice[slice].cc, ((uint)(0)) << (channel ? PWM_CH0_CC_B_LSB : PWM_CH0_CC_A_LSB), channel ? PWM_CH0_CC_B_BITS : PWM_CH0_CC_A_BITS);
    hw_write_masked(&pwm_hw->slice[slice].csr, 1ul << PWM_CH0_CSR_EN_LSB, PWM_CH0_CSR_EN_BITS);
    
    init_wavetable(SINE);
    
    pwm_hw->intr = 1u << 36;
    pwm_irqn_set_slice_enabled(0, slice, true);
    pwm_set_irq_enabled(slice, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP_0, pwm_audio_handler);
    irq_set_enabled(PWM_IRQ_WRAP_0, true);

    for(int i = 0; i < 12; i++)
        key_voice[i] = -1;
}

int key_index(char key) {
    switch(key) {
        case '1': return 0;  
        case '2': return 1;  
        case '3': return 2;  
        case '4': return 3;  
        case '5': return 4;  
        case '6': return 5;  
        case '7': return 6;  
        case '8': return 7;  
        case '9': return 8;  
        case '*': return 9;  
        case '0': return 10; 
        case '#': return 11; 
        default: return -1;
    }
}

int allocate_voice() {
    for(int i = 0; i < MAX_VOICES; i++) {
        if(!voices[i].active)
            return i;
    }
    return -1;
}

int main() {
    //code here
    char freq_buf[9] = {0};
    int pos = 0;
    bool decimal_entered = false;
    int decimal_pos = 0;
    int current_channel = 0;
    uint octave = 3;
    wave_t wave = SINE;

    keypad_init_pins();
    keypad_init_timer();

    init_pwm_audio(); 

    for(int i = 0; i < MAX_VOICES; i++) {
        voices[i].active = 0;
        voices[i].step = 0;
        voices[i].offset = 0;
    }

    // set_freq(0, 440.0f); // Set initial frequency to 440 Hz (A4 note)
    // set_freq(1, 0.0f); // Turn off channel 1 initially
    // set_freq(0, 261.626f);
    // set_freq(1, 329.628f);

    //set_note(0, A, 4); // Set initial frequency for channel 0

    for(;;) {
        uint16_t keyevent = key_pop();

        if(keyevent & 0x100) {
            //On key press
            char key = keyevent & 0xFF;
            int k = key_index(key);
            if(k >= 0) {
                int voice = allocate_voice();
                if(voice >= 0) {
                    key_voice[k] = voice;
                    set_note(voice, (note_t)k, octave);
                }
            }

            switch(key) {
                case 'A': if(octave < 6) octave++; break;
                case 'B': if(octave > 2) octave--; break;
                case 'C':
                    if(wave == SINE) {
                        init_wavetable(TRIANGLE);
                        wave = TRIANGLE;
                    } else if(wave == TRIANGLE) {
                        init_wavetable(SAWTOOTH);
                        wave = SAWTOOTH;
                    } else if(wave == SAWTOOTH) {
                        init_wavetable(SQUARE);
                        wave = SQUARE;
                    } else if(wave == SQUARE) {
                        init_wavetable(SINE);
                        wave = SINE;
                    }
                    break;
                case 'D':
                    if(wave == SINE) {
                        init_wavetable(SQUARE);
                        wave = SQUARE;
                    } else if(wave == TRIANGLE) {
                        init_wavetable(SINE);
                        wave = SINE;
                    } else if(wave == SAWTOOTH) {
                        init_wavetable(TRIANGLE);
                        wave = TRIANGLE;
                    } else if(wave == SQUARE) {
                        init_wavetable(SAWTOOTH);
                        wave = SAWTOOTH;
                    }
                    break;
            }
        } else {
            //Key released
            char key = keyevent & 0xFF;
            int k = key_index(key);
            if(k >= 0) {
                int voice = key_voice[k];
                if(voice >= 0 && voice < MAX_VOICES) {
                    voices[voice].active = 0;
                }
                key_voice[k] = -1;
            }
            
            // if(key != 'A' && key != 'B' && key != 'C' && key != 'D') {
            //     step0 = 0;
            // }

        }
    }
    return 0;
}

