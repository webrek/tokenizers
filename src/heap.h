#ifndef TK_HEAP_H
#define TK_HEAP_H
#include <stddef.h>
#include <stdint.h>
typedef struct tk_heap tk_heap;
tk_heap *tk_heap_new(size_t cap_hint);
void tk_heap_push(tk_heap *h, uint64_t key);
int tk_heap_pop(tk_heap *h, uint64_t *out);
void tk_heap_free(tk_heap *h);
#endif
