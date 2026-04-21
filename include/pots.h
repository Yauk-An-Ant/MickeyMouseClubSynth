//============================================================================
// pots.h  --  Blocking ADC sampling for the 5 Base-menu pots.
//
// Pin mapping (fixed — matches the Base menu row order exactly):
//
//      GPIO 41  (ADC1)  →  POT_ATTACK   →  attack_time    (0..1)
//      GPIO 42  (ADC2)  →  POT_DECAY    →  decay_time     (0..1)
//      GPIO 43  (ADC3)  →  POT_SUSTAIN  →  sustain_level  (0..1)
//      GPIO 44  (ADC4)  →  POT_RELEASE  →  release_time   (0..1)
//      GPIO 45  (ADC5)  →  POT_VOLUME   →  volume         (0..1800)
//
// Sampling strategy
// -----------------
// We do NOT use round-robin + DMA — that approach can silently drop samples
// in the gap between DMA completion and rearm, which causes the pot→slot
// mapping to rotate ("turn knob 1, watch knob 3's value move").
//
// Instead we expose pots_sample_all(), a blocking call that takes ~10 us
// for 5 channels.  It's meant to be invoked from a timer IRQ (see menu.c's
// 1 ms Base timer) — the cost is ~1% CPU at 1 kHz, and the mapping is
// trivially correct because the caller picks the channel for every read.
//
// Guarded by USE_TFT_MENU.
//============================================================================
#ifndef POTS_H
#define POTS_H

#ifdef USE_TFT_MENU

#include <stdint.h>

// ---- Pin configuration ------------------------------------------------------
// GPIO41..45 → ADC1..ADC5 on RP2350B.  Move the pots by changing these two
// macros (as long as the channels stay contiguous).
#define POT_GPIO_BASE   41                    // first pot GPIO
#define NUM_POTS        5                     // pots on GPIO 41..45
#define POT_ADC_BASE    (POT_GPIO_BASE - 40)  // ADC channel of first pot

// Pot index → Base-menu row.  Change here to remap knobs.
#define POT_ATTACK      0   // GPIO 41
#define POT_DECAY       1   // GPIO 42
#define POT_SUSTAIN     2   // GPIO 43
#define POT_RELEASE     3   // GPIO 44
#define POT_VOLUME      4   // GPIO 45

// Latest 12-bit readings.  Populated by pots_sample_all().  Not volatile —
// writes happen from the same CPU that reads, just via an IRQ.
extern uint16_t pot_raw[NUM_POTS];

// One-time hardware init: turn on the ADC, wire up the GPIO pins.  Does not
// start any sampling; callers drive that themselves.
void pots_init(void);

// Sample every pot in order and refresh pot_raw[].  Blocking, ~2 us per
// channel on the RP2350's ADC (~10 us for 5 pots).  Call from the Base
// timer IRQ or anywhere else you want a fresh snapshot.
void pots_sample_all(void);

// pot `idx` scaled to 0..100.
int pot_scaled(int idx);

// pot `idx` scaled to an arbitrary integer range [lo, hi].
int pot_scaled_range(int idx, int lo, int hi);

#endif // USE_TFT_MENU
#endif // POTS_H