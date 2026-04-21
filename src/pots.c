//============================================================================
// pots.c  --  Blocking ADC sampling for 5 pots on GPIO 41..45.
//
// This replaces the earlier round-robin + DMA implementation.  The previous
// version could silently drop samples during DMA-chain rearm, which caused
// the pot-to-slot mapping to rotate ("turn knob 1, value 3 moves").  The
// blocking version is trivially correct because the caller picks the
// channel for every single read.
//============================================================================
#ifdef USE_TFT_MENU

#include "pots.h"

#include "pico/stdlib.h"
#include "hardware/adc.h"

uint16_t pot_raw[NUM_POTS];

void pots_init(void) {
    adc_init();
    for (int i = 0; i < NUM_POTS; i++) {
        adc_gpio_init(POT_GPIO_BASE + i);
    }
    // No FIFO, no DMA — we sample directly via adc_read() on demand.
    adc_fifo_setup(false, false, 0, false, false);
    // Make the first read fast by pre-selecting ADC1.  adc_select_input()
    // will override this anyway.
    adc_select_input(POT_ADC_BASE);
}

void pots_sample_all(void) {
    for (int i = 0; i < NUM_POTS; i++) {
        adc_select_input(POT_ADC_BASE + i);
        pot_raw[i] = adc_read();   // blocking ~2 us per channel
    }
}

int pot_scaled(int idx) {
    if (idx < 0 || idx >= NUM_POTS) return 0;
    // 12-bit ADC (0..4095) → 0..100 with rounding.
    int v = ((int)pot_raw[idx] * 100 + 2047) / 4095;
    if (v > 100) v = 100;
    if (v < 0)   v = 0;
    return v;
}

int pot_scaled_range(int idx, int lo, int hi) {
    int s = pot_scaled(idx);
    return lo + ((hi - lo) * s) / 100;
}

#endif // USE_TFT_MENU