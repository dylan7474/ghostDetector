#include "fft.h"
#include "audio.h"
#include "render.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static void cdft(int n, int isgn, double *a, int *ip, double *w);
static void makewt(int nw, int *ip, double *w);
static void bitrv2(int n, int *ip, double *a);
static void cftfsub(int n, double *a, double *w);
static void cftbsub(int n, double *a, double *w);
static void cft1st(int n, double *a, double *w);
static void cftmdl(int n, int l, double *a, double *w);

double g_fft_buffer[FFT_SIZE * 2];
double g_fft_magnitudes[FFT_SIZE / 2];
volatile int g_fft_ready = 0;
int g_fft_ip[FFT_SIZE + 2];
double g_fft_w[FFT_SIZE * 5 / 4];
float g_peak_freq = 0.0f;
float g_peak_mag = -100.0f;

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
        sprintf(log, "Silence: %.2fs", quiet_duration);
        add_log_entry(log);
    } else if (g_burst_state == STATE_BURST && avg_energy <= g_burst_threshold_db) {
        g_burst_state = STATE_QUIET;
        float burst_duration = (current_time - g_burst_start_time) / 1000.0f;
        g_quiet_start_time = current_time;
        add_classified_event(EVENT_BURST, burst_duration);
        char log[100];
        sprintf(log, ">> BURST: %.2fs @ %.0f Hz", burst_duration, g_peak_freq);
        add_log_entry(log);
    }
}

/*
   Fast Fourier/Cosine/Sine Transform
   (C) 1996-2001 Takuya OOURA
   URL: http://www.kurims.kyoto-u.ac.jp/~ooura/fft.html
   This software is public domain.
*/

static void cdft(int n, int isgn, double *a, int *ip, double *w)
{
    if (n > (ip[0] << 2)) {
        makewt(n >> 2, ip, w);
    }
    bitrv2(n, ip + 2, a);
    if (isgn < 0) {
        cftfsub(n, a, w);
    } else {
        cftbsub(n, a, w);
    }
}

static void makewt(int nw, int *ip, double *w)
{
    int j, nwh, nw0, nw1;
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
        for (j = 2; j < nwh; j += 2) {
            x = cos(delta * j);
            y = sin(delta * j);
            w[j] = x;
            w[j + 1] = y;
            w[nw - j] = y;
            w[nw - j + 1] = x;
        }
        nw0 = 0;
        while ((nwh >>= 1) > 1) {
            nw1 = nw0 + nwh;
            for (j = nw0; j < nw1; j += 2) {
                w[nw + j] = w[j];
                w[nw + j + 1] = w[j + 1];
            }
            nw0 = nw1;
        }
    }
}

static void bitrv2(int n, int *ip, double *a)
{
    int j, j1, k, k1, l, m, m2;
    double xr, xi;
    m = n >> 1;
    l = 0;
    k = 1;
    while (k < m) {
        for (j = 0; j < k; j++) {
            j1 = j + k;
            xr = a[j1];
            xi = a[j1 + m];
            a[j1] = a[j] - xr;
            a[j1 + m] = a[j + m] - xi;
            a[j] += xr;
            a[j + m] += xi;
        }
        l = k;
        k <<= 1;
    }
    m2 = m;
    while ((m2 >>= 1) > 0) {
        k = 0;
        for (j = 0; j < n; j += m2 << 1) {
            for (k1 = j; k1 < j + m2; k1++) {
                k = ip[k >> 1];
            }
        }
    }
}

static void cftfsub(int n, double *a, double *w)
{
    int j, j1, j2, j3, k, l, m, n2;
    double x0r, x0i, x1r, x1i, x2r, x2i, x3r, x3i;
    n2 = n >> 1;
    m = 1;
    while ((m <<= 1) < n) {
        l = m << 1;
        for (j = 0; j < m; j++) {
            w[j] = w[j * 2];
            w[j + m] = w[j * 2 + 1];
        }
        for (j = 0; j < n; j += l) {
            for (k = j; k < j + m; k++) {
                j1 = k + m;
                x0r = a[k] + a[j1];
                x0i = a[k + n2] + a[j1 + n2];
                x1r = a[k] - a[j1];
                x1i = a[k + n2] - a[j1 + n2];
                a[k] = x0r;
                a[k + n2] = x0i;
                a[j1] = x1r;
                a[j1 + n2] = x1i;
            }
        }
        m = l;
    }
}

static void cftbsub(int n, double *a, double *w)
{
    int j, j1, j2, j3, k, l, m, n2;
    double x0r, x0i, x1r, x1i, x2r, x2i, x3r, x3i;
    n2 = n >> 1;
    m = 1;
    while ((m <<= 1) < n) {
        l = m << 1;
        for (j = 0; j < m; j++) {
            w[j] = w[j * 2];
            w[j + m] = w[j * 2 + 1];
        }
        for (j = 0; j < n; j += l) {
            for (k = j; k < j + m; k++) {
                j1 = k + m;
                x0r = a[k] - a[j1];
                x0i = a[k + n2] - a[j1 + n2];
                x1r = a[k] + a[j1];
                x1i = a[k + n2] + a[j1 + n2];
                a[k] = x0r;
                a[k + n2] = x0i;
                a[j1] = x1r;
                a[j1 + n2] = x1i;
            }
        }
        m = l;
    }
}

static void cft1st(int n, double *a, double *w)
{
    int j, k1, k2;
    double wk1r, x0r, x0i, x1r, x1i, x2r, x2i, x3r, x3i;
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
    if (n > 8) {
        k1 = 0;
        for (j = 16; j < n; j += 16) {
            k1 += 2;
            k2 = k1 << 1;
            wk1r = w[k1];
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
            a[j + 8] = x0r * wk1r - x0i * w[k1 + 1];
            a[j + 9] = x0r * w[k1 + 1] + x0i * wk1r;
            a[j + 10] = x1r * w[k2] - x1i * w[k2 + 1];
            a[j + 11] = x1r * w[k2 + 1] + x1i * w[k2];
            a[j + 12] = x2r * wk1r + x2i * w[k1 + 1];
            a[j + 13] = x2r * w[k1 + 1] - x2i * wk1r;
            a[j + 14] = x3r * w[k2] - x3i * w[k2 + 1];
            a[j + 15] = x3r * w[k2 + 1] + x3i * w[k2];
        }
    }
}

static void cftmdl(int n, int l, double *a, double *w)
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
