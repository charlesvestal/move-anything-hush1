#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/plugin_api_v1.h"

extern plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host);

typedef struct {
    float peak;
    float zc_rate;
    float autocorr;
} render_metrics_t;

static void write_fixture(const char *path, const char *program_attrs) {
    char xml[8192];
    int n = snprintf(
        xml, sizeof(xml),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?> "
        "<tal curprogram=\"0\" version=\"2.0\">"
        "<programs><program %s/>"
        "</programs></tal>",
        program_attrs
    );
    assert(n > 0 && (size_t)n < sizeof(xml));

    FILE *fp = fopen(path, "wb");
    assert(fp != NULL);
    fwrite("VST3\0\0", 1, 6, fp);
    fwrite(xml, 1, (size_t)n, fp);
    fwrite("\0tail", 1, 5, fp);
    fclose(fp);
}

static render_metrics_t render_metrics(plugin_api_v2_t *api, void *inst) {
    uint8_t on[3] = {0x90, 60, 110};
    uint8_t off[3] = {0x80, 60, 0};
    int16_t out[128 * 2];
    float mono[64 * 128];
    int idx = 0;
    float peak = 0.0f;

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

    {
        int start = 48 * 128;
        int end = 64 * 128;
        int zc = 0;
        float best = 0.0f;

        for (int i = start + 1; i < end; ++i) {
            float a = mono[i - 1];
            float b = mono[i];
            if ((a >= 0.0f && b < 0.0f) || (a < 0.0f && b >= 0.0f)) zc += 1;
        }

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
                if (r > best) best = r;
            }
        }

        render_metrics_t m;
        m.peak = peak;
        m.zc_rate = (float)zc / (float)(end - start - 1);
        m.autocorr = best;
        return m;
    }
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

    write_fixture(
        "build/selfosc_barking_fixture.vstpreset",
        "programname=\"Barking-ish\" "
        "sawvolume=\"0.0\" pulsevolume=\"0.0\" suboscvolume=\"0.0\" noisevolume=\"0.0\" "
        "filtercutoff=\"0.184\" filterresonance=\"1.0\" filterenvelopevalue=\"0.504\" filterenvelopevaluefullrange=\"0.0\" filterkeyboardvalue=\"1.0\" "
        "filtermodulationvalue=\"0.0\" "
        "adsrattack=\"0.152\" adsrdecay=\"0.272\" adsrsustain=\"0.708\" adsrrelease=\"0.0\" "
        "adsrmode=\"0.691\" vcamode=\"1.0\" "
        "lforate=\"0.0\" lfowaveform=\"0.0\" lfotrigger=\"0.0\" lfosync=\"0.0\" lfoinverted=\"0.0\" "
        "volume=\"0.382\" masterfinetune=\"0.5\" octavetranspose=\"0.5\""
    );
    write_fixture(
        "build/selfosc_arcade_fixture.vstpreset",
        "programname=\"Arcade-ish\" "
        "sawvolume=\"0.0\" pulsevolume=\"0.0\" suboscvolume=\"0.0\" noisevolume=\"0.0\" "
        "filtercutoff=\"0.0\" filterresonance=\"1.0\" filterenvelopevalue=\"1.0\" filterenvelopevaluefullrange=\"1.0\" filterkeyboardvalue=\"0.956\" "
        "filtermodulationvalue=\"0.416\" "
        "adsrattack=\"0.0\" adsrdecay=\"0.752\" adsrsustain=\"0.0\" adsrrelease=\"0.0\" "
        "adsrmode=\"1.0\" vcamode=\"0.0\" "
        "lforate=\"0.596\" lfowaveform=\"0.434\" lfotrigger=\"1.0\" lfosync=\"0.0\" lfoinverted=\"0.0\" "
        "volume=\"0.361\" masterfinetune=\"0.5\" octavetranspose=\"0.5\""
    );

    api->set_param(inst, "import_vstpreset_path", "build/selfosc_barking_fixture.vstpreset");
    render_metrics_t barking = render_metrics(api, inst);
    api->set_param(inst, "all_notes_off", "1");
    api->set_param(inst, "import_vstpreset_path", "build/selfosc_arcade_fixture.vstpreset");
    render_metrics_t arcade = render_metrics(api, inst);

    /* Barking should be a pitched, tonal resonance (AU autocorr ~0.97). */
    assert(barking.peak > 0.01f);
    assert(barking.autocorr > 0.70f);
    assert(barking.zc_rate < 0.08f);
    /* Arcade sweeper is intentionally noisier/random than Barking. */
    assert(arcade.peak > 0.005f);
    assert(arcade.autocorr + 0.20f < barking.autocorr);

    api->destroy_instance(inst);
    return 0;
}
