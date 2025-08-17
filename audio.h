#ifndef AUDIO_H
#define AUDIO_H

#include <SDL.h>

#define SAMPLE_RATE 44100
#define FFT_SIZE 4096
#define MIN_FREQ_TO_DISPLAY 18000
#define MAX_FREQ_TO_DISPLAY 22000
#define MAX_LOG_ENTRIES 10
#define EVENT_HISTORY_SIZE 50
#define PATTERN_LENGTH 3

typedef enum { STATE_QUIET, STATE_BURST } BurstState;
typedef enum { EVENT_SILENCE, EVENT_BURST } EventType;
typedef enum { DURATION_SHORT, DURATION_LONG } EventDurationClass;

typedef struct {
    EventType type;
    EventDurationClass duration_class;
} ClassifiedEvent;

extern float g_audio_buffer[FFT_SIZE];
extern int g_audio_buffer_pos;
extern float g_input_gain_db;
extern float g_burst_threshold_db;
extern BurstState g_burst_state;
extern Uint32 g_burst_start_time;
extern Uint32 g_quiet_start_time;

void audio_callback(void* userdata, Uint8* stream, int len);
void add_classified_event(EventType type, float duration);

#endif
