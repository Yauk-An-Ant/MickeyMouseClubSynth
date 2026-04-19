#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "support.h"
#include "queue.h"
#include "sequencer.c"

void init_pwm_audio();
void pwm_audio_handler();

void pwm_audio_handler() {
    uint slice = 8u + (((36) >> 1u) & 3u);

    pwm_hw->intr = (1ul << slice);
    offset0 += step0;
    offset1 += step1;

    if(offset0 >= (N << 16))
        offset0 -= (N << 16);

    uint samp = wavetable[offset0 >> 16] + wavetable[offset1 >> 16];
    samp /= 2;
    samp = samp * pwm_hw->slice[slice].top / (1ul << 16);
    sequencer_process();
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
    sequencer_init();

    init_pwm_audio(); 

    // set_freq(0, 440.0f); // Set initial frequency to 440 Hz (A4 note)
    // set_freq(1, 0.0f); // Turn off channel 1 initially
    // set_freq(0, 261.626f);
    // set_freq(1, 329.628f);

    set_note(0, A, 4); // Set initial frequency for channel 0

    for(;;) {
        uint16_t keyevent = key_pop();

        if(keyevent & 0x100) {
            char key = keyevent & 0xFF;
            switch(key) {
                case '1': set_note(0, C, octave); break;
                case '2': set_note(0, CS, octave); break;
                case '3': set_note(0, D, octave); break;
                case '4': set_note(0, DS, octave); break;
                case '5': set_note(0, E, octave); break ;
                case '6': set_note(0, F, octave); break;
                case '7': set_note(0, FS, octave); break;
                case '8': set_note(0, G, octave); break;
                case '9': set_note(0, GS, octave); break;
                case '*': set_note(0, A, octave); break;
                case '0': set_note(0, AS, octave); break;
                case '#': set_note(0, B, octave); break;
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
        }
        else {
            char key = keyevent & 0xFF;
            switch (key) {
                case 'A':
                    if (mode == RECORD)
                        sequencer_set_mode(IDLE);
                    else
                        sequencer_set_mode(RECORD);
                    break;
                case 'B':
                    if (mode == PLAY)
                        sequencer_set_mode(IDLE);
                    else
                        sequencer_set_mode(PLAY);
                    break;
                default:
                    break;
            }
        }
    }
    return 0;
}

