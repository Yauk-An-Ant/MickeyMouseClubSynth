//============================================================================
// eeprom.h  --  24AA32AF EEPROM driver + sequencer persistence.
//
// Hardware (per the lab's I²C lab readme)
// ---------------------------------------
//   24AA32AF = 32 Kbit (4096 byte) I²C EEPROM.
//   Bus: I²C1 on the RP2350 — GP14 SDA, GP15 SCL.
//   Address pins A0/A1/A2 tied to GND  →  7-bit I²C address 0x50.
//   External 2 kΩ pull-ups on SDA and SCL to 3.3 V.
//   WP pin tied to GND (writes enabled).
//
// Two layers
// ----------
//   Low level — raw byte read/write at any EEPROM address:
//     eeprom_init(), eeprom_read(), eeprom_write()
//     These mirror the signatures the readme asks for (`loc, data, len`).
//
//   Sequencer persistence — save/load the full pattern:
//     eeprom_save_sequencer(), eeprom_load_sequencer()
//     These sit on top of the low-level API with a magic-header layout
//     so a blank/corrupt chip doesn't get loaded as a real pattern.
//============================================================================
#ifndef EEPROM_H
#define EEPROM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ---- Bus / pin / address (edit these if your wiring is different) ---------
#ifndef EEPROM_I2C
#define EEPROM_I2C        i2c1
#endif
#ifndef EEPROM_PIN_SDA
#define EEPROM_PIN_SDA    14
#endif
#ifndef EEPROM_PIN_SCL
#define EEPROM_PIN_SCL    15
#endif
// A2=A1=A0=GND per the readme → 0b1010_000 = 0x50.
#ifndef EEPROM_I2C_ADDR
#define EEPROM_I2C_ADDR   0x50
#endif
// 400 kHz Fast Mode (readme recommendation for 2 kΩ pull-ups).
#ifndef EEPROM_I2C_HZ
#define EEPROM_I2C_HZ     400000
#endif

// Chip characteristics (24AA32AF datasheet).
#define EEPROM_SIZE_BYTES    4096
#define EEPROM_PAGE_SIZE     32      // writes cannot cross a page boundary

// ---- Low-level API --------------------------------------------------------
// Bring up the I²C peripheral and pin muxes.  Probes the chip with a
// write-then-readback and returns false on any failure.
bool eeprom_init(void);

// Write `len` bytes from `data` starting at EEPROM address `loc`.  Handles
// the 32-byte page boundary automatically — callers don't have to split
// their own writes.  Blocking; can take several ms per page.  Returns true
// on success.
bool eeprom_write(uint16_t loc, const uint8_t *data, size_t len);

// Read `len` bytes starting at EEPROM address `loc` into `data`.  No page
// constraints for reads — the chip sequentially increments its internal
// address counter.  Returns true on success.
bool eeprom_read(uint16_t loc, uint8_t *data, size_t len);

// ---- Sequencer persistence ------------------------------------------------
// Serialise the current sequencer state (steps[] + length) into EEPROM.
// Blocking — ~50-100 ms for a typical pattern.  Returns true on success.
bool eeprom_save_sequencer(void);

// Deserialise a previously-saved pattern, overwriting the current steps[]/
// length.  Returns true on success, false on a blank/corrupt chip (safe
// to call unconditionally at boot).
bool eeprom_load_sequencer(void);

// Quick check — does the EEPROM contain a valid saved pattern?  Reads the
// magic header only, no full load.
bool eeprom_has_saved_pattern(void);

#endif // EEPROM_H