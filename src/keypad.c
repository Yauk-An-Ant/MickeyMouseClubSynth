#include "pico/stdlib.h"
#include <hardware/gpio.h>
#include <stdio.h>
#include "queue.h"

// Global column variable
int col = -1;

// Global key state
static bool state[16]; // Are keys pressed/released

// Keymap for the keypad
const char keymap[17] = "DCBA#9630852*741";

// Defined here to avoid circular dependency issues with autotest
// You can see the struct definition in queue.h
// KeyEvents kev = { 
//     .head = 0, 
//     .tail = 0 
// };

void keypad_drive_column();
void keypad_isr();

/********************************************************* */
// Implement the functions below.

void keypad_init_pins() {
     //i be configuring da inputs
    io_bank0_hw->io[2].ctrl = GPIO_FUNC_SIO;
    io_bank0_hw->io[3].ctrl = GPIO_FUNC_SIO;
    io_bank0_hw->io[4].ctrl = GPIO_FUNC_SIO;
    io_bank0_hw->io[5].ctrl = GPIO_FUNC_SIO;

    pads_bank0_hw->io[2] = PADS_BANK0_GPIO0_IE_BITS;
    pads_bank0_hw->io[3] = PADS_BANK0_GPIO0_IE_BITS;
    pads_bank0_hw->io[4] = PADS_BANK0_GPIO0_IE_BITS;
    pads_bank0_hw->io[5] = PADS_BANK0_GPIO0_IE_BITS;

    uint32_t mask = (1ul << 2) | (1ul << 3) | (1ul << 4) | (1ul << 5);
    sio_hw->gpio_oe_clr = mask;

    // AAAAAAAAAAAAAAAAAAA
    pads_bank0_hw->io[2] = (pads_bank0_hw->io[2] & ~(PADS_BANK0_GPIO0_PUE_BITS | PADS_BANK0_GPIO0_PDE_BITS)) | (((0u << PADS_BANK0_GPIO0_PUE_LSB) | (1u << PADS_BANK0_GPIO0_PDE_LSB)) & (PADS_BANK0_GPIO0_PUE_BITS | PADS_BANK0_GPIO0_PDE_BITS)); 
    pads_bank0_hw->io[3] = (pads_bank0_hw->io[3] & ~(PADS_BANK0_GPIO0_PUE_BITS | PADS_BANK0_GPIO0_PDE_BITS)) | (((0u << PADS_BANK0_GPIO0_PUE_LSB) | (1u << PADS_BANK0_GPIO0_PDE_LSB)) & (PADS_BANK0_GPIO0_PUE_BITS | PADS_BANK0_GPIO0_PDE_BITS)); 
    pads_bank0_hw->io[4] = (pads_bank0_hw->io[4] & ~(PADS_BANK0_GPIO0_PUE_BITS | PADS_BANK0_GPIO0_PDE_BITS)) | (((0u << PADS_BANK0_GPIO0_PUE_LSB) | (1u << PADS_BANK0_GPIO0_PDE_LSB)) & (PADS_BANK0_GPIO0_PUE_BITS | PADS_BANK0_GPIO0_PDE_BITS)); 
    pads_bank0_hw->io[5] = (pads_bank0_hw->io[5] & ~(PADS_BANK0_GPIO0_PUE_BITS | PADS_BANK0_GPIO0_PDE_BITS)) | (((0u << PADS_BANK0_GPIO0_PUE_LSB) | (1u << PADS_BANK0_GPIO0_PDE_LSB)) & (PADS_BANK0_GPIO0_PUE_BITS | PADS_BANK0_GPIO0_PDE_BITS)); 

    // output peak
    io_bank0_hw->io[6].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    io_bank0_hw->io[7].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    io_bank0_hw->io[8].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    io_bank0_hw->io[9].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;

    pads_bank0_hw->io[6] = PADS_BANK0_GPIO0_IE_BITS;
    pads_bank0_hw->io[7] = PADS_BANK0_GPIO0_IE_BITS;
    pads_bank0_hw->io[8] = PADS_BANK0_GPIO0_IE_BITS;
    pads_bank0_hw->io[9] = PADS_BANK0_GPIO0_IE_BITS;

    mask = (1ul << 6) | (1ul << 7) | (1ul << 8) | (1ul << 9);
    sio_hw->gpio_oe_set = mask;
    sio_hw->gpio_clr = mask;
}

void keypad_init_timer() {
    timer0_hw->alarm[0] = timer0_hw->timerawl + 1000000;
    irq_set_exclusive_handler(TIMER0_IRQ_0, keypad_drive_column);
    irq_set_enabled(TIMER0_IRQ_0, true);

    timer0_hw->alarm[1] = timer0_hw->timerawl + 1010000;
    irq_set_exclusive_handler(TIMER0_IRQ_1, keypad_isr);
    irq_set_enabled(TIMER0_IRQ_1, true);

    uint32_t mask = (1ul << 0) | (1ul << 1);
    timer_hw->inte = mask;
}

void keypad_drive_column() {
    timer_hw->intr = 1ul << 0;

    col++;

    if(col == 4) {
        col = 0; 
    }

    sio_hw->gpio_clr = (1ul << 6) | (1ul << 7) | (1ul << 8) | (1ul << 9);
    sio_hw->gpio_set = (1ul << (col + 6));

    timer_hw->alarm[0] = timer_hw->timerawl + 25000;
}

uint8_t keypad_read_rows() {
    return ((gpio_get(5) << 3) | (gpio_get(4) << 2) | (gpio_get(3) << 1) | (gpio_get(2))); 
}

void keypad_isr() {
    timer0_hw->intr = 1ul << 1;
    uint8_t row = keypad_read_rows();

    for(int i = 0; i < 4; i++) {
        if(((1ul << i) & row) > 0 && !state[4 * col + i]) {
            state[4 * col + i] = 1;
            key_push((1ul << 8) | keymap[4 * col + i]);
        } else if(((1ul << i) & row) == 0 && state[4 * col + i]) { 
            state[4 * col + i] = 0;
            key_push((0ul << 8) | keymap[4 * col + i]);
        }
    }

    timer_hw->alarm[1] = timer_hw->timerawl + 25000;
}