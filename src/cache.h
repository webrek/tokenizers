#ifndef TK_CACHE_H
#define TK_CACHE_H
#include "model.h"
typedef tk_model *(*tk_loader_fn)(void *ud, char **err);
void tk_cache_init(void);
const tk_model *tk_cache_get_or_load(const char *key, tk_loader_fn loader, void *ud, char **err);
size_t tk_cache_count(void);
void tk_cache_shutdown(void);
#endif
