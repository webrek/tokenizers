#ifndef TK_NORMALIZE_H
#define TK_NORMALIZE_H
#include <stddef.h>
typedef struct { size_t start, end; } tk_span;
int tk_bert_normalize(const char *text, size_t len, int lowercase, int strip_accents,
                      int handle_cjk, char **out, size_t *out_len, tk_span **spans, size_t *nspans);
#endif
