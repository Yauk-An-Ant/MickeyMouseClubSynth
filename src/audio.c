#include "audio.h"
#include "sequencer.h"   // for sequencer_process() called once per sample

void pwm_audio_handler() {
    uint slice = 8u + (((36) >> 1u) & 3u);
    int32_t mix = 0;
    int active_count = 0;

    pwm_hw->intr = 1ul << slice;
    // Advance the sequencer clock.  Cheap (one int increment + compare) and
    // gives us rock-steady tempo regardless of what the main loop is doing.
    sequencer_process();

    
    for(int i = 0; i < MAX_VOICES; i++) {
        if(voices[i].active && voices[i].envelope_state != IDLE) {
            voices[i].offset += voices[i].step;

            if(voices[i].offset >= (N << 16))
                voices[i].offset -= (N << 16);

            float env = voices[i].envelope_level;

            switch(voices[i].envelope_state) {
                case ATTACK:
                    env += (1.0f - env) * attack_inc;
                    if(env >= 0.999f) {
                        env = 1.0f;
                        voices[i].envelope_state = DECAY;
                    }
                    break;
                case DECAY:
                    env *= decay_time * (sustain_level - env);
                    if((env - sustain_level) < 0.001f || (env - sustain_level) > -0.001f) {
                        env = sustain_level;
                        voices[i].envelope_state = SUSTAIN;
                    }
                    break;
                case SUSTAIN:
                    break;
                case RELEASE:
                    env *= release_time * (0.0f - env);
                    if(env <= 0.01f) {
                        env = 0.0f;
                        voices[i].envelope_state = IDLE;
                        voices[i].active = 0;   // NOW we turn it off
                    }
                    break;
                case IDLE:
                default:
                    break;
            }

            if(env < 0.0f) env = 0.0f;
            if(env > 1.0f) env = 1.0f;

            static float env_smoothed = 0.0f;
            env_smoothed += (env - env_smoothed) * 0.05f;

            voices[i].envelope_level = env;
            int32_t sample = wavetable[voices[i].offset >> 16] - 16384;
            sample = (uint32_t)(sample * env_smoothed);
            sample += 16384;
            mix += sample >> 3;
            active_count++;
        }
    }

    float x = (float)mix / 16384.0f;

    if (distortion_enabled) {
        x = apply_distortion(x);
    }

    x = apply_eq(x);

    if(flanger_enabled) {
        x = apply_flanger(x);
    }

    if(delay_enabled) {
        x = apply_delay(x);
    }

    if (x > 1.0f)
        x = 1.0f + (x - 1.0f) / (1.0f + (x - 1.0f));
    if (x < -1.0f)
        x = -1.0f + (x + 1.0f) / (1.0f - (x + 1.0f));

    mix = (int32_t)(x * 16384.0f);

    // final safety clamp
    if(mix > 16383) mix = 16383;
    if(mix < -16384) mix = -16384;

    uint32_t samp = (uint32_t)(mix + 16384);

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
    
    pwm_hw->intr = 1u << slice;
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