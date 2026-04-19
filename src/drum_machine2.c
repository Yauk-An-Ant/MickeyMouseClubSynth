#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "support.h"
#include "queue.h"
#include "samples.h"

void keypad_init_pins(void);
void keypad_init_timer(void);

//audio pin is 36, which is slice 4 channel B

#define AUDIO_SLICE (8u + (((36) >> 1u) & 3u)) 
//add 8 to get to slice 4, then add offset based on pin num
#define AUDIO_CHAN  ((36) & 1u)//0 for A, 1 for B
#define NUM_DRUMS 7 //totale number of audio samples


//all drum samples 
#define DRUM_KICK 0
#define DRUM_SNARE 1
#define DRUM_HIHAT 2
#define DRUM_CLAP 3
#define DRUM_COWBELL 4
#define DRUM_RIDE 5
#define DRUM_TOM 6

//struct to hold info about each drum sample 
typedef struct
{
    const uint16_t *buf; //points for each audio data
    uint32_t len; //length of audio data
    volatile uint32_t pos; //current position in au
    volatile bool active;
} DrumData; // 

static DrumData sampl[NUM_DRUMS];
extern KeyEvents stack; //extern is used to access the queue of key events in queue.c
//think of it like a stack basically

static bool pop(uint16_t *out) //pop stack of key events
{
    if(stack.head == stack.tail)
    {
        return false;
    }

    *out = stack.q[stack.tail]; //get the event of the tail
    stack.tail = (stack.tail+ 1) % 32; //move tail forward, wrap if needed
    return true;
}

static void init_drums(void)  //intialize all drum samples
{
    sampl[DRUM_KICK]= (DrumData){kicks01,kicks01_len, 0, 0};
    sampl[DRUM_SNARE] =(DrumData){snare01,snare01_len, 0, 0};
    sampl[DRUM_HIHAT]= (DrumData){hihats05,hihats05_len,0, 0};
    sampl[DRUM_CLAP]= (DrumData){clap02,clap02_len,0, 0 };
    sampl[DRUM_COWBELL]= (DrumData){cowbell5,cowbell5_len,0, 0};
    sampl[DRUM_RIDE]= (DrumData){ride1,ride1_len,0, 0 };
    sampl[DRUM_TOM]= (DrumData){tom1,tom1_len,0, 0 };
}

void pwm_audio_handler(void)
{
    uint slice = AUDIO_SLICE;
    pwm_hw->intr = 1ul << slice;

    int32_t mix =0; // accumulator for mized audio sample
    int count = 0; // count of active smaples being mixed

    for (int d =0; d < NUM_DRUMS; d++) 
    {
        if (!sampl[d].active) 
        {
            continue;
        }
        mix += (int32_t)sampl[d].buf[sampl[d].pos] - 32768;
        sampl[d].pos++;
        if (sampl[d].pos >= sampl[d].len) {
            sampl[d].active = false;
            sampl[d].pos    = 0;
        }
        count++;
    }

    uint32_t samp; //final sample output

    if(count == 0)
    {
        samp = 32768; //silence

    }
    else
    {
        mix /= count;
        mix += 32768; //shift  back in range
        if(mix < 0)
        {
            mix = 0;
        }
        if(mix > 65535) //clip to max
        {
            mix = 65535;
        }

        samp = (uint32_t)mix;
    }

    samp = samp * pwm_hw->slice[slice].top /(1ul << 16); //scale to PWM range

    hw_write_masked(&pwm_hw->slice[slice].cc, samp << (AUDIO_CHAN ? PWM_CH0_CC_B_LSB : PWM_CH0_CC_A_LSB),
        AUDIO_CHAN ? PWM_CH0_CC_B_BITS : PWM_CH0_CC_A_BITS
    );//write sample to pwm COMPARE REG
}

static void init_pwm_audio(void)
{
    uint slice = AUDIO_SLICE;
    uint channel = AUDIO_CHAN;

    gpio_set_function(36, GPIO_FUNC_PWM);
    pwm_set_clkdiv(slice, 150);

    pwm_hw->slice[slice].top =((clock_get_hz(clk_sys)/150)/ RATE) - 1; //set top

    hw_write_masked(&pwm_hw->slice[slice].cc,((uint)0) << (channel ? PWM_CH0_CC_B_LSB : PWM_CH0_CC_A_LSB),
        channel ? PWM_CH0_CC_B_BITS : PWM_CH0_CC_A_BITS
    ); //start with 0

    hw_write_masked(&pwm_hw->slice[slice].csr,1ul << PWM_CH0_CSR_EN_LSB,
        PWM_CH0_CSR_EN_BITS
    ); //enable slice

    pwm_hw->intr = 1u << slice;
    pwm_irqn_set_slice_enabled(0,slice,1);
    pwm_set_irq_enabled(slice,1);
    irq_set_exclusive_handler(PWM_IRQ_WRAP_0, pwm_audio_handler); //set IRQ
    irq_set_enabled(PWM_IRQ_WRAP_0, true);
}

static void play_drum(uint8_t d)
{ //place a drum sample in queue to be played 
    if(d >=NUM_DRUMS)
    {
        return;
    }
    sampl[d].pos = 0;
    sampl[d].active = 1;
}

static void process_keys(void) {
    uint16_t event; //event from queue
    while(pop(&event))
    {
        if(!(event & 0x100))
        {
            continue;
        }
        char key = (char)(event & 0xFF); //get key char from the event
        switch(key)
        {
            case '1': play_drum(DRUM_KICK); break;
            case '2': play_drum(DRUM_SNARE); break;
            case '3': play_drum(DRUM_HIHAT); break;
            case '4': play_drum(DRUM_CLAP); break;
            case '5': play_drum(DRUM_COWBELL); break;
            case '6': play_drum(DRUM_RIDE); break;
            case '7': play_drum(DRUM_TOM); break;
            default: break;
        }
    }
}

int main(void) {
    stdio_init_all();

    init_drums();
    init_pwm_audio();
    keypad_init_pins();
    keypad_init_timer();

    for(;;)
    {
        process_keys();
    }

    return 0;
}