#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

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

static void get_string_param(plugin_api_v2_t *api, void *inst, const char *key, char *out, size_t out_len) {
    assert(api->get_param(inst, key, out, (int)out_len) >= 0);
}

static void ensure_dir(const char *path) {
    int rc = mkdir(path, 0755);
    (void)rc;
}

static void write_fixture_vstpreset(const char *path, const char *program_name, float saw) {
    char xml[4096];
    int n = snprintf(
        xml, sizeof(xml),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?> "
        "<tal curprogram=\"0\" version=\"2.0\">"
        "<programs><program "
        "programname=\"%s\" "
        "sawvolume=\"%.6f\" "
        "pulsevolume=\"0.2\" "
        "suboscvolume=\"0.1\" "
        "suboscmode=\"0.0\" "
        "noisevolume=\"0.0\" "
        "whitenoiseenabled=\"0.0\" "
        "dcopwmvalue=\"0.5\" "
        "dcopwmmode=\"0.0\" "
        "dcolfovalue=\"0.0\" "
        "dcorange=\"0.5\" "
        "filtercutoff=\"0.5\" "
        "filterresonance=\"0.2\" "
        "filterenvelopevalue=\"0.5\" "
        "filterenvelopevaluefullrange=\"0.0\" "
        "filterkeyboardvalue=\"0.5\" "
        "adsrattack=\"0.0\" "
        "adsrdecay=\"0.0\" "
        "adsrsustain=\"1.0\" "
        "adsrrelease=\"0.0\" "
        "adsrmode=\"0.0\" "
        "portamentointensity=\"0.0\" "
        "portamentomode=\"0.0\" "
        "lforate=\"0.0\" "
        "lfowaveform=\"0.0\" "
        "lfotrigger=\"0.0\" "
        "lfosync=\"0.0\" "
        "lfoinverted=\"0.0\" "
        "filtermodulationvalue=\"0.0\" "
        "controlvelocityvolume=\"0.0\" "
        "controlvelocityenvelope=\"0.0\" "
        "volume=\"0.7\" "
        "masterfinetune=\"0.5\" "
        "octavetranspose=\"0.5\"/>"
        "</programs></tal>",
        program_name,
        (double)saw
    );
    assert(n > 0 && (size_t)n < sizeof(xml));

    FILE *fp = fopen(path, "wb");
    assert(fp != NULL);
    fwrite("VST3\0\0", 1, 6, fp);
    fwrite(xml, 1, (size_t)n, fp);
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

    void *base = api->create_instance(".", NULL);
    assert(base != NULL);
    int base_count = get_int_param(api, base, "preset_count");
    api->destroy_instance(base);

    ensure_dir("build");
    ensure_dir("build/test_module");
    ensure_dir("build/test_module/presets");

    write_fixture_vstpreset("build/test_module/presets/A Drop 1.vstpreset", "Drop Test A", 0.11f);
    write_fixture_vstpreset("build/test_module/presets/B Drop 2.vstpreset", "Drop Test B", 0.91f);

    void *inst = api->create_instance("build/test_module", NULL);
    assert(inst != NULL);

    int count = get_int_param(api, inst, "preset_count");
    assert(count == base_count + 2);

    char name[128];
    char idx_buf[32];

    snprintf(idx_buf, sizeof(idx_buf), "%d", base_count);
    api->set_param(inst, "preset", idx_buf);
    get_string_param(api, inst, "preset_name", name, sizeof(name));
    assert(strcmp(name, "Drop Test A") == 0);
    assert(get_float_param(api, inst, "saw") < 0.2f);

    snprintf(idx_buf, sizeof(idx_buf), "%d", base_count + 1);
    api->set_param(inst, "preset", idx_buf);
    get_string_param(api, inst, "preset_name", name, sizeof(name));
    assert(strcmp(name, "Drop Test B") == 0);
    assert(get_float_param(api, inst, "saw") > 0.8f);

    api->destroy_instance(inst);
    return 0;
}
