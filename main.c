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

#include "audio.h"
#include "fft.h"
#include "render.h"

// --- Globals ---
SDL_Window* g_window = NULL;
SDL_Renderer* g_renderer = NULL;
SDL_AudioDeviceID g_audio_device_id;
TTF_Font* g_font_medium = NULL;
TTF_Font* g_font_small = NULL;

// Textures
SDL_Texture* g_waterfall_texture = NULL;
SDL_Texture* g_temp_texture = NULL;

// Analysis & State
BurstState g_burst_state = STATE_QUIET;
Uint32 g_burst_start_time = 0;
Uint32 g_quiet_start_time = 0;

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



// --- Function Prototypes ---
int init();
void cleanup();
void run_main_loop();
void add_classified_event(EventType type, float duration);
void analyze_patterns();


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
    SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN);
    g_is_fullscreen = 1;
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);

    g_font_medium = TTF_OpenFont("font.ttf", 18);
    g_font_small = TTF_OpenFont("font.ttf", 14);
    if (!g_font_medium || !g_font_small) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Font Error", "Failed to load 'font.ttf'", g_window);
        return 1;
    }

    g_waterfall_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_WIDTH, WATERFALL_HEIGHT);
    g_temp_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SCREEN_WIDTH, WATERFALL_HEIGHT);
    SDL_SetRenderTarget(g_renderer, g_waterfall_texture);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_SetRenderTarget(g_renderer, NULL);

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

// --- Main Loop and Rendering ---

void run_main_loop() {
    int is_running = 1;
    SDL_Event e;
    int new_data_available = 0;

    while (is_running) {
        while (SDL_PollEvent(&e) != 0) {
            handle_input(&e, &is_running);
        }

        if (g_fft_ready) {
            process_fft();
            g_fft_ready = 0;
            new_data_available = 1;
        }

        render(new_data_available);
        new_data_available = 0;

        SDL_Delay(16);
    }
}
