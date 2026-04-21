//============================================================================
// menu.c  --  Menu state machine + TFT rendering for ILI9341.
//
// The whole TU is guarded by USE_TFT_MENU so the synth can be built without
// any UI at all.
//
// Value model
// -----------
// The menu edits the project-wide synth globals directly.  Three value types
// are supported:
//
//   VT_FLOAT01   float in [0, 1].  Shown as "0.XX".  Keypad step = 0.01
//                (100 steps across the range).  Pot-driven rows get the
//                pot's 0..100 int / 100.0f.
//
//   VT_INT_VOL   int in [0, 1800] (master volume).  Shown as 4-digit.
//                Pot-driven: scaled from the pot's 0..100 range.  Keypad
//                step = 18 (100 steps across the range).
//
//   VT_WAVE      int in [0, 3].  Shown as Si/Tr/Sa/Sq.  Cycled by 8/C.
//                When it changes, init_wavetable() is called.
//
// Keys
// ----
//   C  = next menu / next row / +1 step
//   8  = prev menu / prev row / -1 step
//   9  = go deeper  (BROWSE→EDIT, Dist/EQ EDIT→EDIT_VALUE).
//        On Base/BROWSE, 9 instead *refreshes* from the pots.
//   #  = back one level (universal cancel; never a dead end)
//
// Base menu behaviour
// -------------------
// Base has no EDIT mode.  Pot values are snapshotted at two moments:
//   (1) when the user switches to the Base page, and
//   (2) whenever the user presses '9' while on Base (manual refresh).
// Between refreshes the displayed values stay put — no timer IRQ, no
// background polling.
//
// Rendering strategy
// ------------------
//   redraw_all()  repaints title + all rows (and the waveform box on that
//                 menu).  Called on state/menu transitions.
//   menu_tick()   diffs each row's value against a shadow copy and repaints
//                 only the ones that changed.  Cheap when nothing moved.
//
// All large fills go through LCD_DMA_* (see lcd_dma.c).
//============================================================================
#ifdef USE_TFT_MENU

#include "menu.h"
#include "pots.h"
#include "lcd.h"
#include "lcd_dma.h"

// wave_t enum + init_wavetable().
#include "support.h"
extern void init_wavetable(wave_t w);

#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Project-wide audio globals.
//
// These are defined in your synth globals header.  If you'd rather include
// that header directly, delete this block and add:
//     #include "synth_globals.h"   // or whatever it's called
// above.  Re-declaring an extern is harmless as long as types match.
// ============================================================================
extern float attack_time;
extern float decay_time;
extern float sustain_level;
extern float release_time;
extern float attack_inc;       // derived by audio layer from attack_time
extern float decay_dec;        // derived by audio layer from decay_time
extern float release_dec;      // derived by audio layer from release_time

extern float distortion;
extern float distortion_volume;
// ON/OFF toggle for the distortion stage.  Define this as `int distortion_on`
// in your project globals (0 = bypass, 1 = active).
extern int   distortion_on;

// Flanger parameters.  Define in project globals as floats in [0,1], plus
// an int `flanger_on` (0 = bypass, 1 = active).
extern int   flanger_on;
extern float flanger_depth;
extern float flanger_rate;
extern float flanger_feedback;
extern float flanger_mix;

// Delay parameters.  Same convention.
extern int   delay_on;
extern float delay_time;
extern float delay_mix;
extern float delay_feedback;

extern float lows;
extern float mids;
extern float highs;

extern int volume;             // 0..1800 (already defined in wavegen.c)

// NOTE: If your audio engine derives attack_inc / decay_dec / release_dec
// from the *_time values, do that derivation in the audio layer whenever a
// note starts (or in an IRQ that runs often enough).  The menu only writes
// the *_time values.

// ============================================================================
// Colour palette (RGB565)
// ============================================================================
#define COL_BG          0x0000   // black
#define COL_FG          0xFFFF   // white
#define COL_TITLE_BG    0x0014   // deep blue
#define COL_ROW_BG      0x0000
#define COL_ROW_SEL     0x3186   // slate: selected row in EDIT
#define COL_ROW_ACTIVE  0x8410   // grey:  row currently in EDIT_VALUE
#define COL_BAR_EMPTY   0x2104   // dim grey
#define COL_BAR_FULL    0x07E0   // green
#define COL_BAR_EDIT    0xFFE0   // yellow (while editing value)
#define COL_WAVE        0x07FF   // cyan
#define COL_WAVE_BG     0x0841

// ============================================================================
// Layout
// ============================================================================
#define TITLE_H         30
#define ROW_H           32
#define NAME_X          12
#define VALUE_X_OFFS    120
#define BAR_X_OFFS      170
#define BAR_W           56
#define BAR_H           10

// ============================================================================
// State
// ============================================================================
typedef enum {
    ES_BROWSE = 0,
    ES_EDIT,
    ES_EDIT_VALUE
} edit_state_t;

menu_t       current_menu = MENU_BASE;
static edit_state_t edit_state  = ES_BROWSE;
static int          selected_row = 0;

extern lcd_dev_t lcddev;
static int scr_w, scr_h;

// Wave index owned by the menu.  Stored as int so we can use its address as
// a static initializer in the row descriptor table.  Mirrors whatever
// init_wavetable() is currently configured with.
static int menu_wave_idx = 0;   // 0=Sine 1=Triangle 2=Sawtooth 3=Square

// ============================================================================
// Row descriptor table
// ============================================================================
typedef enum {
    VT_FLOAT01 = 0,   // float 0..1
    VT_INT_VOL,       // int 0..1800
    VT_WAVE,          // int 0..3
    VT_BOOL           // int 0 / 1 (shown as OFF / ON)
} valtype_t;

typedef struct {
    const char *label;
    valtype_t   type;
    void       *ptr;     // float* or int*, depending on type
} row_desc_t;

static const row_desc_t base_rows[] = {
    { "Attack",  VT_FLOAT01, &attack_time   },
    { "Decay",   VT_FLOAT01, &decay_time    },
    { "Sustain", VT_FLOAT01, &sustain_level },
    { "Release", VT_FLOAT01, &release_time  },
    { "Volume",  VT_INT_VOL, &volume        },
};
static const row_desc_t wave_rows[] = {
    { "Wave",    VT_WAVE,    &menu_wave_idx },
};
// Each effect page starts with its ON/OFF toggle so it's easy to thumb to.
static const row_desc_t dist_rows[] = {
    { "Power",   VT_BOOL,    &distortion_on     },
    { "Drive",   VT_FLOAT01, &distortion        },
    { "Volume",  VT_FLOAT01, &distortion_volume },
};
static const row_desc_t flanger_rows[] = {
    { "Power",    VT_BOOL,    &flanger_on       },
    { "Depth",    VT_FLOAT01, &flanger_depth    },
    { "Rate",     VT_FLOAT01, &flanger_rate     },
    { "Feedback", VT_FLOAT01, &flanger_feedback },
    { "Mix",      VT_FLOAT01, &flanger_mix      },
};
static const row_desc_t delay_rows[] = {
    { "Power",    VT_BOOL,    &delay_on       },
    { "Time",     VT_FLOAT01, &delay_time     },
    { "Mix",      VT_FLOAT01, &delay_mix      },
    { "Feedback", VT_FLOAT01, &delay_feedback },
};
static const row_desc_t eq_rows[] = {
    { "Low",     VT_FLOAT01, &lows  },
    { "Mid",     VT_FLOAT01, &mids  },
    { "High",    VT_FLOAT01, &highs },
};

typedef struct {
    const char       *title;
    const row_desc_t *rows;
    int               nrows;
} menu_desc_t;

// Order here must match the menu_t enum in menu.h.
static const menu_desc_t menus[NUM_MENUS] = {
    { "Base",       base_rows,    (int)(sizeof(base_rows)   /sizeof(base_rows[0]))    },
    { "Waveform",   wave_rows,    (int)(sizeof(wave_rows)   /sizeof(wave_rows[0]))    },
    { "Distortion", dist_rows,    (int)(sizeof(dist_rows)   /sizeof(dist_rows[0]))    },
    { "Flanger",    flanger_rows, (int)(sizeof(flanger_rows)/sizeof(flanger_rows[0])) },
    { "Delay",      delay_rows,   (int)(sizeof(delay_rows)  /sizeof(delay_rows[0]))   },
    { "Equalizer",  eq_rows,      (int)(sizeof(eq_rows)     /sizeof(eq_rows[0]))      },
};

static const char *wave_short[4] = { "Si", "Tr", "Sa", "Sq" };

// ============================================================================
// Shadow state (for diff-based repainting)
// ============================================================================
typedef union {
    float f;
    int   i;
} val_u;

typedef struct {
    menu_t       menu;
    edit_state_t estate;
    int          sel;
    val_u        values[8];
    bool         valid;
} shadow_t;
static shadow_t shadow;

// ============================================================================
// Helpers
// ============================================================================
static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo; if (v > hi) return hi; return v;
}
static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo; if (v > hi) return hi; return v;
}
static int row_y(int i) { return TITLE_H + 4 + i * ROW_H; }

// Read / write a row's value generically.
static val_u row_get(const row_desc_t *r) {
    val_u v;
    switch (r->type) {
        case VT_FLOAT01: v.f = *(float *)r->ptr; break;
        case VT_INT_VOL: v.i = *(int   *)r->ptr; break;
        case VT_WAVE:    v.i = *(int   *)r->ptr; break;
        case VT_BOOL:    v.i = *(int   *)r->ptr ? 1 : 0; break;
    }
    return v;
}
static bool row_eq(const row_desc_t *r, val_u a, val_u b) {
    return (r->type == VT_FLOAT01) ? (a.f == b.f) : (a.i == b.i);
}

// Fill ratio 0..1 for the bar.  VT_WAVE and VT_BOOL rows never draw a bar.
static float row_fill_ratio(const row_desc_t *r, val_u v) {
    switch (r->type) {
        case VT_FLOAT01: return clampf(v.f, 0.0f, 1.0f);
        case VT_INT_VOL: return clampf(v.i / 1800.0f, 0.0f, 1.0f);
        default:         return 0.0f;
    }
}

// Format a row's value into `buf` for display.
static void row_format(const row_desc_t *r, val_u v, char *buf, size_t n) {
    switch (r->type) {
        case VT_FLOAT01: {
            // Avoid depending on %f in printf.  Value is always a small
            // multiple of 0.01 in practice, so fixed-point print is exact.
            int scaled = (int)(v.f * 100.0f + 0.5f);
            if (scaled < 0)       snprintf(buf, n, "0.00");
            else if (scaled >= 100) snprintf(buf, n, "1.00");
            else                   snprintf(buf, n, "0.%02d", scaled);
            break;
        }
        case VT_INT_VOL:
            snprintf(buf, n, "%4d", clampi(v.i, 0, 1800));
            break;
        case VT_WAVE:
            snprintf(buf, n, "%s", wave_short[v.i & 3]);
            break;
        case VT_BOOL:
            snprintf(buf, n, "%s", v.i ? "ON " : "OFF");
            break;
    }
}

// ============================================================================
// Drawing primitives
// ============================================================================
static void draw_title(void) {
    char buf[40];
    const char *tag =
        (edit_state == ES_BROWSE)     ? ""
      : (edit_state == ES_EDIT)       ? "  [EDIT]"
      :                                 "  [ADJ]";
    snprintf(buf, sizeof(buf), "%s%s", menus[current_menu].title, tag);

    LCD_DMA_Fill(0, 0, scr_w - 1, TITLE_H - 1, COL_TITLE_BG);

    int len = (int)strlen(buf);
    int tx  = (scr_w - len * 8) / 2;
    if (tx < 4) tx = 4;
    int ty  = (TITLE_H - 16) / 2;
    LCD_DrawString(tx, ty, COL_FG, COL_TITLE_BG, buf, 16, 0);
}

static void draw_bar(int x, int y, float ratio, u16 fg) {
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    int filled = (int)(ratio * BAR_W + 0.5f);
    if (filled > BAR_W) filled = BAR_W;
    LCD_DrawRectangle(x - 1, y - 1, x + BAR_W, y + BAR_H, COL_FG);
    if (filled > 0)
        LCD_DMA_Fill(x, y, x + filled - 1, y + BAR_H - 1, fg);
    if (filled < BAR_W)
        LCD_DMA_Fill(x + filled, y, x + BAR_W - 1, y + BAR_H - 1, COL_BAR_EMPTY);
}

static void draw_row(int i) {
    const menu_desc_t *md = &menus[current_menu];
    if (i < 0 || i >= md->nrows) return;
    const row_desc_t *r = &md->rows[i];
    val_u v = row_get(r);

    int y = row_y(i);

    u16 bg = COL_ROW_BG;
    if      (edit_state == ES_EDIT       && i == selected_row) bg = COL_ROW_SEL;
    else if (edit_state == ES_EDIT_VALUE && i == selected_row) bg = COL_ROW_ACTIVE;

    LCD_DMA_Fill(0, y, scr_w - 1, y + ROW_H - 2, bg);

    LCD_DrawString(NAME_X, y + (ROW_H - 16) / 2, COL_FG, bg,
                   r->label, 16, 1);

    char buf[12];
    row_format(r, v, buf, sizeof(buf));
    LCD_DrawString(NAME_X + VALUE_X_OFFS, y + (ROW_H - 16) / 2,
                   COL_FG, bg, buf, 16, 1);

    if (r->type != VT_WAVE && r->type != VT_BOOL) {
        u16 bar_col = (edit_state == ES_EDIT_VALUE && i == selected_row)
                      ? COL_BAR_EDIT : COL_BAR_FULL;
        draw_bar(NAME_X + BAR_X_OFFS, y + (ROW_H - BAR_H) / 2,
                 row_fill_ratio(r, v), bar_col);
    }
}

static void draw_waveform(int wave_idx) {
    int box_x0 = 10;
    int box_y0 = row_y(1) + 8;
    int box_x1 = scr_w - 11;
    int box_y1 = scr_h - 10;

    LCD_DMA_Fill(box_x0, box_y0, box_x1, box_y1, COL_WAVE_BG);
    LCD_DrawRectangle(box_x0, box_y0, box_x1, box_y1, COL_FG);

    int w   = box_x1 - box_x0 - 4;
    int h   = box_y1 - box_y0 - 4;
    int cx  = box_x0 + 2;
    int cy  = box_y0 + 2 + h / 2;
    int amp = h / 2 - 2;

    int prev_x = cx, prev_y = cy;
    for (int x = 0; x < w; x++) {
        float t     = (float)x / (float)w;   // 0..1 across box
        float phase = t * 2.0f;              // two cycles
        int   y;
        switch (wave_idx & 3) {
            case 0: // sine
                y = cy - (int)(amp * sinf(phase * 2.0f * (float)M_PI));
                break;
            case 1: { // triangle
                float p = fmodf(phase, 1.0f);
                float v = (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
                y = cy - (int)(amp * v);
            } break;
            case 2: { // sawtooth
                float p = fmodf(phase, 1.0f);
                y = cy - (int)(amp * (2.0f * p - 1.0f));
            } break;
            default: { // square
                float p = fmodf(phase, 1.0f);
                y = cy - ((p < 0.5f) ? amp : -amp);
            } break;
        }
        int px = cx + x;
        LCD_DrawLine(prev_x, prev_y, px, y, COL_WAVE);
        prev_x = px; prev_y = y;
    }
}

// ============================================================================
// Full redraw
// ============================================================================
static void redraw_all(void) {
    draw_title();
    LCD_DMA_Fill(0, TITLE_H, scr_w - 1, scr_h - 1, COL_BG);

    const menu_desc_t *md = &menus[current_menu];
    for (int i = 0; i < md->nrows; i++) draw_row(i);

    if (current_menu == MENU_WAVEFORM)
        draw_waveform(menu_wave_idx);

    shadow.menu   = current_menu;
    shadow.estate = edit_state;
    shadow.sel    = selected_row;
    memset(shadow.values, 0, sizeof(shadow.values));
    for (int i = 0; i < md->nrows; i++) {
        shadow.values[i] = row_get(&md->rows[i]);
    }
    shadow.valid = true;
}

// ============================================================================
// Base-menu pot refresh
// ----------------------------------------------------------------------------
// Base values are refreshed on two triggers:
//   (1) When the user switches to the Base page — so the screen doesn't
//       show stale values the instant you land there.
//   (2) When the user presses key '9' while on Base — manual refresh.
//
// There is no timer IRQ.  Pots are sampled synchronously at those two
// moments via pots_sample_all() (blocking, ~10 us for 5 channels).
// ============================================================================

// Do the pot → global mapping in one place so the two refresh paths can't
// drift apart.  Mapping:
//     GPIO 41 (POT_ATTACK)  → attack_time    (0..1)
//     GPIO 42 (POT_DECAY)   → decay_time     (0..1)
//     GPIO 43 (POT_SUSTAIN) → sustain_level  (0..1)
//     GPIO 44 (POT_RELEASE) → release_time   (0..1)
//     GPIO 45 (POT_VOLUME)  → volume         (0..1800)
// Caller must have run pots_sample_all() first.
static void base_update_from_pots(void) {
    attack_time   = pot_scaled(POT_ATTACK)  / 100.0f;
    decay_time    = pot_scaled(POT_DECAY)   / 100.0f;
    sustain_level = pot_scaled(POT_SUSTAIN) / 100.0f;
    release_time  = pot_scaled(POT_RELEASE) / 100.0f;
    volume        = pot_scaled_range(POT_VOLUME, 0, 1800);
}

// Convenience wrapper: sample + map.  Used by enter_menu(BASE) and by the
// '9'-refresh handler.
static void base_refresh(void) {
    pots_sample_all();
    base_update_from_pots();
}

// ============================================================================
// State transitions
// ============================================================================
static void apply_wave_change(void) {
    wave_t w = SINE;
    switch (menu_wave_idx & 3) {
        case 0: w = SINE;     break;
        case 1: w = TRIANGLE; break;
        case 2: w = SAWTOOTH; break;
        case 3: w = SQUARE;   break;
    }
    init_wavetable(w);
}

static void enter_menu(menu_t m) {
    if (m == current_menu) return;

    // Snapshot pots on the way into Base so the first paint shows real
    // values rather than whatever was last displayed there.
    if (m == MENU_BASE) base_refresh();

    current_menu = m;
    selected_row = 0;
    redraw_all();
}

static void edit_value_step(const row_desc_t *r, int dir) {
    switch (r->type) {
        case VT_FLOAT01: {
            // 100 steps across 0..1 (matches the pot resolution and the
            // "0.XX" display format).
            float *p = (float *)r->ptr;
            *p = clampf(*p + dir * 0.01f, 0.0f, 1.0f);
            break;
        }
        case VT_INT_VOL: {
            int *p = (int *)r->ptr;
            *p = clampi(*p + dir * 18, 0, 1800);
            break;
        }
        case VT_WAVE: {
            int *p = (int *)r->ptr;
            *p = (*p + (dir > 0 ? 1 : 3)) & 3;
            apply_wave_change();
            break;
        }
        case VT_BOOL: {
            // Toggle regardless of direction — +/- both flip.
            int *p = (int *)r->ptr;
            *p = *p ? 0 : 1;
            break;
        }
    }
}

// ============================================================================
// Public API
// ============================================================================
void menu_init(void) {
    scr_w = (int)lcddev.width;
    scr_h = (int)lcddev.height;
    if (scr_w <= 0 || scr_h <= 0) { scr_w = 240; scr_h = 320; }

    // Sensible defaults.  Skip anything you'd rather initialise yourself —
    // the menu will pick up whatever value is already in the global.
    attack_time       = 0.20f;
    decay_time        = 0.30f;
    sustain_level     = 0.70f;
    release_time      = 0.40f;

    distortion        = 0.00f;
    distortion_volume = 0.80f;
    distortion_on     = 0;        // bypassed

    flanger_on        = 0;        // bypassed
    flanger_depth     = 0.50f;
    flanger_rate      = 0.25f;
    flanger_feedback  = 0.30f;
    flanger_mix       = 0.50f;

    delay_on          = 0;        // bypassed
    delay_time        = 0.30f;
    delay_mix         = 0.50f;
    delay_feedback    = 0.40f;

    lows              = 0.50f;
    mids              = 0.50f;
    highs             = 0.50f;
    volume            = 1200;     // was the wavegen.c default
    menu_wave_idx     = 0;        // SINE

    // Ensure the wavetable matches our idea of the current wave.
    apply_wave_change();

    current_menu = MENU_BASE;
    edit_state   = ES_BROWSE;
    selected_row = 0;
    shadow.valid = false;

    // We start on Base — snapshot the pots now so the first paint shows
    // their current positions rather than the defaults above.  After this,
    // Base only refreshes when the user presses '9' or switches back in.
    base_refresh();

    redraw_all();
}

void menu_handle_key(char key) {
    // ------------------------------------------------------------------
    // Universal "back" key.  Pops one level out of the state machine, so
    // no menu/state combination is ever a dead end.
    //   EDIT_VALUE  → EDIT
    //   EDIT        → BROWSE
    //   BROWSE      → no-op (already at the top)
    // ------------------------------------------------------------------
    if (key == '#') {
        if (edit_state == ES_EDIT_VALUE) {
            edit_state = ES_EDIT;
            redraw_all();
        } else if (edit_state == ES_EDIT) {
            edit_state = ES_BROWSE;
            redraw_all();
        }
        return;
    }

    switch (edit_state) {

    // ---------------- BROWSE --------------------------------------------
    case ES_BROWSE:
        if      (key == 'C') enter_menu((menu_t)((current_menu + 1) % NUM_MENUS));
        else if (key == '8') enter_menu((menu_t)((current_menu + NUM_MENUS - 1) % NUM_MENUS));
        else if (key == '9') {
            // On Base, 9 is a manual pot refresh.  On other menus, 9 opens
            // EDIT mode (Base has no EDIT mode — pots are the editor).
            if (current_menu == MENU_BASE) {
                base_refresh();
                // menu_tick() will see the shadow mismatch and redraw only
                // the rows whose values actually changed.
                break;
            }
            edit_state   = ES_EDIT;
            selected_row = 0;
            redraw_all();
        }
        break;

    // ---------------- EDIT ----------------------------------------------
    // Never reached on Base (9 from Base/BROWSE is a refresh, not an enter).
    case ES_EDIT:
        if (key == '9') {
            // Effect menus (Distortion, Flanger, Delay) and EQ all use the
            // "select row then drill in" flow.  Waveform just exits.
            if (current_menu == MENU_DISTORTION
             || current_menu == MENU_FLANGER
             || current_menu == MENU_DELAY
             || current_menu == MENU_EQUALIZER) {
                edit_state = ES_EDIT_VALUE;
            } else {
                edit_state = ES_BROWSE;
            }
            redraw_all();
            break;
        }
        switch (current_menu) {
            case MENU_WAVEFORM:
                if      (key == 'C') menu_wave_idx = (menu_wave_idx + 1) & 3;
                else if (key == '8') menu_wave_idx = (menu_wave_idx + 3) & 3;
                apply_wave_change();
                redraw_all();
                break;

            case MENU_DISTORTION:
            case MENU_FLANGER:
            case MENU_DELAY:
            case MENU_EQUALIZER: {
                int n = menus[current_menu].nrows;
                if      (key == 'C') selected_row = (selected_row + 1) % n;
                else if (key == '8') selected_row = (selected_row + n - 1) % n;
                redraw_all();
                break;
            }
            default: break;
        }
        break;

    // ---------------- EDIT_VALUE ----------------------------------------
    case ES_EDIT_VALUE: {
        const menu_desc_t *md = &menus[current_menu];
        if (selected_row < 0 || selected_row >= md->nrows) break;
        const row_desc_t *r = &md->rows[selected_row];

        if      (key == 'C') { edit_value_step(r, +1); draw_row(selected_row); shadow.values[selected_row] = row_get(r); }
        else if (key == '8') { edit_value_step(r, -1); draw_row(selected_row); shadow.values[selected_row] = row_get(r); }
        else if (key == '9') { edit_state = ES_EDIT; redraw_all(); }
        break;
    }
    }
}

void menu_tick(void) {
    // Note: Base-menu pot values are written by the 1ms timer IRQ, not
    // polled here.  menu_tick just redraws rows whose values have changed
    // since the last tick (the diff covers IRQ-driven changes too).

    if (!shadow.valid
        || shadow.menu   != current_menu
        || shadow.estate != edit_state)
    {
        redraw_all();
        return;
    }

    const menu_desc_t *md = &menus[current_menu];

    // Selection changed → repaint old + new rows.
    if (shadow.sel != selected_row) {
        if (shadow.sel >= 0 && shadow.sel < md->nrows) draw_row(shadow.sel);
        draw_row(selected_row);
        shadow.sel = selected_row;
    }

    // Value changes → repaint only the rows that moved.
    for (int i = 0; i < md->nrows; i++) {
        val_u now = row_get(&md->rows[i]);
        if (!row_eq(&md->rows[i], now, shadow.values[i])) {
            draw_row(i);
            shadow.values[i] = now;
            if (current_menu == MENU_WAVEFORM)
                draw_waveform(menu_wave_idx);
        }
    }
}

#endif // USE_TFT_MENU