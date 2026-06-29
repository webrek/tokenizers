#include "cache.h"
#include "model.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static int calls = 0;
static tk_model *ld(void *ud, char **err) {
    (void)ud; (void)err; calls++;
    tk_model *m = tk_model_new(4); tk_model_add(m, (const uint8_t*)"x", 1, 0); return m;
}
int main(void) {
    tk_cache_init();
    const tk_model *a = tk_cache_get_or_load("k1", ld, NULL, NULL);
    const tk_model *b = tk_cache_get_or_load("k1", ld, NULL, NULL);
    assert(a == b && calls == 1);
    const tk_model *c = tk_cache_get_or_load("k2", ld, NULL, NULL);
    assert(c != a && calls == 2);
    assert(tk_cache_count() == 2);
    tk_cache_shutdown();
    printf("test_cache OK\n");
    return 0;
}
