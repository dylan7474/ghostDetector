#ifndef FFT_H
#define FFT_H

#include "audio.h"

extern double g_fft_buffer[FFT_SIZE * 2];
extern double g_fft_magnitudes[FFT_SIZE / 2];
extern volatile int g_fft_ready;
extern int g_fft_ip[FFT_SIZE + 2];
extern double g_fft_w[FFT_SIZE * 5 / 4];
extern float g_peak_freq;
extern float g_peak_mag;

void process_fft();

#endif
