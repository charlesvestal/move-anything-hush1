#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sh101_filter.h"
#include "sh101_osc.h"

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <mode> <out.rawf32>\\n", argv[0]);
        return 1;
    }

    const char *mode = argv[1];
    const char *out_path = argv[2];

    const int frames = 44100;
    float *buf = (float*)calloc((size_t)frames, sizeof(float));
    if (!buf) return 1;

    sh101_osc_t osc;
    sh101_osc_init(&osc, 44100.0f, 1u);

    if (strcmp(mode, "saw_A3") == 0) {
        for (int i = 0; i < frames; ++i) {
            buf[i] = sh101_osc_render(&osc, 220.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f);
        }
    } else if (strcmp(mode, "cutoff_0p35_res_0p1") == 0 || strcmp(mode, "cutoff_0p35_res_0p85") == 0) {
        sh101_filter_t f;
        sh101_filter_init(&f, 44100.0f);
        float res = (strcmp(mode, "cutoff_0p35_res_0p85") == 0) ? 0.85f : 0.1f;
        sh101_filter_set_params(&f, 30.0f + 0.35f * 0.35f * 15000.0f, res, 1.3f);
        for (int i = 0; i < frames; ++i) {
            /* Broadband input highlights resonance behavior near cutoff. */
            float x = sh101_osc_render(&osc, 220.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0, 0.0f);
            buf[i] = sh101_filter_process(&f, x);
        }
    } else {
        fprintf(stderr, "unknown mode: %s\\n", mode);
        free(buf);
        return 2;
    }

    FILE *fp = fopen(out_path, "wb");
    if (!fp) {
        free(buf);
        return 3;
    }
    fwrite(buf, sizeof(float), (size_t)frames, fp);
    fclose(fp);
    free(buf);
    return 0;
}
