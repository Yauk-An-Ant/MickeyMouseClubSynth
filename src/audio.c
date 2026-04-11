#include "audio.h"

void pwm_audio_handler() {
    uint slice = 8u + (((36) >> 1u) & 3u);
    uint32_t mix = 0;
    int active_count = 0;

    pwm_hw->intr = 1ul << slice;

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
