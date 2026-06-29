#include "cache.h"
#include <stdlib.h>
#include <string.h>

#ifdef ZTS
#include "TSRM.h"
static MUTEX_T tk_cache_mx;
#define TK_LOCK()   tsrm_mutex_lock(tk_cache_mx)
#define TK_UNLOCK() tsrm_mutex_unlock(tk_cache_mx)
#else
#define TK_LOCK()
#define TK_UNLOCK()
#endif

typedef struct { char *key; tk_model *model; } entry;
static entry *g_entries = NULL; static size_t g_count = 0, g_cap = 0;

void tk_cache_init(void) {
#ifdef ZTS
    if (!tk_cache_mx) tk_cache_mx = tsrm_mutex_alloc();
#endif
}

static const tk_model *find(const char *key) {
    for (size_t i = 0; i < g_count; i++) if (strcmp(g_entries[i].key, key) == 0) return g_entries[i].model;
    return NULL;
}

const tk_model *tk_cache_get_or_load(const char *key, tk_loader_fn loader, void *ud, char **err) {
    if (err) *err = NULL;
    TK_LOCK();
    const tk_model *m = find(key);
    if (m) { TK_UNLOCK(); return m; }
    char *lerr = NULL;
    tk_model *nm = loader(ud, &lerr);
    if (!nm) { TK_UNLOCK(); if (err) *err = lerr; else free(lerr); return NULL; }
    if (g_count == g_cap) { g_cap = g_cap ? g_cap * 2 : 8; g_entries = realloc(g_entries, g_cap * sizeof(entry)); }
    g_entries[g_count].key = malloc(strlen(key) + 1); strcpy(g_entries[g_count].key, key);
    g_entries[g_count].model = nm; g_count++;
    TK_UNLOCK();
    return nm;
}

size_t tk_cache_count(void) { return g_count; }

void tk_cache_shutdown(void) {
    for (size_t i = 0; i < g_count; i++) { free(g_entries[i].key); tk_model_free(g_entries[i].model); }
    free(g_entries); g_entries = NULL; g_count = g_cap = 0;
#ifdef ZTS
    if (tk_cache_mx) { tsrm_mutex_free(tk_cache_mx); tk_cache_mx = NULL; }
#endif
}
