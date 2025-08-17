#include "render.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static char g_event_log[MAX_LOG_ENTRIES][100];
static int g_event_log_pos = 0;

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
                    SDL_SetWindowFullscreen(g_window, 0);
                    g_is_fullscreen = 0;
                } else {
                    SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN);
                    g_is_fullscreen = 1;
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
    SDL_Rect dest = {x, y, surface->w, surface->h};
    SDL_Rect clip = {x, y, max_width, surface->h};
    SDL_RenderSetClipRect(g_renderer, &clip);
    SDL_RenderCopy(g_renderer, texture, NULL, &dest);
    SDL_RenderSetClipRect(g_renderer, NULL);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

void render(int has_new_data) {
    SDL_Color grid_color = {20, 50, 20, 255};
    SDL_Color text_color = {100, 255, 100, 255};
    SDL_Color highlight_color = {255, 255, 100, 255};

    if (has_new_data) {
        SDL_SetRenderTarget(g_renderer, g_temp_texture);
        SDL_RenderCopy(g_renderer, g_waterfall_texture, NULL, NULL);
        SDL_SetRenderTarget(g_renderer, g_waterfall_texture);
        SDL_Rect dest = {0, 1, SCREEN_WIDTH, WATERFALL_HEIGHT - 1};
        SDL_RenderCopy(g_renderer, g_temp_texture, NULL, &dest);

        for (int i = 0; i < SCREEN_WIDTH; i++) {
            float freq = MIN_FREQ_TO_DISPLAY + ((float)i / SCREEN_WIDTH) * (MAX_FREQ_TO_DISPLAY - MIN_FREQ_TO_DISPLAY);
            int bin_index = (int)(freq / ((float)SAMPLE_RATE / FFT_SIZE));
            float val = (g_fft_magnitudes[bin_index] + 80.0f) / 80.0f;
            val = fmaxf(0.0f, fminf(1.0f, val));
            SDL_SetRenderDrawColor(g_renderer, (Uint8)(val * 100), (Uint8)(val * 255), (Uint8)(val * 100), 255);
            SDL_RenderDrawPoint(g_renderer, i, 0);
        }
    }

    SDL_SetRenderTarget(g_renderer, NULL);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);

    SDL_RenderCopy(g_renderer, g_waterfall_texture, NULL, &(SDL_Rect){0, 0, SCREEN_WIDTH, WATERFALL_HEIGHT});

    SDL_SetRenderDrawColor(g_renderer, grid_color.r, grid_color.g, grid_color.b, 255);
    for (int x = 0; x < SCREEN_WIDTH; x += 50) SDL_RenderDrawLine(g_renderer, x, 0, x, WATERFALL_HEIGHT);
    for (int y = 0; y < WATERFALL_HEIGHT; y += 50) SDL_RenderDrawLine(g_renderer, 0, y, SCREEN_WIDTH, y);

    SDL_Rect panel = {0, WATERFALL_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - WATERFALL_HEIGHT};
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(g_renderer, &panel);
    SDL_SetRenderDrawColor(g_renderer, highlight_color.r, highlight_color.g, highlight_color.b, 255);
    SDL_RenderDrawLine(g_renderer, 0, WATERFALL_HEIGHT, SCREEN_WIDTH, WATERFALL_HEIGHT);
    SDL_RenderDrawLine(g_renderer, MID_SEP_X, WATERFALL_HEIGHT, MID_SEP_X, SCREEN_HEIGHT);
    SDL_RenderDrawLine(g_renderer, RIGHT_SEP_X, WATERFALL_HEIGHT, RIGHT_SEP_X, SCREEN_HEIGHT);

    char buffer[100];
    int current_y;

    // Left Column
    render_text_clipped("STATUS & CONTROLS", LEFT_COL_X - 5, PANEL_TOP, LEFT_COL_WIDTH + 10, g_font_medium, highlight_color);
    sprintf(buffer, "Input Gain: %+.1f dB (Up/Down)", g_input_gain_db);
    render_text_clipped(buffer, LEFT_COL_X, PANEL_TOP + 30, LEFT_COL_WIDTH, g_font_small, text_color);
    sprintf(buffer, "Burst Threshold: %+.1f dB (Left/Right)", g_burst_threshold_db);
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
    sprintf(buffer, "Peak Frequency: %.2f Hz", g_peak_freq);
    render_text_clipped(buffer, MID_COL_X, current_y, MID_COL_WIDTH, g_font_small, text_color);
    current_y += 20;
    sprintf(buffer, "Peak Magnitude: %.2f dB", g_peak_mag);
    render_text_clipped(buffer, MID_COL_X, current_y, MID_COL_WIDTH, g_font_small, text_color);

    current_y += 40;
    render_text_clipped("PATTERN ANALYSIS", MID_COL_X - 5, current_y, MID_COL_WIDTH + 10, g_font_medium, highlight_color);
    current_y += 30;
    if (g_pattern_reps > 1) {
        char pattern_str[50] = "PATTERN: [";
        for (int i = 0; i < PATTERN_LENGTH; i++) {
            char event_char[5];
            sprintf(event_char, "%c%c",
                g_detected_pattern[i].type == EVENT_BURST ? 'B' : 'S',
                g_detected_pattern[i].duration_class == DURATION_SHORT ? 's' : 'L');
            strcat(pattern_str, event_char);
            if (i < PATTERN_LENGTH - 1) strcat(pattern_str, " > ");
        }
        strcat(pattern_str, "]");
        sprintf(buffer, "%s (x%d)", pattern_str, g_pattern_reps);
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
