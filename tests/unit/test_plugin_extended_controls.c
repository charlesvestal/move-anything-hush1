#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/plugin_api_v1.h"

extern plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host);

static int get_int_param(plugin_api_v2_t *api, void *inst, const char *key) {
    char buf[128];
    assert(api->get_param(inst, key, buf, (int)sizeof(buf)) >= 0);
    return atoi(buf);
}

static float get_float_param(plugin_api_v2_t *api, void *inst, const char *key) {
    char buf[128];
    assert(api->get_param(inst, key, buf, (int)sizeof(buf)) >= 0);
    return strtof(buf, NULL);
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

    api->set_param(inst, "sub_mode", "2");
    assert(get_int_param(api, inst, "sub_mode") == 2);

    api->set_param(inst, "white_noise", "1");
    assert(get_int_param(api, inst, "white_noise") == 1);

    api->set_param(inst, "pwm_mode", "0");
    assert(get_int_param(api, inst, "pwm_mode") == 0);

    api->set_param(inst, "pwm_env_depth", "0.6");
    assert(fabsf(get_float_param(api, inst, "pwm_env_depth") - 0.6f) < 0.01f);

    api->set_param(inst, "fine_tune", "25");
    assert(fabsf(get_float_param(api, inst, "fine_tune") - 25.0f) < 0.01f);

    api->set_param(inst, "lfo_waveform", "2");
    assert(get_int_param(api, inst, "lfo_waveform") == 2);

    api->set_param(inst, "lfo_trigger", "1");
    assert(get_int_param(api, inst, "lfo_trigger") == 1);

    api->set_param(inst, "lfo_sync", "1");
    assert(get_int_param(api, inst, "lfo_sync") == 1);

    api->set_param(inst, "lfo_invert", "1");
    assert(get_int_param(api, inst, "lfo_invert") == 1);

    api->set_param(inst, "lfo_pitch_snap", "1");
    assert(get_int_param(api, inst, "lfo_pitch_snap") == 1);

    api->set_param(inst, "filter_env_full_range", "1");
    assert(get_int_param(api, inst, "filter_env_full_range") == 1);

    api->set_param(inst, "filter_env_polarity", "1");
    assert(get_int_param(api, inst, "filter_env_polarity") == 1);

    api->set_param(inst, "filter_volume_correction", "0.9");
    assert(get_float_param(api, inst, "filter_volume_correction") > 0.85f);

    api->set_param(inst, "vca_mode", "0");
    assert(get_int_param(api, inst, "vca_mode") == 0);

    api->set_param(inst, "portamento_linear", "1");
    assert(get_int_param(api, inst, "portamento_linear") == 1);

    api->set_param(inst, "adsr_declick", "0.75");
    assert(get_float_param(api, inst, "adsr_declick") > 0.74f);

    /* Clamps should hold. */
    api->set_param(inst, "sub_mode", "99");
    assert(get_int_param(api, inst, "sub_mode") == 2);

    api->set_param(inst, "pwm_mode", "99");
    assert(get_int_param(api, inst, "pwm_mode") == 2);

    api->set_param(inst, "fine_tune", "-400");
    assert(get_float_param(api, inst, "fine_tune") <= -99.9f);

    api->destroy_instance(inst);
    return 0;
}
