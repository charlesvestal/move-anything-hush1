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

static void write_fixture_vstpreset(const char *path) {
    const char *xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?> "
        "<tal curprogram=\"0\" version=\"2.0\">"
        "<programs><program "
        "programname=\"Import Test\" "
        "sawvolume=\"0.7\" "
        "pulsevolume=\"0.3\" "
        "suboscvolume=\"0.2\" "
        "suboscmode=\"0.9\" "
        "noisevolume=\"0.1\" "
        "whitenoiseenabled=\"1.0\" "
        "dcopwmvalue=\"1.0\" "
        "dcopwmmode=\"0.5\" "
        "dcolfovalue=\"0.6\" "
        "dcorange=\"0.75\" "
        "filtercutoff=\"0.2\" "
        "filterresonance=\"0.4\" "
        "filterenvelopevalue=\"0.9\" "
        "filterenvelopevaluefullrange=\"1.0\" "
        "filterkeyboardvalue=\"0.8\" "
        "adsrattack=\"0.0\" "
        "adsrdecay=\"0.212\" "
        "adsrsustain=\"0.5\" "
        "adsrrelease=\"0.25\" "
        "adsrmode=\"0.95\" "
        "vcamode=\"0.0\" "
        "adsrdecklick=\"0.8\" "
        "portamentointensity=\"0.5\" "
        "portamentomode=\"0.45\" "
        "portamentolinear=\"1.0\" "
        "lforate=\"0.5\" "
        "lfowaveform=\"0.8\" "
        "dcolfovaluesnap=\"1.0\" "
        "lfotrigger=\"1.0\" "
        "lfosync=\"1.0\" "
        "lfoinverted=\"1.0\" "
        "filtermodulationvalue=\"0.25\" "
        "filtervolumecorrection=\"0.9\" "
        "controlvelocityvolume=\"0.75\" "
        "controlvelocityenvelope=\"0.33\" "
        "volume=\"0.62\" "
        "masterfinetune=\"0.625\" "
        "octavetranspose=\"0.5\"/>"
        "</programs></tal>";

    FILE *fp = fopen(path, "wb");
    assert(fp != NULL);
    fwrite("VST3\0\0", 1, 6, fp);
    fwrite(xml, 1, strlen(xml), fp);
    fwrite("\0tail", 1, 5, fp);
    fclose(fp);
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

    const char *fixture = "build/import_fixture.vstpreset";
    write_fixture_vstpreset(fixture);
    api->set_param(inst, "import_vstpreset_path", fixture);

    assert(fabsf(get_float_param(api, inst, "saw") - 0.7f) < 0.01f);
    assert(get_float_param(api, inst, "cutoff") > 0.4f);
    assert(get_float_param(api, inst, "decay") > 0.15f);
    assert(get_int_param(api, inst, "sub_mode") == 2);
    assert(get_int_param(api, inst, "white_noise") == 1);
    assert(get_int_param(api, inst, "lfo_waveform") == 2);
    assert(get_int_param(api, inst, "lfo_sync") == 1);
    assert(get_int_param(api, inst, "gate_trig_mode") == 1);
    assert(get_int_param(api, inst, "vca_mode") == 0);
    assert(get_int_param(api, inst, "lfo_pitch_snap") == 1);
    assert(get_int_param(api, inst, "portamento_linear") == 1);
    assert(get_float_param(api, inst, "filter_volume_correction") > 0.85f);
    assert(get_float_param(api, inst, "adsr_declick") > 0.75f);
    assert(fabsf(get_float_param(api, inst, "fine_tune") - 25.0f) < 0.2f);

    api->destroy_instance(inst);
    return 0;
}
