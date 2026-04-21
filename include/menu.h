//============================================================================
// menu.h  --  TFT menu system for the synth.  ILI9341 controller, SPI1.
//
// To enable the menu, define USE_TFT_MENU somewhere global (build flag, or a
// "#define USE_TFT_MENU" in the TU that includes this header).  With the
// symbol undefined, all menu / LCD / pot code compiles to nothing.
//
// The menu edits the project-wide synth globals directly.  It does NOT own
// any copies; there is no settings struct.  See the extern block in menu.c.
//
// Keys
//   C  = next menu / next row (Dist/EQ edit) / +1 step (EDIT_VALUE)
//   8  = prev menu / prev row (Dist/EQ edit) / -1 step (EDIT_VALUE)
//   9  = go deeper  (BROWSE→EDIT, Dist/EQ EDIT→EDIT_VALUE).
//        On Base/BROWSE, 9 instead refreshes the values from the pots.
//   #  = back one level (EDIT_VALUE→EDIT, EDIT→BROWSE)
//
// State machine
//   BROWSE       8 = prev menu,  C = next menu,  # = n/a
//                9 = enter edit  (OR manual pot-refresh on Base)
//     Base menu  has no EDIT mode — 9 is a "read pots now" button.  There
//                is no background polling; values hold between refreshes.
//   EDIT
//     Waveform     8/C cycles wave         9/# = exit
//     Dist/EQ      8/C selects a row       9 = drill in → EDIT_VALUE
//                                          # = exit to BROWSE
//   EDIT_VALUE   8 = -step, C = +step      9/# = back to EDIT
//============================================================================
#ifndef MENU_H
#define MENU_H

#ifdef USE_TFT_MENU

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    MENU_BASE = 0,
    MENU_WAVEFORM,
    MENU_DISTORTION,
    MENU_FLANGER,
    MENU_DELAY,
    MENU_EQUALIZER,
    NUM_MENUS
} menu_t;

extern menu_t current_menu;

// One-time init.  Call AFTER LCD_Setup() and LCD_DMA_Init().
void menu_init(void);

// Feed a key press ('8', '9', 'C') into the state machine.  Presses only.
void menu_handle_key(char key);

// Call every main-loop iteration.  Pulls pots (Base/EDIT only) and redraws
// only the rows that changed against the shadow state.
void menu_tick(void);

#endif // USE_TFT_MENU
#endif // MENU_H