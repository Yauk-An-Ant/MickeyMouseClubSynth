#define N 2048
#define MAX_VOICES 12
#define RATE 20000

typedef enum {
    SINE,
    TRIANGLE,
    SAWTOOTH,
    SQUARE
} wave_t;

typedef enum {
    C,
    CS,
    D,
    DS,
    E,
    F,
    FS,
    G,
    GS,
    A,
    AS,
    B
} note_t;

typedef enum {
    IDLE,
    ATTACK,
    DECAY,
    SUSTAIN,
    RELEASE
} envelope_state_t;

typedef struct {
    int step;
    int offset;
    int active;
    char key;

    //asdr things
    envelope_state_t envelope_state;
    float envelope_level;
} voice_t;

voice_t voices[MAX_VOICES];

//everything 0 to 1

//asdr settings
float attack_time;
float decay_time;
float sustain_level;
float release_time;

float attack_inc;
float decay_dec;
float release_dec;

//distortion settings
float distortion_volume;
float distortion;

//eq settings
float lows;
float mids;
float highs;

// --- Flanger ---
int   flanger_on;       // 0 = bypass, 1 = active
float flanger_depth;    // 0..1
float flanger_rate;    // 0..1  (scale to your LFO Hz range)
float flanger_feedback;    // 0..1
float flanger_mix;    // 0..1  (dry/wet)

// --- Delay ---
int   delay_on;       // 0 = bypass, 1 = active
float delay_time;    // 0..1  (scale to your max delay in ms)
float delay_mix;    // 0..1  (dry/wet)
float delay_feedback;    // 0..1

void init_asdr(float attack, float decay, float sustain, float release);

extern short int wavetable[N];
extern const float base_freqs[];
extern int volume;

void init_wavetable(wave_t wave);
void set_note(int chan, note_t n, int octave);
float note_to_freq(note_t n, int octave);