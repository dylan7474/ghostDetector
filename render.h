#ifndef RENDER_H
#define RENDER_H

#include <SDL.h>
#include <SDL_ttf.h>
#include "audio.h"
#include "fft.h"

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768
#define WATERFALL_HEIGHT 450
#define PANEL_TOP (WATERFALL_HEIGHT + 15)
#define LEFT_COL_X 15
#define MID_COL_X 355
#define RIGHT_COL_X 755
#define MID_SEP_X 340
#define RIGHT_SEP_X 730
#define LEFT_COL_WIDTH (MID_SEP_X - LEFT_COL_X - 10)
#define MID_COL_WIDTH (RIGHT_SEP_X - MID_COL_X - 10)
#define RIGHT_COL_WIDTH (SCREEN_WIDTH - RIGHT_COL_X - 10)

extern SDL_Window* g_window;
extern SDL_Renderer* g_renderer;
extern TTF_Font* g_font_medium;
extern TTF_Font* g_font_small;
extern SDL_Texture* g_waterfall_texture;
extern SDL_Texture* g_temp_texture;
extern int g_is_fullscreen;
extern ClassifiedEvent g_detected_pattern[PATTERN_LENGTH];
extern int g_pattern_reps;

void handle_input(SDL_Event* e, int* is_running);
void render(int has_new_data);
void render_text_clipped(const char* text, int x, int y, int max_width, TTF_Font* font, SDL_Color color);
void add_log_entry(const char* entry);

#endif
