#include "model.h"
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *bytes; uint32_t len; uint32_t id; uint8_t used; } slot;
typedef struct { uint8_t *s; uint32_t len; uint32_t id; } special;

struct tk_model {
    slot *tab; uint32_t cap; uint32_t count;          /* bytes->id */
    uint8_t **id_bytes; uint32_t *id_len; uint32_t id_cap; /* id->bytes */
    special *specials; size_t spec_count, spec_cap;
    void *pcre2;                                       /* pcre2_code* */
    void (*pcre2_free)(void*);                          /* set by pretok when compiling */
    char *pattern;
};

static uint64_t fnv1a(const uint8_t *b, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}
static void tab_insert(slot *tab, uint32_t cap, const uint8_t *b, uint32_t len, uint32_t id) {
    uint32_t i = (uint32_t)(fnv1a(b, len) & (cap - 1));
    while (tab[i].used) i = (i + 1) & (cap - 1);
    tab[i].bytes = (uint8_t*)malloc(len ? len : 1);
    memcpy(tab[i].bytes, b, len);
    tab[i].len = len; tab[i].id = id; tab[i].used = 1;
}
static uint32_t next_pow2(uint32_t x){ uint32_t p=8; while(p<x) p<<=1; return p; }

tk_model *tk_model_new(uint32_t vocab_hint) {
    tk_model *m = calloc(1, sizeof *m);
    m->cap = next_pow2(vocab_hint ? vocab_hint * 2 : 16);
    m->tab = calloc(m->cap, sizeof(slot));
    m->id_cap = vocab_hint ? vocab_hint : 16;
    m->id_bytes = calloc(m->id_cap, sizeof(uint8_t*));
    m->id_len = calloc(m->id_cap, sizeof(uint32_t));
    return m;
}
static void rehash(tk_model *m) {
    uint32_t ncap = m->cap << 1;
    slot *nt = calloc(ncap, sizeof(slot));
    for (uint32_t i = 0; i < m->cap; i++)
        if (m->tab[i].used) tab_insert(nt, ncap, m->tab[i].bytes, m->tab[i].len, m->tab[i].id);
    for (uint32_t i = 0; i < m->cap; i++) if (m->tab[i].used) free(m->tab[i].bytes);
    free(m->tab); m->tab = nt; m->cap = ncap;
}
int tk_model_add(tk_model *m, const uint8_t *bytes, size_t len, uint32_t id) {
    if ((m->count + 1) * 4 >= m->cap * 3) rehash(m);
    tab_insert(m->tab, m->cap, bytes, (uint32_t)len, id);
    m->count++;
    if (id >= m->id_cap) {
        uint32_t nc = m->id_cap; while (nc <= id) nc <<= 1;
        m->id_bytes = realloc(m->id_bytes, nc * sizeof(uint8_t*));
        m->id_len = realloc(m->id_len, nc * sizeof(uint32_t));
        memset(m->id_bytes + m->id_cap, 0, (nc - m->id_cap) * sizeof(uint8_t*));
        m->id_cap = nc;
    }
    m->id_bytes[id] = malloc(len ? len : 1); memcpy(m->id_bytes[id], bytes, len);
    m->id_len[id] = (uint32_t)len;
    return 0;
}
uint32_t tk_model_rank(const tk_model *m, const uint8_t *bytes, size_t len) {
    uint32_t i = (uint32_t)(fnv1a(bytes, len) & (m->cap - 1));
    while (m->tab[i].used) {
        if (m->tab[i].len == len && memcmp(m->tab[i].bytes, bytes, len) == 0) return m->tab[i].id;
        i = (i + 1) & (m->cap - 1);
    }
    return TK_RANK_MAX;
}
const uint8_t *tk_model_bytes(const tk_model *m, uint32_t id, size_t *len_out) {
    if (id >= m->id_cap || !m->id_bytes[id]) return NULL;
    if (len_out) *len_out = m->id_len[id];
    return m->id_bytes[id];
}
uint32_t tk_model_vocab_size(const tk_model *m) { return m->count; }

int tk_model_add_special(tk_model *m, const char *s, size_t len, uint32_t id) {
    if (m->spec_count == m->spec_cap) {
        m->spec_cap = m->spec_cap ? m->spec_cap * 2 : 8;
        m->specials = realloc(m->specials, m->spec_cap * sizeof(special));
    }
    special *sp = &m->specials[m->spec_count++];
    sp->s = malloc(len); memcpy(sp->s, s, len); sp->len = (uint32_t)len; sp->id = id;
    if (id < m->id_cap) { /* allow decode of specials too */ }
    return 0;
}
uint32_t tk_model_special_id(const tk_model *m, const uint8_t *s, size_t len) {
    for (size_t i = 0; i < m->spec_count; i++)
        if (m->specials[i].len == len && memcmp(m->specials[i].s, s, len) == 0) return m->specials[i].id;
    return TK_RANK_MAX;
}
size_t tk_model_special_count(const tk_model *m) { return m->spec_count; }
const char *tk_model_special_at(const tk_model *m, size_t i, size_t *len_out, uint32_t *id_out) {
    if (i >= m->spec_count) return NULL;
    if (len_out) *len_out = m->specials[i].len;
    if (id_out) *id_out = m->specials[i].id;
    return (const char*)m->specials[i].s;
}

/* pattern setters defined in Task 5 (pretok) to keep PCRE2 there; declared weakly here */
void tk_model_free(tk_model *m) {
    if (!m) return;
    for (uint32_t i = 0; i < m->cap; i++) if (m->tab[i].used) free(m->tab[i].bytes);
    free(m->tab);
    for (uint32_t i = 0; i < m->id_cap; i++) free(m->id_bytes[i]);
    free(m->id_bytes); free(m->id_len);
    for (size_t i = 0; i < m->spec_count; i++) free(m->specials[i].s);
    free(m->specials);
    if (m->pcre2 && m->pcre2_free) m->pcre2_free(m->pcre2);
    free(m->pattern);
    free(m);
}
/* expose struct internals to pretok.c via accessors */
void tk_model__set_pcre2(tk_model *m, void *code, void (*freefn)(void*)) {
    if (m->pcre2 && m->pcre2_free) m->pcre2_free(m->pcre2);
    m->pcre2 = code; m->pcre2_free = freefn;
}
char **tk_model__pattern_slot(tk_model *m) { return &m->pattern; }
const void *tk_model_pattern_code(const tk_model *m) { return m->pcre2; }
