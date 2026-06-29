#include "heap.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    tk_heap *h = tk_heap_new(2);
    uint64_t vals[] = { 5, 1, 9, 1, 3, 7 };
    for (int i = 0; i < 6; i++) tk_heap_push(h, vals[i]);
    uint64_t out, prev = 0; int n = 0;
    while (tk_heap_pop(h, &out) == 0) { assert(out >= prev); prev = out; n++; }
    assert(n == 6);
    assert(tk_heap_pop(h, &out) == -1);
    tk_heap_free(h);
    printf("test_heap OK\n");
    return 0;
}
