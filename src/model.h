#ifndef TK_MODEL_H
#define TK_MODEL_H
#include <stddef.h>
#include <stdint.h>

#define TK_RANK_MAX 0xFFFFFFFFu
#define TK_MAX_VOCAB_ID 0x3FFFFFu  /* 4,194,303 — well above any real vocab; bounds id-array size */

typedef struct tk_model tk_model;

tk_model *tk_model_new(uint32_t vocab_hint);
void tk_model_free(tk_model *m);
int tk_model_add(tk_model *m, const uint8_t *bytes, size_t len, uint32_t id);
uint32_t tk_model_rank(const tk_model *m, const uint8_t *bytes, size_t len);
const uint8_t *tk_model_bytes(const tk_model *m, uint32_t id, size_t *len_out);
uint32_t tk_model_vocab_size(const tk_model *m);

/* set/get the pre-tokenizer pattern and special tokens (used by later tasks) */
int tk_model_set_pattern(tk_model *m, const char *pattern);          /* compiles PCRE2 (pretok.c) */
int tk_model_set_pattern_str(tk_model *m, const char *pattern);      /* stores pattern string only */
const void *tk_model_pattern_code(const tk_model *m);                /* pcre2_code* (opaque here) */
void tk_model__set_pcre2(tk_model *m, void *code, void (*freefn)(void*)); /* internal: pretok */
char **tk_model__pattern_slot(tk_model *m);                          /* internal: pattern string slot */
int tk_model_add_special(tk_model *m, const char *s, size_t len, uint32_t id);
uint32_t tk_model_special_id(const tk_model *m, const uint8_t *s, size_t len); /* TK_RANK_MAX if none */
size_t tk_model_special_count(const tk_model *m);
const char *tk_model_special_at(const tk_model *m, size_t i, size_t *len_out, uint32_t *id_out);

#endif
