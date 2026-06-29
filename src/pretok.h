#ifndef TK_PRETOK_H
#define TK_PRETOK_H
#include "model.h"
int tk_pretok_split(const tk_model *m, const uint8_t *text, size_t len,
                    void (*emit)(void *ud, size_t start, size_t end), void *ud);
#endif
