#include <assert.h>

#include "sh101_env.h"

int main(void) {
    sh101_env_t e;
    sh101_env_init(&e, 44100.0f);
    sh101_env_set_adsr(&e, 0.01f, 0.12f, 0.65f, 0.2f);
    sh101_env_gate_on(&e, 1.0f);
    for (int i = 0; i < 441; ++i) {
        sh101_env_process(&e);
    }
    assert(e.value > 0.8f);
    return 0;
}
