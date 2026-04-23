//============================================================================
// eeprom.c  --  24AA32AF EEPROM driver + sequencer persistence.
//
// Implementation follows the I²C lab readme's `eeprom_write` / `eeprom_read`
// recipe exactly:
//
//   WRITE:
//     START → [addr<<1 | W]A → [loc_hi]A → [loc_lo]A → [data...]A... → STOP
//     Then the chip is busy for up to 5 ms doing its internal write cycle.
//     If you hit it again before that, it NACKs.  We just sleep 6 ms.
//
//   READ:
//     (zero-byte write to set the address pointer)
//     START → [addr<<1 | W]A → [loc_hi]A → [loc_lo]A → STOP
//     (then the read)
//     START → [addr<<1 | R]A → data → STOP
//
// Sequencer layout in EEPROM
// --------------------------
//   Page 0 (0x000–0x01F)  — header:
//     [0..3]   magic "SEQ1" = 0x31514553 (little-endian)
//     [4]      length (0..SEQ_MAX_STEPS)
//     [5..31]  reserved (zero)
//
//   Pages 1+ (0x020..) — steps, 16 bytes per step, 2 steps per page.
//     Page-aligned writes = no cross-page splits to worry about.
//     128 steps × 16 bytes = 2048 bytes, under half the chip.
//============================================================================
#include "eeprom.h"
#include "sequencer.h"

#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

// ---- Layout ----------------------------------------------------------------
#define EE_HEADER_ADDR      0x0000
#define EE_HEADER_MAGIC     0x31514553u   // "SEQ1"
#define EE_HEADER_SIZE      32            // page 0
#define EE_STEPS_ADDR       0x0020        // page 1 onward
#define EE_STEP_BYTES       16            // bytes per serialised step

// ============================================================================
// Low-level helpers
// ============================================================================
// The EEPROM uses 16-bit internal addressing (MSB first, then LSB).  Every
// write and every address-pointer-set shares the same prefix: 2 bytes on
// the wire after the device address.

// Unconditional sleep covering the chip's write cycle.  Datasheet: max 5 ms,
// typical 3 ms.  Sleeping is more reliable than ACK-polling, which depends
// on SDK version quirks for zero/short packets.
static void wait_write_cycle(void) {
    sleep_ms(6);
}

// Write a chunk of up to EEPROM_PAGE_SIZE bytes starting at `loc`.  The
// caller guarantees the chunk doesn't cross a 32-byte page boundary.
// Includes a small retry loop so a previous still-in-progress write cycle
// can naturally clear itself.
static bool write_chunk(uint16_t loc, const uint8_t *data, size_t len) {
    if (len == 0 || len > EEPROM_PAGE_SIZE) return (len == 0);

    // [loc_hi][loc_lo][data...]
    uint8_t buf[2 + EEPROM_PAGE_SIZE];
    buf[0] = (uint8_t)(loc >> 8);
    buf[1] = (uint8_t)(loc & 0xFF);
    memcpy(&buf[2], data, len);

    for (int attempt = 0; attempt < 3; attempt++) {
        int n = i2c_write_blocking(EEPROM_I2C, EEPROM_I2C_ADDR,
                                   buf, 2 + len, false);
        if (n == (int)(2 + len)) {
            wait_write_cycle();
            return true;
        }
        // NACK or timeout — chip is probably still busy from a previous
        // cycle.  Wait and try again.
        sleep_ms(6);
    }
    printf("eeprom: write@0x%04x failed (chip NACKing)\n", loc);
    return false;
}

// ============================================================================
// Public low-level API — matches the readme's signatures
// ============================================================================
bool eeprom_init(void) {
    // --- I²C peripheral + pin mux ------------------------------------------
    i2c_init(EEPROM_I2C, EEPROM_I2C_HZ);
    gpio_set_function(EEPROM_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(EEPROM_PIN_SCL, GPIO_FUNC_I2C);
    // External 2 kΩ pull-ups are on the board per the readme.  Internal
    // pull-ups are enabled too; harmless, and covers bare breadboards.
    gpio_pull_up(EEPROM_PIN_SDA);
    gpio_pull_up(EEPROM_PIN_SCL);

    // --- Probe: write + read-back a sentinel byte at the last chip address
    // (outside our sequencer layout, so we don't clobber saved state).
    // Writing is the stricter test — if writes don't work, reads alone
    // aren't useful.
    uint8_t want = 0xA5;
    uint8_t got  = 0;
    if (!eeprom_write(0x0FFF, &want, 1)) {
        printf("eeprom: probe write failed (addr 0x%02x)\n", EEPROM_I2C_ADDR);
        return false;
    }
    if (!eeprom_read(0x0FFF, &got, 1)) {
        printf("eeprom: probe read failed (addr 0x%02x)\n", EEPROM_I2C_ADDR);
        return false;
    }
    if (got != want) {
        printf("eeprom: probe readback mismatch (wrote 0x%02x, got 0x%02x)\n",
               want, got);
        return false;
    }

    printf("eeprom: init OK (device 0x%02x on I2C1, write verified)\n",
           EEPROM_I2C_ADDR);
    return true;
}

bool eeprom_write(uint16_t loc, const uint8_t *data, size_t len) {
    if (len == 0) return true;
    if ((uint32_t)loc + len > EEPROM_SIZE_BYTES) return false;

    // Split the write at page boundaries.  The chip wraps within a page
    // if you cross one in a single transaction — so we chunk manually.
    while (len > 0) {
        uint16_t page_end = (loc & ~(EEPROM_PAGE_SIZE - 1)) + EEPROM_PAGE_SIZE;
        size_t   chunk    = page_end - loc;
        if (chunk > len) chunk = len;

        if (!write_chunk(loc, data, chunk)) return false;

        loc  += chunk;
        data += chunk;
        len  -= chunk;
    }
    return true;
}

bool eeprom_read(uint16_t loc, uint8_t *data, size_t len) {
    if (len == 0) return true;
    if ((uint32_t)loc + len > EEPROM_SIZE_BYTES) return false;

    // Step 1: "zero-byte write" to set the internal address pointer.
    // The readme describes this explicitly: write the address then keep
    // the bus for a repeated-start read.  The last param `true` = no
    // STOP, so the next i2c_read_blocking issues a repeated START.
    uint8_t ptr[2] = { (uint8_t)(loc >> 8), (uint8_t)(loc & 0xFF) };
    if (i2c_write_blocking(EEPROM_I2C, EEPROM_I2C_ADDR, ptr, 2, true) != 2) {
        return false;
    }

    // Step 2: read the data.  The EEPROM auto-increments its internal
    // address on each byte read, so we can pull as many as we want in
    // one transaction (no page limit for reads).
    int n = i2c_read_blocking(EEPROM_I2C, EEPROM_I2C_ADDR, data, len, false);
    return n == (int)len;
}

// ============================================================================
// Sequencer persistence
// ============================================================================
// One Step → 16 bytes on the wire.  Layout:
//   [0]      num_notes           (0 = rest)
//   [1]      flags  bit0=tie, bit1=is_rest
//   [2..5]   notes[0..3]         (one byte each; note_t fits in 8 bits)
//   [6..9]   octaves[0..3]
//   [10..15] reserved (zero)
static void pack_step(const Step *s, uint8_t out[EE_STEP_BYTES]) {
    memset(out, 0, EE_STEP_BYTES);
    out[0] = s->num_notes;
    out[1] = (s->tie ? 0x01 : 0) | (s->is_rest ? 0x02 : 0);
    for (int i = 0; i < SEQ_MAX_CHORD; i++) {
        out[2 + i] = (uint8_t)s->notes[i];
        out[6 + i] = s->octaves[i];
    }
}

static void unpack_step(const uint8_t in[EE_STEP_BYTES], Step *s) {
    memset(s, 0, sizeof(*s));
    s->num_notes = in[0];
    if (s->num_notes > SEQ_MAX_CHORD) s->num_notes = SEQ_MAX_CHORD;
    s->tie     = (in[1] & 0x01) != 0;
    s->is_rest = (in[1] & 0x02) != 0;
    for (int i = 0; i < SEQ_MAX_CHORD; i++) {
        s->notes  [i] = (note_t)in[2 + i];
        s->octaves[i] = in[6 + i];
    }
}

bool eeprom_has_saved_pattern(void) {
    uint8_t hdr[4];
    if (!eeprom_read(EE_HEADER_ADDR, hdr, sizeof(hdr))) return false;
    uint32_t magic = (uint32_t)hdr[0]
                   | ((uint32_t)hdr[1] << 8)
                   | ((uint32_t)hdr[2] << 16)
                   | ((uint32_t)hdr[3] << 24);
    return magic == EE_HEADER_MAGIC;
}

bool eeprom_save_sequencer(void) {
    // --- Header (page 0) --------------------------------------------------
    uint8_t header[EE_HEADER_SIZE] = {0};
    header[0] = (uint8_t)(EE_HEADER_MAGIC      );
    header[1] = (uint8_t)(EE_HEADER_MAGIC >>  8);
    header[2] = (uint8_t)(EE_HEADER_MAGIC >> 16);
    header[3] = (uint8_t)(EE_HEADER_MAGIC >> 24);
    header[4] = length;

    if (!eeprom_write(EE_HEADER_ADDR, header, sizeof(header))) {
        printf("eeprom: save failed (header)\n");
        return false;
    }

    // --- Steps (2 per page, page-aligned) ---------------------------------
    uint8_t page[EEPROM_PAGE_SIZE];
    for (uint8_t i = 0; i < length; i += 2) {
        memset(page, 0, sizeof(page));
        pack_step(&steps[i], &page[0]);
        if (i + 1 < length) {
            pack_step(&steps[i + 1], &page[EE_STEP_BYTES]);
        }
        uint16_t loc = EE_STEPS_ADDR + (uint16_t)i * EE_STEP_BYTES;
        if (!eeprom_write(loc, page, sizeof(page))) {
            printf("eeprom: save failed at step %u\n", i);
            return false;
        }
    }

    printf("eeprom: saved %u steps\n", length);
    return true;
}

bool eeprom_load_sequencer(void) {
    // --- Header -----------------------------------------------------------
    uint8_t hdr[EE_HEADER_SIZE];
    if (!eeprom_read(EE_HEADER_ADDR, hdr, sizeof(hdr))) {
        printf("eeprom: load failed (header read)\n");
        return false;
    }

    uint32_t magic = (uint32_t)hdr[0]
                   | ((uint32_t)hdr[1] << 8)
                   | ((uint32_t)hdr[2] << 16)
                   | ((uint32_t)hdr[3] << 24);
    if (magic != EE_HEADER_MAGIC) {
        printf("eeprom: no saved pattern (magic=0x%08lx)\n",
               (unsigned long)magic);
        return false;
    }

    uint8_t saved_len = hdr[4];
    if (saved_len > SEQ_MAX_STEPS) {
        printf("eeprom: header corrupt (length=%u)\n", saved_len);
        return false;
    }
    if (saved_len == 0) {
        length = 0;
        play_index = 0;
        return true;
    }

    // --- Steps ------------------------------------------------------------
    // One big sequential read.  Pad to a multiple of 32 so we always read
    // whole pages; unused trailing bytes are ignored.
    size_t bytes = ((size_t)saved_len * EE_STEP_BYTES + 31) & ~31u;
    uint8_t buf[SEQ_MAX_STEPS * EE_STEP_BYTES];
    if (bytes > sizeof(buf)) bytes = sizeof(buf);

    if (!eeprom_read(EE_STEPS_ADDR, buf, bytes)) {
        printf("eeprom: load failed (step data)\n");
        return false;
    }

    for (uint8_t i = 0; i < saved_len; i++) {
        unpack_step(&buf[(size_t)i * EE_STEP_BYTES], &steps[i]);
    }
    length     = saved_len;
    play_index = 0;

    printf("eeprom: loaded %u steps\n", length);
    return true;
}