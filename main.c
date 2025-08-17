/*
 * main.c - Paranormal Audio Research Console (PARC)
 *
 * A serious attempt to detect and analyze anomalous ultrasonic audio events.
 * This tool is designed to function like a piece of paranormal investigation
 * equipment, providing multiple analytical readouts.
 *
 * Version 2.0: Added a Rhythmic Pattern Analyzer to automatically detect
 * repeating sequences of short and long audio events.
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// --- FFT Implementation (by Takuya OOURA, public domain) ---
void cdft(int, int, double *, int *, double *);
// --- End of FFT Declaration ---

// --- Constants ---
#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768
#define WATERFALL_HEIGHT 450
#define MAX_LOG_ENTRIES 10
#define EVENT_HISTORY_SIZE 50
#define PATTERN_LENGTH 3
#define PANEL_TOP (WATERFALL_HEIGHT + 15)
#define LEFT_COL_X 15
#define MID_COL_X 355
#define RIGHT_COL_X 755
#define MID_SEP_X 340
#define RIGHT_SEP_X 730
#define LEFT_COL_WIDTH (MID_SEP_X - LEFT_COL_X - 10)
#define MID_COL_WIDTH (RIGHT_SEP_X - MID_COL_X - 10)
#define RIGHT_COL_WIDTH (SCREEN_WIDTH - RIGHT_COL_X - 10)

// Audio processing constants
#define SAMPLE_RATE 44100
#define FFT_SIZE 4096
#define MIN_FREQ_TO_DISPLAY 18000
#define MAX_FREQ_TO_DISPLAY 22000

// --- Structs and Enums ---
typedef enum { STATE_QUIET, STATE_BURST } BurstState;
typedef enum { EVENT_SILENCE, EVENT_BURST } EventType;
typedef enum { DURATION_SHORT, DURATION_LONG } EventDurationClass;

typedef struct {
    EventType type;
    EventDurationClass duration_class;
} ClassifiedEvent;

// --- Globals ---
SDL_Window* g_window = NULL;
SDL_Renderer* g_renderer = NULL;
SDL_AudioDeviceID g_audio_device_id;
TTF_Font* g_font_medium = NULL;
TTF_Font* g_font_small = NULL;

// Textures
SDL_Texture* g_waterfall_texture = NULL;
SDL_Texture* g_temp_texture = NULL;

// Audio & FFT
float g_audio_buffer[FFT_SIZE];
double g_fft_buffer[FFT_SIZE * 2];
double g_fft_magnitudes[FFT_SIZE / 2];
int g_audio_buffer_pos = 0;
SDL_atomic_t g_fft_ready;

// Analysis & State
float g_peak_freq = 0.0f;
float g_peak_mag = -100.0f;
BurstState g_burst_state = STATE_QUIET;
Uint32 g_burst_start_time = 0;
Uint32 g_quiet_start_time = 0;
char g_event_log[MAX_LOG_ENTRIES][100];
int g_event_log_pos = 0;

// Pattern Analysis
ClassifiedEvent g_event_history[EVENT_HISTORY_SIZE];
int g_event_history_count = 0;
float g_avg_burst_duration = 0;
float g_avg_silence_duration = 0;
int g_burst_count = 0;
int g_silence_count = 0;
ClassifiedEvent g_detected_pattern[PATTERN_LENGTH];
int g_pattern_reps = 0;

// Controls
float g_input_gain_db = 0.0f;
float g_burst_threshold_db = -40.0f;
int g_is_fullscreen = 1;

// FFT work arrays
int g_fft_ip[FFT_SIZE + 2];
double g_fft_w[FFT_SIZE * 5 / 4];

// --- Function Prototypes ---
int init();
void cleanup();
void audio_callback(void* userdata, Uint8* stream, int len);
void process_fft();
void run_main_loop();
void handle_input(SDL_Event* e, int* is_running);
void render(int has_new_data);
void render_text_clipped(const char* text, int x, int y, int max_width, TTF_Font* font, SDL_Color color);
void add_log_entry(const char* entry);
void add_classified_event(EventType type, float duration);
void analyze_patterns();

// --- FFT Function Prototypes ---
void makewt(int nw, int *ip, double *w);
void bitrv2(int n, int *ip, double *a);
void cftfsub(int n, double *a, double *w);
void cftbsub(int n, double *a, double *w);
void cft1st(int n, double *a, double *w);
void cftmdl(int n, int l, double *a, double *w);


// --- Main Function ---
int main(int argc, char* argv[]) {
    if (init() != 0) {
        cleanup();
        return 1;
    }
    run_main_loop();
    cleanup();
    return 0;
}

// --- Initialization and Cleanup ---

int init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0 || TTF_Init() == -1) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "SDL or TTF could not initialize!", NULL);
        return 1;
    }

    g_window = SDL_CreateWindow("Paranormal Audio Research Console", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
    if (!g_window || !g_renderer) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Window or Renderer could not be created!", NULL);
        return 1;
    }
    if (SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN) != 0) {
        SDL_Log("SDL_SetWindowFullscreen failed: %s", SDL_GetError());
        return 1;
    }
    g_is_fullscreen = 1;
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);

    g_font_medium = TTF_OpenFont("font.ttf", 18);
    g_font_small = TTF_OpenFont("font.ttf", 14);
    if (!g_font_medium || !g_font_small) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Font Error", "Failed to load 'font.ttf'", g_window);
        return 1;
    }

    g_waterfall_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_WIDTH, WATERFALL_HEIGHT);
    if (!g_waterfall_texture) {
        SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
        return 1;
    }
    g_temp_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_WIDTH, WATERFALL_HEIGHT);
    if (!g_temp_texture) {
        SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
        return 1;
    }
    if (SDL_SetRenderTarget(g_renderer, g_waterfall_texture) != 0) {
        SDL_Log("SDL_SetRenderTarget failed: %s", SDL_GetError());
        return 1;
    }
    if (SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255) != 0) {
        SDL_Log("SDL_SetRenderDrawColor failed: %s", SDL_GetError());
        return 1;
    }
    if (SDL_RenderClear(g_renderer) != 0) {
        SDL_Log("SDL_RenderClear failed: %s", SDL_GetError());
        return 1;
    }
    if (SDL_SetRenderTarget(g_renderer, NULL) != 0) {
        SDL_Log("SDL_SetRenderTarget failed: %s", SDL_GetError());
        return 1;
    }

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_callback;

    g_audio_device_id = SDL_OpenAudioDevice(NULL, 1, &want, &have, 0);
    if (g_audio_device_id == 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Audio Error", "Failed to open audio device!", g_window);
        return 1;
    }
    g_fft_ip[0] = 0;
    SDL_AtomicSet(&g_fft_ready, 0);
    g_quiet_start_time = SDL_GetTicks();
    add_log_entry("System online. Monitoring...");
    SDL_PauseAudioDevice(g_audio_device_id, 0);
    return 0;
}

void cleanup() {
    if (g_audio_device_id != 0) SDL_CloseAudioDevice(g_audio_device_id);
    if (g_font_medium) TTF_CloseFont(g_font_medium);
    if (g_font_small) TTF_CloseFont(g_font_small);
    if (g_waterfall_texture) SDL_DestroyTexture(g_waterfall_texture);
    if (g_temp_texture) SDL_DestroyTexture(g_temp_texture);
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window) SDL_DestroyWindow(g_window);
    TTF_Quit();
    SDL_Quit();
}

// --- Audio, FFT, and Analysis Logic ---

void audio_callback(void* userdata, Uint8* stream, int len) {
    Sint16* samples = (Sint16*)stream;
    int num_samples = len / sizeof(Sint16);
    float linear_gain = powf(10.0f, g_input_gain_db / 20.0f);

    for (int i = 0; i < num_samples; i++) {
        if (g_audio_buffer_pos < FFT_SIZE) {
            float sample_with_gain = (float)samples[i] * linear_gain;
            sample_with_gain = fmaxf(-32767.0f, fminf(32767.0f, sample_with_gain));
            g_audio_buffer[g_audio_buffer_pos++] = sample_with_gain / 32768.0f;
        }
        if (g_audio_buffer_pos >= FFT_SIZE) {
            if (SDL_AtomicGet(&g_fft_ready) == 0) {
                for (int j = 0; j < FFT_SIZE; j++) {
                    float hann_multiplier = 0.5f * (1.0f - cos(2.0f * M_PI * j / (FFT_SIZE - 1)));
                    g_fft_buffer[j * 2] = g_audio_buffer[j] * hann_multiplier;
                    g_fft_buffer[j * 2 + 1] = 0.0;
                }
                SDL_AtomicSet(&g_fft_ready, 1);
            }
            int overlap = FFT_SIZE / 2;
            memmove(g_audio_buffer, g_audio_buffer + overlap, (FFT_SIZE - overlap) * sizeof(float));
            g_audio_buffer_pos = FFT_SIZE - overlap;
        }
    }
}

void add_classified_event(EventType type, float duration) {
    // Shift history
    if (g_event_history_count >= EVENT_HISTORY_SIZE) {
        memmove(g_event_history, g_event_history + 1, (EVENT_HISTORY_SIZE - 1) * sizeof(ClassifiedEvent));
    } else {
        g_event_history_count++;
    }

    // Add new event
    ClassifiedEvent* new_event = &g_event_history[g_event_history_count - 1];
    new_event->type = type;

    // Classify duration and update average
    if (type == EVENT_BURST) {
        new_event->duration_class = (duration < g_avg_burst_duration) ? DURATION_SHORT : DURATION_LONG;
        g_avg_burst_duration = (g_avg_burst_duration * g_burst_count + duration) / (g_burst_count + 1);
        g_burst_count++;
    } else { // EVENT_SILENCE
        new_event->duration_class = (duration < g_avg_silence_duration) ? DURATION_SHORT : DURATION_LONG;
        g_avg_silence_duration = (g_avg_silence_duration * g_silence_count + duration) / (g_silence_count + 1);
        g_silence_count++;
    }
    
    analyze_patterns();
}

void analyze_patterns() {
    if (g_event_history_count < PATTERN_LENGTH) {
        g_pattern_reps = 0;
        return;
    }

    // Define the target pattern as the last N events
    ClassifiedEvent target_pattern[PATTERN_LENGTH];
    memcpy(target_pattern, &g_event_history[g_event_history_count - PATTERN_LENGTH], PATTERN_LENGTH * sizeof(ClassifiedEvent));
    
    int reps = 0;
    // Scan the rest of the history for this pattern
    for (int i = 0; i <= g_event_history_count - PATTERN_LENGTH; i++) {
        if (memcmp(target_pattern, &g_event_history[i], PATTERN_LENGTH * sizeof(ClassifiedEvent)) == 0) {
            reps++;
        }
    }

    // If this pattern is found more than once, log it
    if (reps > 1) {
        memcpy(g_detected_pattern, target_pattern, PATTERN_LENGTH * sizeof(ClassifiedEvent));
        g_pattern_reps = reps;
    } else {
        g_pattern_reps = 0;
    }
}


void process_fft() {
    cdft(FFT_SIZE * 2, -1, g_fft_buffer, g_fft_ip, g_fft_w);

    float bin_size_hz = (float)SAMPLE_RATE / FFT_SIZE;
    int min_bin = (int)(MIN_FREQ_TO_DISPLAY / bin_size_hz);
    int max_bin = (int)(MAX_FREQ_TO_DISPLAY / bin_size_hz);
    
    float current_total_energy = 0.0f;
    g_peak_mag = -200.0f;

    for (int i = min_bin; i <= max_bin; i++) {
        double real = g_fft_buffer[i * 2];
        double imag = g_fft_buffer[i * 2 + 1];
        g_fft_magnitudes[i] = 10 * log10(fmax(1e-12, real * real + imag * imag));
        current_total_energy += g_fft_magnitudes[i];
        if (g_fft_magnitudes[i] > g_peak_mag) {
            g_peak_mag = g_fft_magnitudes[i];
            g_peak_freq = i * bin_size_hz;
        }
    }
    
    float avg_energy = current_total_energy / (max_bin - min_bin + 1);

    Uint32 current_time = SDL_GetTicks();
    if (g_burst_state == STATE_QUIET && avg_energy > g_burst_threshold_db) {
        g_burst_state = STATE_BURST;
        float quiet_duration = (current_time - g_quiet_start_time) / 1000.0f;
        g_burst_start_time = current_time;
        add_classified_event(EVENT_SILENCE, quiet_duration);
        char log[100];
        snprintf(log, sizeof(log), "Silence: %.2fs", quiet_duration);
        add_log_entry(log);
    } else if (g_burst_state == STATE_BURST && avg_energy <= g_burst_threshold_db) {
        g_burst_state = STATE_QUIET;
        float burst_duration = (current_time - g_burst_start_time) / 1000.0f;
        g_quiet_start_time = current_time;
        add_classified_event(EVENT_BURST, burst_duration);
        char log[100];
        snprintf(log, sizeof(log), ">> BURST: %.2fs @ %.0f Hz", burst_duration, g_peak_freq);
        add_log_entry(log);
    }
}

// --- Main Loop and Rendering ---

void run_main_loop() {
    int is_running = 1;
    SDL_Event e;
    int new_data_available = 0;

    while (is_running) {
        while (SDL_PollEvent(&e) != 0) {
            handle_input(&e, &is_running);
        }

        if (SDL_AtomicGet(&g_fft_ready)) {
            process_fft();
            SDL_AtomicSet(&g_fft_ready, 0);
            new_data_available = 1;
        }

        render(new_data_available);
        new_data_available = 0;

        SDL_Delay(16);
    }
}

void handle_input(SDL_Event* e, int* is_running) {
    if (e->type == SDL_QUIT) *is_running = 0;
    if (e->type == SDL_KEYDOWN) {
        switch (e->key.keysym.sym) {
            case SDLK_ESCAPE:
                *is_running = 0;
                break;
            case SDLK_f:
            case SDLK_F11:
                if (g_is_fullscreen) {
                    if (SDL_SetWindowFullscreen(g_window, 0) != 0) {
                        SDL_Log("SDL_SetWindowFullscreen failed: %s", SDL_GetError());
                    } else {
                        g_is_fullscreen = 0;
                    }
                } else {
                    if (SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN) != 0) {
                        SDL_Log("SDL_SetWindowFullscreen failed: %s", SDL_GetError());
                    } else {
                        g_is_fullscreen = 1;
                    }
                }
                break;
            case SDLK_UP: g_input_gain_db = fminf(20.0f, g_input_gain_db + 1.0f); break;
            case SDLK_DOWN: g_input_gain_db = fmaxf(-20.0f, g_input_gain_db - 1.0f); break;
            case SDLK_RIGHT: g_burst_threshold_db = fminf(0.0f, g_burst_threshold_db + 1.0f); break;
            case SDLK_LEFT: g_burst_threshold_db = fmaxf(-80.0f, g_burst_threshold_db - 1.0f); break;
        }
    }
}

void add_log_entry(const char* entry) {
    if (!entry || !g_font_small) return;

    char text[256];
    strncpy(text, entry, sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';

    char* remaining = text;
    while (*remaining) {
        char line[100];
        int len = 0;
        int last_space = -1;
        int w = 0;

        while (remaining[len] && len < 99) {
            line[len] = remaining[len];
            line[len + 1] = '\0';
            TTF_SizeText(g_font_small, line, &w, NULL);
            if (w > RIGHT_COL_WIDTH) {
                if (last_space >= 0) {
                    line[last_space] = '\0';
                    len = last_space;
                } else {
                    line[len] = '\0';
                }
                break;
            }
            if (remaining[len] == ' ') last_space = len;
            len++;
        }
        line[len] = '\0';

        if (g_event_log_pos < MAX_LOG_ENTRIES) {
            strcpy(g_event_log[g_event_log_pos++], line);
        } else {
            for (int i = 0; i < MAX_LOG_ENTRIES - 1; i++) {
                strcpy(g_event_log[i], g_event_log[i + 1]);
            }
            strcpy(g_event_log[MAX_LOG_ENTRIES - 1], line);
        }

        remaining += len;
        while (*remaining == ' ') remaining++;
    }
}

void render_text_clipped(const char* text, int x, int y, int max_width, TTF_Font* font, SDL_Color color) {
    if (!text || !font) return;
    SDL_Surface* surface = TTF_RenderText_Blended(font, text, color);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(g_renderer, surface);
    if (!texture) {
        SDL_Log("SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
        SDL_FreeSurface(surface);
        return;
    }
    SDL_Rect dest = {x, y, surface->w, surface->h};
    SDL_Rect clip = {x, y, max_width, surface->h};
    if (SDL_RenderSetClipRect(g_renderer, &clip) != 0) {
        SDL_Log("SDL_RenderSetClipRect failed: %s", SDL_GetError());
    }
    if (SDL_RenderCopy(g_renderer, texture, NULL, &dest) != 0) {
        SDL_Log("SDL_RenderCopy failed: %s", SDL_GetError());
    }
    if (SDL_RenderSetClipRect(g_renderer, NULL) != 0) {
        SDL_Log("SDL_RenderSetClipRect failed: %s", SDL_GetError());
    }
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

void render(int has_new_data) {
    SDL_Color grid_color = {20, 50, 20, 255};
    SDL_Color text_color = {100, 255, 100, 255};
    SDL_Color highlight_color = {255, 255, 100, 255};

    if (has_new_data) {
        if (SDL_SetRenderTarget(g_renderer, g_temp_texture) != 0) {
            SDL_Log("SDL_SetRenderTarget failed: %s", SDL_GetError());
        }
        if (SDL_RenderCopy(g_renderer, g_waterfall_texture, NULL, NULL) != 0) {
            SDL_Log("SDL_RenderCopy failed: %s", SDL_GetError());
        }
        if (SDL_SetRenderTarget(g_renderer, g_waterfall_texture) != 0) {
            SDL_Log("SDL_SetRenderTarget failed: %s", SDL_GetError());
        }
        SDL_Rect dest = {0, 1, SCREEN_WIDTH, WATERFALL_HEIGHT - 1};
        if (SDL_RenderCopy(g_renderer, g_temp_texture, NULL, &dest) != 0) {
            SDL_Log("SDL_RenderCopy failed: %s", SDL_GetError());
        }

        for (int i = 0; i < SCREEN_WIDTH; i++) {
            float freq = MIN_FREQ_TO_DISPLAY + ((float)i / SCREEN_WIDTH) * (MAX_FREQ_TO_DISPLAY - MIN_FREQ_TO_DISPLAY);
            int bin_index = (int)(freq / ((float)SAMPLE_RATE / FFT_SIZE));
            float val = (g_fft_magnitudes[bin_index] + 80.0f) / 80.0f;
            val = fmaxf(0.0f, fminf(1.0f, val));
            SDL_SetRenderDrawColor(g_renderer, (Uint8)(val * 100), (Uint8)(val * 255), (Uint8)(val * 100), 255);
            if (SDL_RenderDrawPoint(g_renderer, i, 0) != 0) {
                SDL_Log("SDL_RenderDrawPoint failed: %s", SDL_GetError());
            }
        }
    }

    if (SDL_SetRenderTarget(g_renderer, NULL) != 0) {
        SDL_Log("SDL_SetRenderTarget failed: %s", SDL_GetError());
    }
    if (SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255) != 0) {
        SDL_Log("SDL_SetRenderDrawColor failed: %s", SDL_GetError());
    }
    if (SDL_RenderClear(g_renderer) != 0) {
        SDL_Log("SDL_RenderClear failed: %s", SDL_GetError());
    }

    if (SDL_RenderCopy(g_renderer, g_waterfall_texture, NULL, &(SDL_Rect){0, 0, SCREEN_WIDTH, WATERFALL_HEIGHT}) != 0) {
        SDL_Log("SDL_RenderCopy failed: %s", SDL_GetError());
    }

    if (SDL_SetRenderDrawColor(g_renderer, grid_color.r, grid_color.g, grid_color.b, 255) != 0) {
        SDL_Log("SDL_SetRenderDrawColor failed: %s", SDL_GetError());
    }
    for (int x = 0; x < SCREEN_WIDTH; x += 50) {
        if (SDL_RenderDrawLine(g_renderer, x, 0, x, WATERFALL_HEIGHT) != 0) {
            SDL_Log("SDL_RenderDrawLine failed: %s", SDL_GetError());
        }
    }
    for (int y = 0; y < WATERFALL_HEIGHT; y += 50) {
        if (SDL_RenderDrawLine(g_renderer, 0, y, SCREEN_WIDTH, y) != 0) {
            SDL_Log("SDL_RenderDrawLine failed: %s", SDL_GetError());
        }
    }

    SDL_Rect panel = {0, WATERFALL_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - WATERFALL_HEIGHT};
    if (SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255) != 0) {
        SDL_Log("SDL_SetRenderDrawColor failed: %s", SDL_GetError());
    }
    if (SDL_RenderFillRect(g_renderer, &panel) != 0) {
        SDL_Log("SDL_RenderFillRect failed: %s", SDL_GetError());
    }
    if (SDL_SetRenderDrawColor(g_renderer, highlight_color.r, highlight_color.g, highlight_color.b, 255) != 0) {
        SDL_Log("SDL_SetRenderDrawColor failed: %s", SDL_GetError());
    }
    if (SDL_RenderDrawLine(g_renderer, 0, WATERFALL_HEIGHT, SCREEN_WIDTH, WATERFALL_HEIGHT) != 0) {
        SDL_Log("SDL_RenderDrawLine failed: %s", SDL_GetError());
    }
    if (SDL_RenderDrawLine(g_renderer, MID_SEP_X, WATERFALL_HEIGHT, MID_SEP_X, SCREEN_HEIGHT) != 0) {
        SDL_Log("SDL_RenderDrawLine failed: %s", SDL_GetError());
    }
    if (SDL_RenderDrawLine(g_renderer, RIGHT_SEP_X, WATERFALL_HEIGHT, RIGHT_SEP_X, SCREEN_HEIGHT) != 0) {
        SDL_Log("SDL_RenderDrawLine failed: %s", SDL_GetError());
    }

    char buffer[100];
    int current_y;

    // Left Column
    render_text_clipped("STATUS & CONTROLS", LEFT_COL_X - 5, PANEL_TOP, LEFT_COL_WIDTH + 10, g_font_medium, highlight_color);
    snprintf(buffer, sizeof(buffer), "Input Gain: %+.1f dB (Up/Down)", g_input_gain_db);
    render_text_clipped(buffer, LEFT_COL_X, PANEL_TOP + 30, LEFT_COL_WIDTH, g_font_small, text_color);
    snprintf(buffer, sizeof(buffer), "Burst Threshold: %+.1f dB (Left/Right)", g_burst_threshold_db);
    render_text_clipped(buffer, LEFT_COL_X, PANEL_TOP + 50, LEFT_COL_WIDTH, g_font_small, text_color);
    if (g_burst_state == STATE_BURST) {
        render_text_clipped("STATE: BURST DETECTED", LEFT_COL_X, PANEL_TOP + 70, LEFT_COL_WIDTH, g_font_small, highlight_color);
    } else {
        render_text_clipped("STATE: Monitoring...", LEFT_COL_X, PANEL_TOP + 70, LEFT_COL_WIDTH, g_font_small, text_color);
    }

    // Middle Column
    current_y = PANEL_TOP;
    render_text_clipped("REAL-TIME ANALYSIS", MID_COL_X - 5, current_y, MID_COL_WIDTH + 10, g_font_medium, highlight_color);
    current_y += 30;
    snprintf(buffer, sizeof(buffer), "Peak Frequency: %.2f Hz", g_peak_freq);
    render_text_clipped(buffer, MID_COL_X, current_y, MID_COL_WIDTH, g_font_small, text_color);
    current_y += 20;
    snprintf(buffer, sizeof(buffer), "Peak Magnitude: %.2f dB", g_peak_mag);
    render_text_clipped(buffer, MID_COL_X, current_y, MID_COL_WIDTH, g_font_small, text_color);

    current_y += 40;
    render_text_clipped("PATTERN ANALYSIS", MID_COL_X - 5, current_y, MID_COL_WIDTH + 10, g_font_medium, highlight_color);
    current_y += 30;
    if (g_pattern_reps > 1) {
        char pattern_str[50] = "PATTERN: [";
        for (int i = 0; i < PATTERN_LENGTH; i++) {
            char event_char[5];
            snprintf(event_char, sizeof(event_char), "%c%c",
                g_detected_pattern[i].type == EVENT_BURST ? 'B' : 'S',
                g_detected_pattern[i].duration_class == DURATION_SHORT ? 's' : 'L');
            strcat(pattern_str, event_char);
            if (i < PATTERN_LENGTH - 1) strcat(pattern_str, " > ");
        }
        strcat(pattern_str, "]");
        snprintf(buffer, sizeof(buffer), "%s (x%d)", pattern_str, g_pattern_reps);
        render_text_clipped(buffer, MID_COL_X, current_y, MID_COL_WIDTH, g_font_small, highlight_color);
    } else {
        render_text_clipped("Searching for patterns...", MID_COL_X, current_y, MID_COL_WIDTH, g_font_small, text_color);
    }

    // Right Column
    render_text_clipped("EVENT LOG", RIGHT_COL_X - 5, PANEL_TOP, RIGHT_COL_WIDTH + 10, g_font_medium, highlight_color);
    for (int i = 0; i < g_event_log_pos; i++) {
        render_text_clipped(g_event_log[i], RIGHT_COL_X, PANEL_TOP + 30 + (i * 20), RIGHT_COL_WIDTH, g_font_small, text_color);
    }

    SDL_RenderPresent(g_renderer);
}


/*
   Fast Fourier/Cosine/Sine Transform
   (C) 1996-2001 Takuya OOURA
   URL: http://www.kurims.kyoto-u.ac.jp/~ooura/fft.html
   This software is public domain.
*/

void cdft(int n, int isgn, double *a, int *ip, double *w)
{
    if (n > (ip[0] << 2)) {
        makewt(n >> 2, ip, w);
    }
    if (isgn >= 0) {
        cftfsub(n, a, w);
    } else {
        cftbsub(n, a, w);
    }
}

void makewt(int nw, int *ip, double *w)
{
    int j, nwh;
    double delta, x, y;
    ip[0] = nw;
    ip[1] = 1;
    if (nw > 2) {
        nwh = nw >> 1;
        delta = atan(1.0) / nwh;
        w[0] = 1;
        w[1] = 0;
        w[nwh] = cos(delta * nwh);
        w[nwh + 1] = w[nwh];
        if (nwh > 2) {
            for (j = 2; j < nwh; j += 2) {
                x = cos(delta * j);
                y = sin(delta * j);
                w[j] = x;
                w[j + 1] = y;
                w[nw - j] = y;
                w[nw - j + 1] = x;
            }
            bitrv2(nw, ip + 2, w);
        }
    }
}

void bitrv2(int n, int *ip, double *a)
{
    int j, j1, k, k1, l, m, m2;
    double xr, xi;
    ip[0] = 0;
    l = n;
    m = 1;
    while ((m << 3) < l) {
        l >>= 1;
        m <<= 1;
    }
    m2 = m << 1;
    if ((m << 3) == l) {
        for (j = 0; j < m; j++) {
            j1 = j << 2;
            k = ip[j] << 1;
            k1 = k + m2;
            xr = a[j1];
            xi = a[j1 + 1];
            a[j1] = a[k];
            a[j1 + 1] = a[k + 1];
            a[k] = xr;
            a[k + 1] = xi;
            j1 += m2;
            k1 += m2;
            xr = a[j1];
            xi = a[j1 + 1];
            a[j1] = a[k1];
            a[j1 + 1] = a[k1 + 1];
            a[k1] = xr;
            a[k1 + 1] = xi;
        }
    } else {
        for (j = 0; j < m; j++) {
            k = ip[j] << 1;
            j1 = j << 1;
            xr = a[j1];
            xi = a[j1 + 1];
            a[j1] = a[k];
            a[j1 + 1] = a[k + 1];
            a[k] = xr;
            a[k + 1] = xi;
        }
    }
}

void cftfsub(int n, double *a, double *w)
{
    int j, j1, j2, j3, l;
    double x0r, x0i, x1r, x1i, x2r, x2i, x3r, x3i;
    l = 2;
    if (n > 8) {
        cft1st(n, a, w);
        l = 8;
        while ((l << 2) < n) {
            cftmdl(n, l, a, w);
            l <<= 2;
        }
    }
    if ((l << 2) == n) {
        for (j = 0; j < l; j += 2) {
            j1 = j + l;
            j2 = j1 + l;
            j3 = j2 + l;
            x0r = a[j] + a[j1];
            x0i = a[j + 1] + a[j1 + 1];
            x1r = a[j] - a[j1];
            x1i = a[j + 1] - a[j1 + 1];
            x2r = a[j2] + a[j3];
            x2i = a[j2 + 1] + a[j3 + 1];
            x3r = a[j2] - a[j3];
            x3i = a[j2 + 1] - a[j3 + 1];
            a[j] = x0r + x2r;
            a[j + 1] = x0i + x2i;
            a[j2] = x0r - x2r;
            a[j2 + 1] = x0i - x2i;
            a[j1] = x1r - x3i;
            a[j1 + 1] = x1i + x3r;
            a[j3] = x1r + x3i;
            a[j3 + 1] = x1i - x3r;
        }
    } else {
        for (j = 0; j < l; j += 2) {
            j1 = j + l;
            x0r = a[j] - a[j1];
            x0i = a[j + 1] - a[j1 + 1];
            a[j] += a[j1];
            a[j + 1] += a[j1 + 1];
            a[j1] = x0r;
            a[j1 + 1] = x0i;
        }
    }
}

void cftbsub(int n, double *a, double *w)
{
    int j, j1, j2, j3, l;
    double x0r, x0i, x1r, x1i, x2r, x2i, x3r, x3i;
    l = 2;
    if (n > 8) {
        cft1st(n, a, w);
        l = 8;
        while ((l << 2) < n) {
            cftmdl(n, l, a, w);
            l <<= 2;
        }
    }
    if ((l << 2) == n) {
        for (j = 0; j < l; j += 2) {
            j1 = j + l;
            j2 = j1 + l;
            j3 = j2 + l;
            x0r = a[j] + a[j2];
            x0i = a[j + 1] + a[j2 + 1];
            x1r = a[j] - a[j2];
            x1i = a[j + 1] - a[j2 + 1];
            x2r = a[j1] + a[j3];
            x2i = a[j1 + 1] + a[j3 + 1];
            x3r = a[j1] - a[j3];
            x3i = a[j1 + 1] - a[j3 + 1];
            a[j] = x0r + x2r;
            a[j + 1] = x0i + x2i;
            a[j1] = x1r + x3i;
            a[j1 + 1] = x1i - x3r;
            x0r = x0r - x2r;
            x0i = x0i - x2i;
            x1r = x1r - x3i;
            x1i = x1i + x3r;
            a[j2] = x0r;
            a[j2 + 1] = x0i;
            a[j3] = x1r;
            a[j3 + 1] = x1i;
        }
    } else {
        for (j = 0; j < l; j += 2) {
            j1 = j + l;
            x0r = a[j] - a[j1];
            x0i = a[j + 1] - a[j1 + 1];
            a[j] += a[j1];
            a[j + 1] += a[j1 + 1];
            a[j1] = x0r;
            a[j1 + 1] = x0i;
        }
    }
}

void cft1st(int n, double *a, double *w)
{
    int j, k1, k2;
    double wk1r, wk1i, wk2r, wk2i, wk3r, wk3i;
    double x0r, x0i, x1r, x1i, x2r, x2i, x3r, x3i;
    x0r = a[0] + a[2];
    x0i = a[1] + a[3];
    x1r = a[0] - a[2];
    x1i = a[1] - a[3];
    x2r = a[4] + a[6];
    x2i = a[5] + a[7];
    x3r = a[4] - a[6];
    x3i = a[5] - a[7];
    a[0] = x0r + x2r;
    a[1] = x0i + x2i;
    a[4] = x0r - x2r;
    a[5] = x0i - x2i;
    a[2] = x1r - x3i;
    a[3] = x1i + x3r;
    a[6] = x1r + x3i;
    a[7] = x1i - x3r;
    wk1r = w[2];
    x0r = a[8] + a[10];
    x0i = a[9] + a[11];
    x1r = a[8] - a[10];
    x1i = a[9] - a[11];
    x2r = a[12] + a[14];
    x2i = a[13] + a[15];
    x3r = a[12] - a[14];
    x3i = a[13] - a[15];
    a[8] = x0r + x2r;
    a[9] = x0i + x2i;
    a[12] = x0r - x2r;
    a[13] = x0i - x2i;
    a[10] = x1r - x3i;
    a[11] = x1i + x3r;
    a[14] = x1r + x3i;
    a[15] = x1i - x3r;
    k1 = 0;
    for (j = 16; j < n; j += 16) {
        k1 += 4;
        k2 = k1 << 1;
        wk2r = w[k1];
        wk2i = w[k1 + 1];
        wk1r = w[k2];
        wk1i = w[k2 + 1];
        wk3r = wk1r - 2 * wk2i * wk1i;
        wk3i = 2 * wk2i * wk1r - wk1i;
        x0r = a[j] + a[j + 2];
        x0i = a[j + 1] + a[j + 3];
        x1r = a[j] - a[j + 2];
        x1i = a[j + 1] - a[j + 3];
        x2r = a[j + 4] + a[j + 6];
        x2i = a[j + 5] + a[j + 7];
        x3r = a[j + 4] - a[j + 6];
        x3i = a[j + 5] - a[j + 7];
        a[j] = x0r + x2r;
        a[j + 1] = x0i + x2i;
        a[j + 4] = x0r - x2r;
        a[j + 5] = x0i - x2i;
        a[j + 2] = x1r - x3i;
        a[j + 3] = x1i + x3r;
        a[j + 6] = x1r + x3i;
        a[j + 7] = x1i - x3r;
        x0r = a[j + 8] + a[j + 10];
        x0i = a[j + 9] + a[j + 11];
        x1r = a[j + 8] - a[j + 10];
        x1i = a[j + 9] - a[j + 11];
        x2r = a[j + 12] + a[j + 14];
        x2i = a[j + 13] + a[j + 15];
        x3r = a[j + 12] - a[j + 14];
        x3i = a[j + 13] - a[j + 15];
        a[j + 8] = x0r + x2r;
        a[j + 9] = x0i + x2i;
        a[j + 12] = x0r - x2r;
        a[j + 13] = x0i - x2i;
        a[j + 10] = x1r - x3i;
        a[j + 11] = x1i + x3r;
        a[j + 14] = x1r + x3i;
        a[j + 15] = x1i - x3r;
        x0r = a[j + 8];
        x0i = a[j + 9];
        x1r = a[j + 10];
        x1i = a[j + 11];
        x2r = a[j + 12];
        x2i = a[j + 13];
        x3r = a[j + 14];
        x3i = a[j + 15];
        a[j + 8] = x0r * wk2r - x0i * wk2i;
        a[j + 9] = x0r * wk2i + x0i * wk2r;
        a[j + 10] = x1r * wk1r - x1i * wk1i;
        a[j + 11] = x1r * wk1i + x1i * wk1r;
        a[j + 12] = x2r * wk2r + x2i * wk2i;
        a[j + 13] = x2r * wk2i - x2i * wk2r;
        a[j + 14] = x3r * wk3r - x3i * wk3i;
        a[j + 15] = x3r * wk3i + x3i * wk3r;
    }
}

void cftmdl(int n, int l, double *a, double *w)
{
    int j, j1, j2, k, k1, k2, m, m2;
    double wk1r, wk1i, wk2r, wk2i, wk3r, wk3i;
    double x0r, x0i, x1r, x1i, x2r, x2i, x3r, x3i;
    m = l << 2;
    for (j = 0; j < l; j += 2) {
        j1 = j + l;
        j2 = j1 + l;
        k = j + m;
        x0r = a[j] + a[j1];
        x0i = a[j + 1] + a[j1 + 1];
        x1r = a[j] - a[j1];
        x1i = a[j + 1] - a[j1 + 1];
        x2r = a[j2] + a[k];
        x2i = a[j2 + 1] + a[k + 1];
        x3r = a[j2] - a[k];
        x3i = a[j2 + 1] - a[k + 1];
        a[j] = x0r + x2r;
        a[j + 1] = x0i + x2i;
        a[j2] = x0r - x2r;
        a[j2 + 1] = x0i - x2i;
        a[j1] = x1r - x3i;
        a[j1 + 1] = x1i + x3r;
        a[k] = x1r + x3i;
        a[k + 1] = x1i - x3r;
    }
    k1 = 1;
    m2 = l;
    for (k = m; k < n; k += m) {
        k1++;
        k2 = k1 << 1;
        wk2r = w[k1];
        wk2i = w[k1 + 1];
        wk1r = w[k2];
        wk1i = w[k2 + 1];
        wk3r = wk1r - 2 * wk2i * wk1i;
        wk3i = 2 * wk2i * wk1r - wk1i;
        for (j = k; j < l + k; j += 2) {
            j1 = j + l;
            j2 = j1 + l;
            m2 = j2 + l;
            x0r = a[j] + a[j1];
            x0i = a[j + 1] + a[j1 + 1];
            x1r = a[j] - a[j1];
            x1i = a[j + 1] - a[j1 + 1];
            x2r = a[j2] + a[m2];
            x2i = a[j2 + 1] + a[m2 + 1];
            x3r = a[j2] - a[m2];
            x3i = a[j2 + 1] - a[m2 + 1];
            a[j] = x0r + x2r;
            a[j + 1] = x0i + x2i;
            a[j2] = x0r - x2r;
            a[j2 + 1] = x0i - x2i;
            a[j1] = x1r - x3i;
            a[j1 + 1] = x1i + x3r;
            a[m2] = x1r + x3i;
            a[m2 + 1] = x1i - x3r;
            x0r = a[j];
            x0i = a[j + 1];
            x1r = a[j1];
            x1i = a[j1 + 1];
            x2r = a[j2];
            x2i = a[j2 + 1];
            x3r = a[m2];
            x3i = a[m2 + 1];
            a[j] = x0r * wk2r - x0i * wk2i;
            a[j + 1] = x0r * wk2i + x0i * wk2r;
            a[j1] = x1r * wk1r - x1i * wk1i;
            a[j1 + 1] = x1r * wk1i + x1i * wk1r;
            a[j2] = x2r * wk2r + x2i * wk2i;
            a[j2 + 1] = x2r * wk2i - x2i * wk2r;
            a[m2] = x3r * wk3r - x3i * wk3i;
            a[m2 + 1] = x3r * wk3i + x3i * wk3r;
        }
    }
}
