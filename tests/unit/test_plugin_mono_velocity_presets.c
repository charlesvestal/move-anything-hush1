#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/plugin_api_v1.h"

extern plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host);

static float render_peak(plugin_api_v2_t *api, void *inst, int midi_note, int velocity) {
    int16_t out[128 * 2];
    uint8_t on[3] = {0x90, (uint8_t)midi_note, (uint8_t)velocity};
    uint8_t off[3] = {0x80, (uint8_t)midi_note, 0};
    float peak = 0.0f;

    api->on_midi(inst, on, 3, MOVE_MIDI_SOURCE_INTERNAL);
    for (int b = 0; b < 8; ++b) {
        api->render_block(inst, out, 128);
        for (int i = 0; i < 128 * 2; ++i) {
            float v = fabsf((float)out[i] / 32768.0f);
            if (v > peak) peak = v;
        }
    }
    api->on_midi(inst, off, 3, MOVE_MIDI_SOURCE_INTERNAL);
    for (int b = 0; b < 2; ++b) {
        api->render_block(inst, out, 128);
    }
    return peak;
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

    char buf[128];

    /* Preset system should expose the full 10-category target bank. */
    assert(api->get_param(inst, "preset_count", buf, (int)sizeof(buf)) >= 0);
    int preset_count = atoi(buf);
    assert(preset_count >= 10);

    api->set_param(inst, "preset", "0");
    assert(api->get_param(inst, "preset_name", buf, (int)sizeof(buf)) >= 0);
    assert(strcmp(buf, "Classic 101 Sub Bass") == 0);

    api->set_param(inst, "preset", "9");
    assert(api->get_param(inst, "preset_name", buf, (int)sizeof(buf)) >= 0);
    assert(strcmp(buf, "Random Filter Burble") == 0);

    /* Velocity mode OFF should flatten velocity response regardless of sensitivity. */
    api->set_param(inst, "velocity_mode", "0");
    api->set_param(inst, "velocity_sens", "1.0");
    float off_lo = render_peak(api, inst, 60, 24);
    float off_hi = render_peak(api, inst, 60, 120);
    assert(off_hi > 0.0001f);
    assert((off_hi / off_lo) < 1.2f);

    /* Trigger-sampled velocity mode should respond to sensitivity and note velocity. */
    api->set_param(inst, "velocity_mode", "1");
    api->set_param(inst, "velocity_sens", "0.0");
    float flat_lo = render_peak(api, inst, 60, 24);
    float flat_hi = render_peak(api, inst, 60, 120);
    assert(flat_hi > 0.0001f);
    assert((flat_hi / flat_lo) < 1.2f);

    /* Velocity sensitivity = 1 should produce stronger velocity scaling. */
    api->set_param(inst, "velocity_sens", "1.0");
    float dyn_lo = render_peak(api, inst, 60, 24);
    float dyn_hi = render_peak(api, inst, 60, 120);
    assert((dyn_hi / dyn_lo) > 1.45f);

    api->destroy_instance(inst);
    return 0;
}
