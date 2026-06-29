#ifndef TK_ENGINE_H
#define TK_ENGINE_H
#include "model.h"

size_t tk_bpe_merge(const tk_model *m, const uint8_t *piece, size_t len, uint32_t *out_ids);

/* growable id vector + full encode/decode/count, implemented in Task 8 */
typedef struct { uint32_t *data; size_t len, cap; } tk_ids;
void tk_ids_init(tk_ids *v);
void tk_ids_push(tk_ids *v, uint32_t id);
void tk_ids_free(tk_ids *v);

int tk_encode_ordinary(const tk_model *m, const uint8_t *text, size_t len, tk_ids *out);
int tk_encode(const tk_model *m, const uint8_t *text, size_t len,
              const char **allowed, size_t n_allowed, int disallow_unlisted,
              tk_ids *out, char **err);
size_t tk_count(const tk_model *m, const uint8_t *text, size_t len);
int tk_decode(const tk_model *m, const uint32_t *ids, size_t n,
              uint8_t **out, size_t *out_len, char **err);
#endif
