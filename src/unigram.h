#ifndef TK_UNIGRAM_H
#define TK_UNIGRAM_H
#include <stdint.h>
#include <stddef.h>
#include "model.h"
typedef struct { uint32_t unk_id; float unk_score; int add_prefix_space; size_t max_piece_len; } tk_ug_opts;
int tk_unigram_encode(const tk_model *m, const float *scores, const char *text, size_t len,
                      const tk_ug_opts *o, uint32_t **out_ids, size_t *n_out);
#endif
