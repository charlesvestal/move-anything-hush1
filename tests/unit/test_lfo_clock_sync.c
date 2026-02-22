#include <assert.h>

#include "sh101_lfo.h"

int main(void) {
    sh101_lfo_t l;
    sh101_lfo_init(&l, 44100.0f);
    sh101_lfo_set_rate_hz(&l, 5.0f);
    float a = sh101_lfo_process(&l);
    for (int i = 0; i < 4410; ++i) {
        sh101_lfo_process(&l);
    }
    float b = sh101_lfo_process(&l);
    assert(a != b);
    return 0;
}
