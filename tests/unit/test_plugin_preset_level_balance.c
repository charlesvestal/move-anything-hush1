#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/plugin_api_v1.h"

extern plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host);

static float render_rms(plugin_api_v2_t *api, void *inst, int midi_note, int velocity) {
    int16_t out[128 * 2];
    uint8_t on[3] = {0x90, (uint8_t)midi_note, (uint8_t)velocity};
    uint8_t off[3] = {0x80, (uint8_t)midi_note, 0};
    double sum_sq = 0.0;
    int count = 0;

    api->on_midi(inst, on, 3, MOVE_MIDI_SOURCE_INTERNAL);
    for (int b = 0; b < 12; ++b) {
        api->render_block(inst, out, 128);
        for (int i = 0; i < 128 * 2; ++i) {
            float v = (float)out[i] / 32768.0f;
            sum_sq += (double)v * (double)v;
            count += 1;
        }
    }
    api->on_midi(inst, off, 3, MOVE_MIDI_SOURCE_INTERNAL);
    for (int b = 0; b < 2; ++b) {
        api->render_block(inst, out, 128);
    }

    if (count <= 0) return 0.0f;
    return (float)sqrt(sum_sq / (double)count);
}

int main(void) {
    host_api_v1_t host;
    memset(&host, 0, sizeof(host));
    host.api_version = MOVE_PLUGIN_API_VERSION;
    host.sample_rate = 44100;
    host.frames_per_block = 128;

    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api != NULL);

    void *inst = api->create_instance(".", NULL);
    assert(inst != NULL);

    char buf[64];
    assert(api->get_param(inst, "preset_count", buf, (int)sizeof(buf)) >= 0);
    int preset_count = atoi(buf);
    assert(preset_count >= 10);

    api->set_param(inst, "velocity_mode", "0");

    float min_rms = 10.0f;
    float max_rms = 0.0f;
    for (int p = 0; p < preset_count; ++p) {
        char pbuf[32];
        snprintf(pbuf, sizeof(pbuf), "%d", p);
        api->set_param(inst, "preset", pbuf);
        api->set_param(inst, "all_notes_off", "1");

        float rms = render_rms(api, inst, 48, 100);
        if (rms < min_rms) min_rms = rms;
        if (rms > max_rms) max_rms = rms;
    }

    assert(min_rms > 0.005f);
    /* Keep preset jumps moderate: <= ~13.4 dB spread on fixed note render. */
    assert((max_rms / min_rms) < 4.7f);

    api->destroy_instance(inst);
    return 0;
}
