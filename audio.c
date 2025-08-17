#include "audio.h"
#include "fft.h"
#include <math.h>
#include <string.h>

float g_audio_buffer[FFT_SIZE];
int g_audio_buffer_pos = 0;

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
            if (!g_fft_ready) {
                for (int j = 0; j < FFT_SIZE; j++) {
                    float hann_multiplier = 0.5f * (1.0f - cos(2.0f * M_PI * j / (FFT_SIZE - 1)));
                    g_fft_buffer[j * 2] = g_audio_buffer[j] * hann_multiplier;
                    g_fft_buffer[j * 2 + 1] = 0.0;
                }
                g_fft_ready = 1;
            }
            int overlap = FFT_SIZE / 2;
            memmove(g_audio_buffer, g_audio_buffer + overlap, (FFT_SIZE - overlap) * sizeof(float));
            g_audio_buffer_pos = FFT_SIZE - overlap;
        }
    }
}
