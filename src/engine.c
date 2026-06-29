#include "engine.h"
#include "heap.h"
#include "pretok.h"
#include <stdlib.h>
#include <string.h>

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

/* ── Task 8: growable id vector ─────────────────────────────────────── */

void tk_ids_init(tk_ids *v) { v->data = NULL; v->len = 0; v->cap = 0; }
void tk_ids_push(tk_ids *v, uint32_t id) {
    if (v->len == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 64;
        uint32_t *tmp = realloc(v->data, ncap * sizeof(uint32_t));
        if (!tmp) return;
        v->data = tmp; v->cap = ncap;
    }
    v->data[v->len++] = id;
}
void tk_ids_free(tk_ids *v) { free(v->data); v->data = NULL; v->len = v->cap = 0; }

typedef struct { const tk_model *m; const uint8_t *text; tk_ids *out; uint32_t *scr; size_t scrcap; size_t count; } enc_ctx;

static void enc_emit(void *ud, size_t s, size_t e) {
    enc_ctx *c = ud; size_t plen = e - s;
    if (plen > c->scrcap) {
        uint32_t *tmp = realloc(c->scr, plen * sizeof(uint32_t));
        if (!tmp) return;
        c->scr = tmp; c->scrcap = plen;
    }
    size_t n = tk_bpe_merge(c->m, c->text + s, plen, c->scr);
    if (c->out) for (size_t i = 0; i < n; i++) tk_ids_push(c->out, c->scr[i]);
    c->count += n;
}

int tk_encode_ordinary(const tk_model *m, const uint8_t *text, size_t len, tk_ids *out) {
    enc_ctx c = { m, text, out, NULL, 0, 0 };
    int rc = tk_pretok_split(m, text, len, enc_emit, &c);
    free(c.scr);
    return rc;
}

size_t tk_count(const tk_model *m, const uint8_t *text, size_t len) {
    enc_ctx c = { m, text, NULL, NULL, 0, 0 };
    if (tk_pretok_split(m, text, len, enc_emit, &c) != 0) { free(c.scr); return 0; }
    free(c.scr);
    return c.count;
}

/* find earliest occurrence (>= from) of any model special; returns 1 if found */
static int find_special(const tk_model *m, const uint8_t *text, size_t len, size_t from,
                        size_t *pos_out, size_t *idx_out) {
    size_t best = len; int found = 0; size_t bi = 0;
    for (size_t i = 0; i < tk_model_special_count(m); i++) {
        size_t slen; uint32_t sid; const char *s = tk_model_special_at(m, i, &slen, &sid);
        if (slen == 0 || slen > len) continue;
        for (size_t p = from; p + slen <= len && p < best + 1; p++) {
            if (text[p] == (uint8_t)s[0] && memcmp(text + p, s, slen) == 0) {
                if (!found || p < best) { best = p; bi = i; found = 1; }
                break;
            }
        }
    }
    if (found) { *pos_out = best; *idx_out = bi; }
    return found;
}

static int is_allowed(const char **allowed, size_t n, const char *s, size_t slen) {
    for (size_t i = 0; i < n; i++) if (strlen(allowed[i]) == slen && memcmp(allowed[i], s, slen) == 0) return 1;
    return 0;
}

int tk_encode(const tk_model *m, const uint8_t *text, size_t len,
              const char **allowed, size_t n_allowed, int disallow_unlisted,
              tk_ids *out, char **err) {
    if (err) *err = NULL;
    if (tk_model_special_count(m) == 0) return tk_encode_ordinary(m, text, len, out);
    size_t cur = 0;
    while (cur < len) {
        size_t pos, idx;
        if (!find_special(m, text, len, cur, &pos, &idx)) {
            return tk_encode_ordinary(m, text + cur, len - cur, out) == 0 ? 0 : -1;
        }
        size_t slen; uint32_t sid; const char *s = tk_model_special_at(m, idx, &slen, &sid);
        int allowed_hit = is_allowed(allowed, n_allowed, s, slen);
        if (!allowed_hit) {
            if (disallow_unlisted) {
                if (err) { const char *p = "encountered a disallowed special token"; char *e = malloc(strlen(p)+1); strcpy(e,p); *err = e; }
                return -1;
            }
            /* treat as ordinary: extend ordinary span to include it */
            if (tk_encode_ordinary(m, text + cur, (pos + slen) - cur, out) != 0) return -1;
            cur = pos + slen; continue;
        }
        if (pos > cur && tk_encode_ordinary(m, text + cur, pos - cur, out) != 0) return -1;
        tk_ids_push(out, sid);
        cur = pos + slen;
    }
    return 0;
}

int tk_decode(const tk_model *m, const uint32_t *ids, size_t n,
              uint8_t **out, size_t *out_len, char **err) {
    if (err) *err = NULL;
    size_t cap = 64, o = 0; uint8_t *buf = malloc(cap);
    for (size_t i = 0; i < n; i++) {
        size_t blen; const uint8_t *b = tk_model_bytes(m, ids[i], &blen);
        const char *sb = NULL; size_t slen = 0;
        if (!b) { /* maybe a special id */
            for (size_t k = 0; k < tk_model_special_count(m); k++) {
                size_t l; uint32_t sid; const char *s = tk_model_special_at(m, k, &l, &sid);
                if (sid == ids[i]) { sb = s; slen = l; break; }
            }
            if (!sb) { free(buf); if (err) { const char *p="invalid token id in decode"; char *e=malloc(strlen(p)+1); strcpy(e,p); *err=e; } return -1; }
            b = (const uint8_t*)sb; blen = slen;
        }
        if (o + blen > cap) {
            while (o + blen > cap) cap *= 2;
            uint8_t *tmp = realloc(buf, cap);
            if (!tmp) { free(buf); if (err) { const char *p = "out of memory"; char *e = malloc(strlen(p)+1); if (e) strcpy(e,p); *err = e; } return -1; }
            buf = tmp;
        }
        memcpy(buf + o, b, blen); o += blen;
    }
    *out = buf; *out_len = o;
    return 0;
}
