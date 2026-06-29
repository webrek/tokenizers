#include "engine.h"
#include "heap.h"
#include <stdlib.h>

static uint32_t pair_rank(const tk_model *m, const uint8_t *piece, size_t p,
                          const size_t *next, size_t len) {
    size_t q = next[p];
    if (q >= len) return TK_RANK_MAX;
    size_t s = next[q];
    return tk_model_rank(m, piece + p, s - p);
}

size_t tk_bpe_merge(const tk_model *m, const uint8_t *piece, size_t len, uint32_t *out_ids) {
    if (len == 0) return 0;
    if (len == 1) { out_ids[0] = tk_model_rank(m, piece, 1); return 1; }

    size_t *next = malloc((len + 1) * sizeof(size_t));
    size_t *prev = malloc((len + 1) * sizeof(size_t));
    uint8_t *alive = malloc((len + 1) * sizeof(uint8_t));
    for (size_t i = 0; i <= len; i++) { next[i] = i + 1; prev[i] = (i == 0) ? 0 : i - 1; alive[i] = 1; }
    next[len] = len; alive[len] = 0;

    tk_heap *h = tk_heap_new(len);
    for (size_t p = 0; p < len; p = next[p]) {
        uint32_t r = pair_rank(m, piece, p, next, len);
        if (r != TK_RANK_MAX) tk_heap_push(h, ((uint64_t)r << 32) | (uint32_t)p);
    }

    uint64_t kv;
    while (tk_heap_pop(h, &kv) == 0) {
        uint32_t r = (uint32_t)(kv >> 32);
        size_t p = (size_t)(uint32_t)kv;
        if (!alive[p]) continue;
        if (pair_rank(m, piece, p, next, len) != r) continue;
        size_t q = next[p], s = next[q];
        next[p] = s; if (s <= len) prev[s] = p; alive[q] = 0;
        uint32_t rp = pair_rank(m, piece, p, next, len);
        if (rp != TK_RANK_MAX) tk_heap_push(h, ((uint64_t)rp << 32) | (uint32_t)p);
        if (p != 0) { size_t pp = prev[p];
            uint32_t rpp = pair_rank(m, piece, pp, next, len);
            if (rpp != TK_RANK_MAX) tk_heap_push(h, ((uint64_t)rpp << 32) | (uint32_t)pp); }
    }
    tk_heap_free(h);

    size_t n = 0;
    for (size_t p = 0; p < len; p = next[p]) out_ids[n++] = tk_model_rank(m, piece + p, next[p] - p);
    free(next); free(prev); free(alive);
    return n;
}
