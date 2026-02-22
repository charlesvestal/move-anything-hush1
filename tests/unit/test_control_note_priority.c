#include <assert.h>

#include "sh101_control.h"

int main(void) {
    sh101_control_t c;
    sh101_control_init(&c, 44100.0f);

    sh101_control_note_on(&c, 60, 100);
    assert(c.current_note == 60);

    sh101_control_note_on(&c, 64, 100);
    assert(c.current_note == 64);

    sh101_control_note_off(&c, 64);
    assert(c.current_note == 60);

    c.glide_ms = 80.0f;
    sh101_control_note_on(&c, 67, 100);
    assert(c.pitch_target_hz > c.pitch_current_hz);

    sh101_control_set_priority(&c, SH101_NOTE_PRIORITY_LOWEST);
    assert(c.current_note == 60);

    return 0;
}
