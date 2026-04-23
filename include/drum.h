//============================================================================
// drum.h  --  Drum sample playback, integrated with the synth audio engine.
//
// The drum machine in drum_machine2.c owned the whole audio handler.  Here
// we break it up: this module exposes two things that the existing
// pwm_audio_handler (in audio.c) can call:
//
//   drum_trigger(n)   — start playing drum sample n (called from the
//                       keypad handler or the sequencer).
//   drum_mix_sample() — called once per audio sample from inside the
//                       audio IRQ; returns a signed int16 contribution
//                       to be added to the synth mix.
//
// The module does NOT own the PWM IRQ — audio.c does.  All this module
// does is advance sample playback on each tick and produce the summed
// drum sample for that tick.
//============================================================================
#ifndef DRUM_H
#define DRUM_H

#include <stdint.h>
#include <stdbool.h>

// Seven drums, matching the samples that came with drum_machine2.c.
// These indices are the drum's bit position in the sequencer's drum_mask.
#define DRUM_KICK     0
#define DRUM_SNARE    1
#define DRUM_HIHAT    2
#define DRUM_CLAP     3
#define DRUM_COWBELL  4
#define DRUM_RIDE     5
#define DRUM_TOM      6
#define NUM_DRUMS     7

void    drum_init(void);
void    drum_trigger(uint8_t which);   // restart sample `which` from the top
int32_t drum_mix_sample(void);         // advance all playing drums by 1 tick,
                                       // return signed sum centred on zero

#endif // DRUM_H