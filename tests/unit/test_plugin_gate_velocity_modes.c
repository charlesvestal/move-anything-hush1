#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/plugin_api_v1.h"

extern plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host);

static float fparam(plugin_api_v2_t *api, void *inst, const char *key) {
    char buf[128];
    int n = api->get_param(inst, key, buf, (int)sizeof(buf));
    assert(n >= 0);
    return strtof(buf, NULL);
}

static int iparam(plugin_api_v2_t *api, void *inst, const char *key) {
    char buf[128];
    int n = api->get_param(inst, key, buf, (int)sizeof(buf));
    assert(n >= 0);
    return atoi(buf);
}

static void note_on(plugin_api_v2_t *api, void *inst, int note, int vel) {
    uint8_t on[3] = {0x90, (uint8_t)note, (uint8_t)vel};
    api->on_midi(inst, on, 3, MOVE_MIDI_SOURCE_INTERNAL);
}

static void note_off(plugin_api_v2_t *api, void *inst, int note) {
    uint8_t off[3] = {0x80, (uint8_t)note, 0};
    api->on_midi(inst, off, 3, MOVE_MIDI_SOURCE_INTERNAL);
}

static void render_blocks(plugin_api_v2_t *api, void *inst, int blocks) {
    int16_t out[128 * 2];
    for (int i = 0; i < blocks; ++i) {
        api->render_block(inst, out, 128);
    }
}

static float render_peak(plugin_api_v2_t *api, void *inst, int note, int vel) {
    int16_t out[128 * 2];
    float peak = 0.0f;
    note_on(api, inst, note, vel);
    for (int b = 0; b < 6; ++b) {
        api->render_block(inst, out, 128);
        for (int i = 0; i < 128 * 2; ++i) {
            float v = fabsf((float)out[i] / 32768.0f);
            if (v > peak) peak = v;
        }
    }
    note_off(api, inst, note);
    render_blocks(api, inst, 2);
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

    /* Parameters required by SH-101 behavior model. */
    (void)iparam(api, inst, "gate_trig_mode");
    (void)iparam(api, inst, "velocity_mode");
    (void)iparam(api, inst, "portamento_mode");
    (void)iparam(api, inst, "same_note_quirk");

    /* Mode-dependent priority: GATE/LFO => low, GATE+TRIG => last. */
    api->set_param(inst, "all_notes_off", "1");
    api->set_param(inst, "gate_trig_mode", "0");
    note_on(api, inst, 60, 100);
    note_on(api, inst, 64, 100);
    assert(iparam(api, inst, "current_note") == 60);
    api->set_param(inst, "all_notes_off", "1");

    api->set_param(inst, "gate_trig_mode", "1");
    note_on(api, inst, 60, 100);
    note_on(api, inst, 64, 100);
    assert(iparam(api, inst, "current_note") == 64);
    api->set_param(inst, "all_notes_off", "1");

    api->set_param(inst, "gate_trig_mode", "2");
    note_on(api, inst, 60, 100);
    note_on(api, inst, 64, 100);
    assert(iparam(api, inst, "current_note") == 60);
    api->set_param(inst, "all_notes_off", "1");

    /* Velocity OFF => near-flat velocity response. */
    api->set_param(inst, "velocity_mode", "0");
    api->set_param(inst, "velocity_sens", "1.0");
    float p_lo = render_peak(api, inst, 60, 24);
    float p_hi = render_peak(api, inst, 60, 120);
    assert(p_hi > 0.0001f);
    assert((p_hi / p_lo) < 1.20f);

    /* Trigger-sampled velocity: gate mode should not update on legato note-ons. */
    api->set_param(inst, "all_notes_off", "1");
    api->set_param(inst, "reset_trigger_count", "1");
    api->set_param(inst, "velocity_mode", "1");
    api->set_param(inst, "gate_trig_mode", "0");
    note_on(api, inst, 60, 20);
    render_blocks(api, inst, 1);
    float v0 = fparam(api, inst, "active_velocity");
    note_on(api, inst, 64, 120);
    render_blocks(api, inst, 1);
    float v1 = fparam(api, inst, "active_velocity");
    assert(iparam(api, inst, "trigger_count") == 1);
    assert(fabsf(v1 - v0) < 0.05f);
    api->set_param(inst, "all_notes_off", "1");

    /* Trigger-sampled velocity: gate+trig updates per note-on. */
    api->set_param(inst, "reset_trigger_count", "1");
    api->set_param(inst, "gate_trig_mode", "1");
    note_on(api, inst, 60, 20);
    render_blocks(api, inst, 1);
    note_on(api, inst, 64, 120);
    render_blocks(api, inst, 1);
    assert(iparam(api, inst, "trigger_count") >= 2);
    assert(fparam(api, inst, "active_velocity") > 0.75f);
    api->set_param(inst, "all_notes_off", "1");

    /* Same-note quirk: repeated same pitch does not retrigger until pitch changes. */
    api->set_param(inst, "reset_trigger_count", "1");
    api->set_param(inst, "same_note_quirk", "1");
    api->set_param(inst, "gate_trig_mode", "1");
    note_on(api, inst, 60, 100);
    note_off(api, inst, 60);
    note_on(api, inst, 60, 100);
    assert(iparam(api, inst, "trigger_count") == 1);
    note_on(api, inst, 62, 100);
    note_off(api, inst, 62);
    note_on(api, inst, 60, 100);
    assert(iparam(api, inst, "trigger_count") >= 3);

    /* LFO mode: periodic retrigger while gate is held. */
    api->set_param(inst, "all_notes_off", "1");
    api->set_param(inst, "reset_trigger_count", "1");
    api->set_param(inst, "same_note_quirk", "0");
    api->set_param(inst, "gate_trig_mode", "2");
    api->set_param(inst, "lfo_rate", "12.0");
    note_on(api, inst, 60, 100);
    render_blocks(api, inst, 60); /* ~0.17 s */
    assert(iparam(api, inst, "trigger_count") >= 2);

    api->destroy_instance(inst);
    return 0;
}
