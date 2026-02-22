#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/plugin_api_v1.h"

extern plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host);

static int get_int_param(plugin_api_v2_t *api, void *inst, const char *key) {
    char buf[128];
    if (api->get_param(inst, key, buf, (int)sizeof(buf)) < 0) return -1;
    return atoi(buf);
}

static float get_float_param(plugin_api_v2_t *api, void *inst, const char *key) {
    char buf[128];
    if (api->get_param(inst, key, buf, (int)sizeof(buf)) < 0) return -1.0f;
    return strtof(buf, NULL);
}

static void get_string_param(plugin_api_v2_t *api, void *inst, const char *key, char *out, size_t out_len) {
    if (api->get_param(inst, key, out, (int)out_len) < 0) {
        snprintf(out, out_len, "");
    }
}

int main(int argc, char **argv) {
    const char *path;
    host_api_v1_t host;
    plugin_api_v2_t *api;
    void *inst;
    uint8_t on[3] = {0x90, 60, 110};
    uint8_t off[3] = {0x80, 60, 0};
    int16_t out[128 * 2];
    float mono[64 * 128];
    int idx = 0;
    float peak = 0.0f;
    float absmean = 0.0f;
    int zc = 0;
    float best = 0.0f;
    int best_lag = 0;
    int start = 48 * 128;
    int end = 64 * 128;
    char name[128];

    if (argc < 2) {
        fprintf(stderr, "usage: measure_sh101_preset <preset.vstpreset>\n");
        return 2;
    }
    path = argv[1];

    memset(&host, 0, sizeof(host));
    host.api_version = MOVE_PLUGIN_API_VERSION;
    host.sample_rate = 44100;
    host.frames_per_block = 128;

    api = move_plugin_init_v2(&host);
    if (!api) {
        fprintf(stderr, "failed to init plugin api\n");
        return 1;
    }
    inst = api->create_instance(".", NULL);
    if (!inst) {
        fprintf(stderr, "failed to create instance\n");
        return 1;
    }
    api->set_param(inst, "import_vstpreset_path", path);

    api->on_midi(inst, on, 3, MOVE_MIDI_SOURCE_INTERNAL);
    for (int b = 0; b < 64; ++b) {
        api->render_block(inst, out, 128);
        for (int i = 0; i < 128; ++i) {
            float s = (float)out[i * 2] / 32768.0f;
            if (fabsf(s) > peak) peak = fabsf(s);
            mono[idx++] = s;
        }
    }
    api->on_midi(inst, off, 3, MOVE_MIDI_SOURCE_INTERNAL);
    api->render_block(inst, out, 128);

    for (int i = start + 1; i < end; ++i) {
        float a = mono[i - 1];
        float b = mono[i];
        if ((a >= 0.0f && b < 0.0f) || (a < 0.0f && b >= 0.0f)) zc += 1;
        absmean += fabsf(b);
    }
    absmean /= (float)(end - start - 1);

    for (int lag = 40; lag <= 500; ++lag) {
        double acc = 0.0;
        double e1 = 0.0;
        double e2 = 0.0;
        for (int i = start + lag; i < end; ++i) {
            float x = mono[i];
            float y = mono[i - lag];
            acc += (double)x * (double)y;
            e1 += (double)x * (double)x;
            e2 += (double)y * (double)y;
        }
        if (e1 > 1e-9 && e2 > 1e-9) {
            float r = (float)(acc / sqrt(e1 * e2));
            if (r > best) {
                best = r;
                best_lag = lag;
            }
        }
    }

    get_string_param(api, inst, "import_name", name, sizeof(name));
    printf("{\"name\":\"%s\",\"peak\":%.6f,\"absmean\":%.6f,\"zc_rate\":%.6f,\"autocorr\":%.6f,\"lag\":%d,"
           "\"cutoff\":%.6f,\"resonance\":%.6f,\"gate_trig_mode\":%d,\"vca_mode\":%d}\n",
           name,
           peak,
           absmean,
           (float)zc / (float)(end - start - 1),
           best,
           best_lag,
           get_float_param(api, inst, "cutoff"),
           get_float_param(api, inst, "resonance"),
           get_int_param(api, inst, "gate_trig_mode"),
           get_int_param(api, inst, "vca_mode"));

    api->destroy_instance(inst);
    return 0;
}
