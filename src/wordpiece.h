#ifndef TK_WORDPIECE_H
#define TK_WORDPIECE_H
#include <stdint.h>
#include "model.h"
typedef struct { uint32_t unk_id; const char *prefix; size_t prefix_len; size_t max_chars;
                 int lowercase, strip_accents, handle_cjk; } tk_wp_opts;
int tk_wordpiece_encode(const tk_model *m, const char *text, size_t len, const tk_wp_opts *o,
                        uint32_t **out_ids, size_t *n_out);
#endif
