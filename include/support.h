#define N 1000
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

typedef struct {
    int step;
    int offset;
    int active;
    char key;
} voice_t;

voice_t voices[MAX_VOICES];

extern short int wavetable[N];
extern const float base_freqs[];
extern int volume;

void init_wavetable(wave_t wave);
void set_note(int chan, note_t n, int octave);
float note_to_freq(note_t n, int octave);