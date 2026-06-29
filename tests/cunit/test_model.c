#include "model.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    tk_model *m = tk_model_new(8);
    assert(tk_model_add(m, (const uint8_t*)"a", 1, 10) == 0);
    assert(tk_model_add(m, (const uint8_t*)"b", 1, 11) == 0);
    assert(tk_model_add(m, (const uint8_t*)"ab", 2, 42) == 0);

    assert(tk_model_rank(m, (const uint8_t*)"ab", 2) == 42);
    assert(tk_model_rank(m, (const uint8_t*)"a", 1) == 10);
    assert(tk_model_rank(m, (const uint8_t*)"zz", 2) == 0xFFFFFFFFu); /* absent */

    size_t n = 0;
    const uint8_t *b = tk_model_bytes(m, 42, &n);
    assert(n == 2 && memcmp(b, "ab", 2) == 0);
    assert(tk_model_vocab_size(m) == 3);

    tk_model_free(m);
    printf("test_model OK\n");
    return 0;
}
