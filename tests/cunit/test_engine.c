#include "model.h"
#include "engine.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    tk_model *m = tk_model_new(8);
    tk_model_add(m, (const uint8_t*)"a", 1, 0);
    tk_model_add(m, (const uint8_t*)"b", 1, 1);
    tk_model_add(m, (const uint8_t*)"c", 1, 2);
    tk_model_add(m, (const uint8_t*)"bc", 2, 3);
    tk_model_add(m, (const uint8_t*)"aa", 2, 4);
    uint32_t out[16];

    size_t n = tk_bpe_merge(m, (const uint8_t*)"abc", 3, out);
    assert(n == 2 && out[0] == 0 && out[1] == 3);          /* a + bc */

    n = tk_bpe_merge(m, (const uint8_t*)"bc", 2, out);
    assert(n == 1 && out[0] == 3);

    n = tk_bpe_merge(m, (const uint8_t*)"a", 1, out);
    assert(n == 1 && out[0] == 0);

    n = tk_bpe_merge(m, (const uint8_t*)"aaaa", 4, out);
    assert(n == 2 && out[0] == 4 && out[1] == 4);          /* aa + aa */

    tk_model_free(m);
    printf("test_engine OK\n");
    return 0;
}
