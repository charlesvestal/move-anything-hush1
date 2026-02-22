#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/plugin_api_v1.h"

extern plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host);

static float get_float_param(plugin_api_v2_t *api, void *inst, const char *key) {
    char buf[128];
    if (api->get_param(inst, key, buf, (int)sizeof(buf)) < 0) return -1.0f;
    return strtof(buf, NULL);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: diagnose_sh101_preset <preset.vstpreset>\n");
        return 2;
    }
    const char *path = argv[1];

    host_api_v1_t host;
    memset(&host, 0, sizeof(host));
    host.api_version = MOVE_PLUGIN_API_VERSION;
    host.sample_rate = 44100;
    host.frames_per_block = 128;

    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    if (!api) { fprintf(stderr, "failed to init\n"); return 1; }
    void *inst = api->create_instance(".", NULL);
    if (!inst) { fprintf(stderr, "failed to create\n"); return 1; }
    api->set_param(inst, "import_vstpreset_path", path);

    printf("preset: %s\n", path);
    printf("params: saw=%.3f pulse=%.3f sub=%.3f noise=%.3f cutoff=%.3f res=%.3f\n",
           get_float_param(api, inst, "saw"),
           get_float_param(api, inst, "pulse"),
           get_float_param(api, inst, "sub"),
           get_float_param(api, inst, "noise"),
           get_float_param(api, inst, "cutoff"),
           get_float_param(api, inst, "resonance"));
    printf("params: attack=%.4f decay=%.4f sustain=%.3f release=%.4f vca_mode=%d gate_trig=%d\n",
           get_float_param(api, inst, "attack"),
           get_float_param(api, inst, "decay"),
           get_float_param(api, inst, "sustain"),
           get_float_param(api, inst, "release"),
           (int)get_float_param(api, inst, "vca_mode"),
           (int)get_float_param(api, inst, "gate_trig_mode"));
    printf("params: env_amt=%.3f volume=%.3f lfo_filter=%.3f lfo_pitch=%.3f\n",
           get_float_param(api, inst, "env_amt"),
           get_float_param(api, inst, "volume"),
           get_float_param(api, inst, "lfo_filter"),
           get_float_param(api, inst, "lfo_pitch"));

    uint8_t on[3] = {0x90, 60, 110};
    uint8_t off[3] = {0x80, 60, 0};
    int16_t out[128 * 2];
    float mono[64 * 128];
    int idx = 0;

    api->on_midi(inst, on, 3, MOVE_MIDI_SOURCE_INTERNAL);

    printf("per_block_rms:\n");
    for (int b = 0; b < 64; ++b) {
        api->render_block(inst, out, 128);
        float sumSq = 0;
        for (int i = 0; i < 128; ++i) {
            float s = (float)out[i * 2] / 32768.0f;
            mono[idx++] = s;
            sumSq += s * s;
        }
        float rms = sqrtf(sumSq / 128.0f);
        const char *marker = (b >= 48) ? " *MEAS*" : "";
        printf("  block %2d: rms=%.6f%s\n", b, rms, marker);
    }

    api->on_midi(inst, off, 3, MOVE_MIDI_SOURCE_INTERNAL);

    /* Measurement window analysis */
    int start = 48 * 128;
    int end = 64 * 128;
    float peak = 0, absmean = 0;
    int zc = 0;
    for (int i = start; i < end; ++i) {
        if (fabsf(mono[i]) > peak) peak = fabsf(mono[i]);
        absmean += fabsf(mono[i]);
        if (i > start) {
            float a = mono[i-1], b2 = mono[i];
            if ((a >= 0 && b2 < 0) || (a < 0 && b2 >= 0)) zc++;
        }
    }
    absmean /= (float)(end - start);

    float bestAC = 0;
    int bestLag = 0;
    for (int lag = 40; lag <= 500; ++lag) {
        double acc = 0, e1 = 0, e2 = 0;
        for (int i = start + lag; i < end; ++i) {
            double x = mono[i], y = mono[i - lag];
            acc += x * y;
            e1 += x * x;
            e2 += y * y;
        }
        if (e1 > 1e-9 && e2 > 1e-9) {
            float r = (float)(acc / sqrt(e1 * e2));
            if (r > bestAC) { bestAC = r; bestLag = lag; }
        }
    }

    printf("measurement: peak=%.6f absmean=%.6f zc_rate=%.6f autocorr=%.6f lag=%d\n",
           peak, absmean, (float)zc / (float)(end - start - 1), bestAC, bestLag);

    api->destroy_instance(inst);
    return 0;
}
