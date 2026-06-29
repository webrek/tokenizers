#include "heap.h"
#include <stdlib.h>
struct tk_heap { uint64_t *a; size_t len, cap; };
tk_heap *tk_heap_new(size_t cap_hint) {
    tk_heap *h = malloc(sizeof *h);
    h->cap = cap_hint < 4 ? 4 : cap_hint; h->len = 0;
    h->a = malloc(h->cap * sizeof(uint64_t));
    return h;
}
void tk_heap_push(tk_heap *h, uint64_t key) {
    if (h->len == h->cap) { h->cap *= 2; h->a = realloc(h->a, h->cap * sizeof(uint64_t)); }
    size_t i = h->len++; h->a[i] = key;
    while (i > 0) { size_t p = (i - 1) / 2; if (h->a[p] <= h->a[i]) break;
        uint64_t t = h->a[p]; h->a[p] = h->a[i]; h->a[i] = t; i = p; }
}
int tk_heap_pop(tk_heap *h, uint64_t *out) {
    if (h->len == 0) return -1;
    *out = h->a[0]; h->a[0] = h->a[--h->len];
    size_t i = 0;
    for (;;) { size_t l = 2*i+1, r = 2*i+2, m = i;
        if (l < h->len && h->a[l] < h->a[m]) m = l;
        if (r < h->len && h->a[r] < h->a[m]) m = r;
        if (m == i) break;
        uint64_t t = h->a[m]; h->a[m] = h->a[i]; h->a[i] = t; i = m; }
    return 0;
}
void tk_heap_free(tk_heap *h) { if (h) { free(h->a); free(h); } }
