#ifndef AUDIO_H
#define AUDIO_H

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "support.h"

int key_voice[12];
//extern float base_volume;

void init_pwm_audio(void);
void pwm_audio_handler(void);
int key_index(char key);
int allocate_voice(void);

void set_base_volume(float v);

#endif