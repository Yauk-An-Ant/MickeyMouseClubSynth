#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "queue.h"
#include "sequencer.h"

int main() {
    //code here
    stdio_init_all();
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

    //Set Parameters
    //base_volume = 0.5f;

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
                // case 'A': if(octave < 6) octave++; printf("octave: %d\n", octave); break;
                // case 'B': if(octave > 2) octave--; printf("octave: %d\n", octave); break;
                case 'C':
                    if(wave == SINE) {
                        printf("Triangle Wave\n");
                        init_wavetable(TRIANGLE);
                        wave = TRIANGLE;
                    } else if(wave == TRIANGLE) {
                        printf("Sawtooth Wave\n");
                        init_wavetable(SAWTOOTH);
                        wave = SAWTOOTH;
                    } else if(wave == SAWTOOTH) {
                        printf("Square Wave\n");
                        init_wavetable(SQUARE);
                        wave = SQUARE;
                    } else if(wave == SQUARE) {
                        printf("Sine Wave\n");
                        init_wavetable(SINE);
                        wave = SINE;
                    }
                    break;
                case 'D':
                    if(wave == SINE) {
                        printf("Square Wave\n");
                        init_wavetable(SQUARE);
                        wave = SQUARE;
                    } else if(wave == TRIANGLE) {
                        printf("Sine Wave\n");
                        init_wavetable(SINE);
                        wave = SINE;
                    } else if(wave == SAWTOOTH) {
                        printf("Triangle Wave\n");
                        init_wavetable(TRIANGLE);
                        wave = TRIANGLE;
                    } else if(wave == SQUARE) {
                        printf("Sawtooth Wave\n");
                        init_wavetable(SAWTOOTH);
                        wave = SAWTOOTH;
                    }
                case 'A':
                    if (mode == RECORD)
                        sequencer_set_mode(SEQ_IDLE);
                    else
                        sequencer_set_mode(RECORD);
                    break;
                case 'B':
                    if (mode == PLAY)
                        sequencer_set_mode(SEQ_IDLE);
                    else
                        sequencer_set_mode(PLAY);
                    break;
                default:
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

