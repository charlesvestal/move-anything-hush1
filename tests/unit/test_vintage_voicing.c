#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "sh101_filter.h"
#include "sh101_osc.h"

static float oscillator_peak(void) {
    sh101_osc_t osc;
    sh101_osc_init(&osc, 44100.0f, 1u);
    float peak = 0.0f;
    for (int i = 0; i < 50000; ++i) {
        float s = sh101_osc_render(&osc, 110.0f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 0.0f);
        float a = fabsf(s);
        if (a > peak) peak = a;
    }
    return peak;
}

static float noise_lag1_corr(void) {
    sh101_osc_t osc;
    sh101_osc_init(&osc, 44100.0f, 1234u);

    double sxy = 0.0;
    double sx2 = 0.0;
    float prev = 0.0f;
    for (int i = 0; i < 60000; ++i) {
        float n = sh101_osc_render(&osc, 220.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0, 0.0f);
        if (i > 0) {
            sxy += (double)n * (double)prev;
            sx2 += (double)n * (double)n;
        }
        prev = n;
    }
    return (sx2 > 0.0) ? (float)(sxy / sx2) : 0.0f;
}

static float filter_rms_for_res(float res) {
    sh101_filter_t f;
    sh101_filter_init(&f, 44100.0f);
    sh101_filter_set_params(&f, 400.0f, res, 1.3f);

    float phase = 0.0f;
    float inc = 55.0f / 44100.0f;
    double sum = 0.0;
    int count = 0;

    for (int i = 0; i < 30000; ++i) {
        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
        float in = sinf(phase * 6.28318530718f);
        float out = sh101_filter_process(&f, in);
        if (i > 5000) {
            sum += (double)out * (double)out;
            count += 1;
        }
    }
    return (count > 0) ? (float)sqrt(sum / (double)count) : 0.0f;
}

int main(void) {
    float peak = oscillator_peak();
    float corr = noise_lag1_corr();
    float rms_lo = filter_rms_for_res(0.1f);
    float rms_hi = filter_rms_for_res(1.05f);

    /* Vintage voicing: mixed oscillator should soft-limit before extreme clipping. */
    assert(peak < 2.1f);

    /* Slightly colored noise should show positive short-lag correlation. */
    assert(corr > 0.03f);

    /* Resonance should cause a clear low-end/level drop. */
    assert((rms_hi / rms_lo) < 0.78f);

    return 0;
}
