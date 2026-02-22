/* Renders a .vstpreset through the SH-101 module and writes a WAV file.
   Usage: render_sh101_wav <preset.vstpreset> <output.wav> */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/plugin_api_v1.h"

extern plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host);

static void write_le16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void write_le32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: render_sh101_wav <preset.vstpreset> <output.wav>\n");
        return 2;
    }
    const char *preset_path = argv[1];
    const char *wav_path = argv[2];

    host_api_v1_t host;
    memset(&host, 0, sizeof(host));
    host.api_version = MOVE_PLUGIN_API_VERSION;
    host.sample_rate = 44100;
    host.frames_per_block = 128;

    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    if (!api) { fprintf(stderr, "failed to init\n"); return 1; }
    void *inst = api->create_instance(".", NULL);
    if (!inst) { fprintf(stderr, "failed to create\n"); return 1; }
    api->set_param(inst, "import_vstpreset_path", preset_path);

    /* Render: 64 blocks of note-on + 32 blocks of note-off (release tail) */
    uint8_t on[3] = {0x90, 60, 110};
    uint8_t off[3] = {0x80, 60, 0};
    int16_t block[128 * 2];
    int total_blocks = 96;
    int total_samples = total_blocks * 128;
    int16_t *mono = (int16_t *)calloc(total_samples, sizeof(int16_t));

    api->on_midi(inst, on, 3, MOVE_MIDI_SOURCE_INTERNAL);
    for (int b = 0; b < 64; ++b) {
        api->render_block(inst, block, 128);
        for (int i = 0; i < 128; ++i)
            mono[b * 128 + i] = block[i * 2];
    }
    api->on_midi(inst, off, 3, MOVE_MIDI_SOURCE_INTERNAL);
    for (int b = 64; b < total_blocks; ++b) {
        api->render_block(inst, block, 128);
        for (int i = 0; i < 128; ++i)
            mono[b * 128 + i] = block[i * 2];
    }

    /* Write WAV */
    FILE *f = fopen(wav_path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", wav_path); return 1; }
    uint32_t data_size = total_samples * 2;
    fwrite("RIFF", 1, 4, f);
    write_le32(f, 36 + data_size);
    fwrite("WAVEfmt ", 1, 8, f);
    write_le32(f, 16);
    write_le16(f, 1);        /* PCM */
    write_le16(f, 1);        /* mono */
    write_le32(f, 44100);
    write_le32(f, 44100 * 2);
    write_le16(f, 2);
    write_le16(f, 16);
    fwrite("data", 1, 4, f);
    write_le32(f, data_size);
    fwrite(mono, 2, total_samples, f);
    fclose(f);

    fprintf(stderr, "wrote %s (%d samples, %.2fs)\n", wav_path, total_samples, total_samples / 44100.0f);
    api->destroy_instance(inst);
    free(mono);
    return 0;
}
