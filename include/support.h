#define N 2048
#define MAX_VOICES 12
#define RATE 20000
#define FLANGER_BUF 1024
#define DELAY_BUF_SIZE 12000

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
bool distortion_enabled;
float distortion_volume;
float distortion;

//eq settings
float lows;
float mids;
float highs;

//flanger settings
bool flanger_enabled;
float flanger_depth;
float flanger_rate;
float flanger_feedback;
float flanger_mix;

//delay settings
bool delay_enabled;
float delay_time;
float delay_mix;
float delay_feedback;

void init_asdr(float attack, float decay, float sustain, float release);

extern short int wavetable[N];
extern const float base_freqs[];
extern int volume;

void init_wavetable(wave_t wave);
void set_note(int chan, note_t n, int octave, bool);
float note_to_freq(note_t n, int octave);

void init_distortion(bool enable, float dist, float dist_volume);
float apply_distortion(float x);

void init_eq(float l, float m, float h);
float apply_eq(float x);

void init_flanger(bool enabled, float depth, float rate, float feedback, float mix);
float apply_flanger(float x);

void init_delay(bool enabled, float time, float mix, float feedback);
float apply_delay(float x);