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

static void write_fixture_vstpreset(const char *path, float vca_mode) {
    char xml[4096];
    int n = snprintf(
        xml, sizeof(xml),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?> "
        "<tal curprogram=\"0\" version=\"2.0\">"
        "<programs><program "
        "programname=\"VCA Mode Test\" "
        "sawvolume=\"0.85\" "
        "pulsevolume=\"0.0\" "
        "suboscvolume=\"0.0\" "
        "noisevolume=\"0.0\" "
        "filtercutoff=\"1.0\" "
        "filterresonance=\"0.0\" "
        "filterenvelopevalue=\"0.0\" "
        "filterenvelopevaluefullrange=\"0.0\" "
        "filterkeyboardvalue=\"0.5\" "
        "adsrattack=\"0.0\" "
        "adsrdecay=\"0.0\" "
        "adsrsustain=\"0.0\" "
        "adsrrelease=\"0.0\" "
        "adsrmode=\"1.0\" "
        "vcamode=\"%.6f\" "
        "volume=\"0.6\" "
        "masterfinetune=\"0.5\" "
        "octavetranspose=\"0.5\"/>"
        "</programs></tal>",
        (double)vca_mode
    );
    assert(n > 0 && (size_t)n < sizeof(xml));

    FILE *fp = fopen(path, "wb");
    assert(fp != NULL);
    fwrite("VST3\0\0", 1, 6, fp);
    fwrite(xml, 1, (size_t)n, fp);
    fwrite("\0tail", 1, 5, fp);
    fclose(fp);
}

static float render_held_tail_peak(plugin_api_v2_t *api, void *inst) {
    uint8_t on[3] = {0x90, 60, 110};
    uint8_t off[3] = {0x80, 60, 0};
    int16_t out[128 * 2];
    float tail_peak = 0.0f;

    api->on_midi(inst, on, 3, MOVE_MIDI_SOURCE_INTERNAL);
    for (int b = 0; b < 48; ++b) {
        api->render_block(inst, out, 128);
        if (b < 32) continue;
        for (int i = 0; i < 128 * 2; ++i) {
            float a = fabsf((float)out[i] / 32768.0f);
            if (a > tail_peak) tail_peak = a;
        }
    }
    api->on_midi(inst, off, 3, MOVE_MIDI_SOURCE_INTERNAL);
    api->render_block(inst, out, 128);
    return tail_peak;
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

    write_fixture_vstpreset("build/vca_gate_fixture.vstpreset", 0.0f);
    write_fixture_vstpreset("build/vca_env_fixture.vstpreset", 1.0f);

    api->set_param(inst, "import_vstpreset_path", "build/vca_gate_fixture.vstpreset");
    assert(get_int_param(api, inst, "vca_mode") == 0);
    float gate_tail = render_held_tail_peak(api, inst);

    api->set_param(inst, "all_notes_off", "1");
    api->set_param(inst, "import_vstpreset_path", "build/vca_env_fixture.vstpreset");
    assert(get_int_param(api, inst, "vca_mode") == 1);
    float env_tail = render_held_tail_peak(api, inst);

    assert(gate_tail > 0.03f);
    assert(gate_tail > env_tail * 3.0f + 0.01f);

    api->destroy_instance(inst);
    return 0;
}
