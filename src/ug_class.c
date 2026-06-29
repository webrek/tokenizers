#include <string.h>
#include <stdlib.h>
#include "php.h"
#include "zend_exceptions.h"
#include "php_tokenizers.h"
#include "tokenizers_arginfo.h"
#include "src/model.h"
#include "src/unigram.h"

extern zend_class_entry *tokenizers_exception_ce;
zend_class_entry *tokenizers_ug_ce;

typedef struct {
    const tk_model *model;
    int owns;
    float *scores;        /* malloc'd, indexed by id; free'd in free_obj */
    size_t max_piece_len;
    tk_ug_opts opts;
    zend_object std;      /* MUST be last */
} tk_ug_obj;

static inline tk_ug_obj *tk_ug_from(zend_object *o) {
    return (tk_ug_obj*)((char*)o - XtOffsetOf(tk_ug_obj, std));
}

static zend_object_handlers tk_ug_handlers;

static zend_object *tk_ug_create(zend_class_entry *ce) {
    tk_ug_obj *o = zend_object_alloc(sizeof(tk_ug_obj), ce);
    zend_object_std_init(&o->std, ce);
    object_properties_init(&o->std, ce);
    o->std.handlers = &tk_ug_handlers;
    o->model = NULL; o->owns = 0; o->scores = NULL; o->max_piece_len = 0;
    memset(&o->opts, 0, sizeof(o->opts));
    return &o->std;
}

static void tk_ug_free(zend_object *obj) {
    tk_ug_obj *o = tk_ug_from(obj);
    if (o->owns && o->model) tk_model_free((tk_model*)o->model);
    if (o->scores) free(o->scores);
    zend_object_std_dtor(&o->std);
}

static void tk_ug_throw(const char *msg) {
    zend_throw_exception(tokenizers_exception_ce, msg, 0);
}

PHP_METHOD(Tokenizers_Unigram, fromVocab) {
    zval *pieces_arr; zval *opts_arr = NULL;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_ARRAY(pieces_arr)
        Z_PARAM_OPTIONAL Z_PARAM_ARRAY(opts_arr)
    ZEND_PARSE_PARAMETERS_END();

    /* defaults */
    zend_long unk_id_opt = 0;
    int has_unk_id = 0;
    zend_bool add_prefix_space = 1;

    if (opts_arr) {
        zval *zv;
        if ((zv = zend_hash_str_find(Z_ARRVAL_P(opts_arr), "unkId", 5))) {
            unk_id_opt = zval_get_long(zv);
            has_unk_id = 1;
        }
        if ((zv = zend_hash_str_find(Z_ARRVAL_P(opts_arr), "addPrefixSpace", 14))) {
            add_prefix_space = zend_is_true(zv);
        }
    }

    zend_ulong n_pieces = (zend_ulong)zend_hash_num_elements(Z_ARRVAL_P(pieces_arr));
    if (n_pieces == 0) {
        tk_ug_throw("pieces array must not be empty");
        RETURN_THROWS();
    }

    tk_model *m = tk_model_new((uint32_t)n_pieces);
    float *scores = (float*)malloc((size_t)n_pieces * sizeof(float));
    if (!scores) {
        tk_model_free(m);
        tk_ug_throw("out of memory");
        RETURN_THROWS();
    }

    size_t max_piece_len = 0;
    uint32_t resolved_unk_id = 0;
    int found_unk = 0;
    int vocab_err = 0;
    uint32_t id = 0;
    zval *pair;

    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(pieces_arr), pair) {
        ZVAL_DEREF(pair);
        if (Z_TYPE_P(pair) != IS_ARRAY) { vocab_err = 1; break; }

        zval *piece_zv = zend_hash_index_find(Z_ARRVAL_P(pair), 0);
        zval *score_zv = zend_hash_index_find(Z_ARRVAL_P(pair), 1);
        if (!piece_zv || !score_zv) { vocab_err = 1; break; }

        zend_string *piece_str_obj = zval_get_string(piece_zv);
        const char *piece_str = ZSTR_VAL(piece_str_obj);
        size_t piece_len = ZSTR_LEN(piece_str_obj);

        float score = (float)zval_get_double(score_zv);

        int rc = tk_model_add(m, (const uint8_t*)piece_str, piece_len, id);

        /* Check for <unk> before releasing piece_str_obj */
        if (!has_unk_id && !found_unk && piece_len == 5 && memcmp(piece_str, "<unk>", 5) == 0) {
            resolved_unk_id = id;
            found_unk = 1;
        }

        zend_string_release(piece_str_obj);

        if (rc == -1) { vocab_err = 1; break; }

        scores[id] = score;
        if (piece_len > max_piece_len) max_piece_len = piece_len;

        id++;
    } ZEND_HASH_FOREACH_END();

    if (vocab_err) {
        tk_model_free(m); free(scores);
        tk_ug_throw("invalid pieces array");
        RETURN_THROWS();
    }

    if (has_unk_id) {
        if (unk_id_opt < 0 || (zend_ulong)unk_id_opt >= n_pieces) {
            tk_model_free(m); free(scores);
            tk_ug_throw("unkId out of range");
            RETURN_THROWS();
        }
        resolved_unk_id = (uint32_t)unk_id_opt;
    } else if (!found_unk) {
        resolved_unk_id = 0;
    }

    float unk_score = (resolved_unk_id < (uint32_t)n_pieces) ? scores[resolved_unk_id] : -10.0f;

    /* Construct the PHP object */
    object_init_ex(return_value, tokenizers_ug_ce);
    tk_ug_obj *o = tk_ug_from(Z_OBJ_P(return_value));
    o->model          = m;
    o->owns           = 1;
    o->scores         = scores;
    o->max_piece_len  = max_piece_len;
    o->opts.unk_id         = resolved_unk_id;
    o->opts.unk_score      = unk_score;
    o->opts.add_prefix_space = (int)add_prefix_space;
    o->opts.max_piece_len  = max_piece_len;
}

PHP_METHOD(Tokenizers_Unigram, encode) {
    char *text; size_t text_len;
    ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_STRING(text, text_len) ZEND_PARSE_PARAMETERS_END();
    tk_ug_obj *o = tk_ug_from(Z_OBJ_P(ZEND_THIS));

    uint32_t *ids = NULL; size_t n = 0;
    int rc = tk_unigram_encode(o->model, o->scores, text, text_len, &o->opts, &ids, &n);
    if (rc != 0) { tk_ug_throw("encode failed"); RETURN_THROWS(); }

    array_init_size(return_value, n);
    for (size_t i = 0; i < n; i++) add_next_index_long(return_value, ids[i]);
    if (ids) free(ids);
}

PHP_METHOD(Tokenizers_Unigram, countTokens) {
    char *text; size_t text_len;
    ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_STRING(text, text_len) ZEND_PARSE_PARAMETERS_END();
    tk_ug_obj *o = tk_ug_from(Z_OBJ_P(ZEND_THIS));

    uint32_t *ids = NULL; size_t n = 0;
    int rc = tk_unigram_encode(o->model, o->scores, text, text_len, &o->opts, &ids, &n);
    if (rc != 0) { tk_ug_throw("encode failed"); RETURN_THROWS(); }
    if (ids) free(ids);
    RETURN_LONG((zend_long)n);
}

PHP_METHOD(Tokenizers_Unigram, decode) {
    zval *arr;
    ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_ARRAY(arr) ZEND_PARSE_PARAMETERS_END();
    tk_ug_obj *o = tk_ug_from(Z_OBJ_P(ZEND_THIS));

    /* Concatenate pieces, replacing ▁ (0xE2 0x96 0x81) with a single space,
       then ltrim ONE leading space (produced by add_prefix_space). */
    size_t buf_cap = 64, buf_len = 0;
    char *buf = emalloc(buf_cap);
    zval *zv;

    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(arr), zv) {
        uint32_t id = (uint32_t)zval_get_long(zv);
        size_t blen = 0;
        const uint8_t *bytes = tk_model_bytes(o->model, id, &blen);
        if (!bytes || !blen) continue;

        /* Worst case: blen bytes (▁ shrinks from 3→1, never grows) */
        if (buf_len + blen + 1 >= buf_cap) {
            while (buf_cap <= buf_len + blen) buf_cap *= 2;
            buf = erealloc(buf, buf_cap);
        }

        for (size_t i = 0; i < blen; ) {
            if (i + 2 < blen
                && (unsigned char)bytes[i]   == 0xE2
                && (unsigned char)bytes[i+1] == 0x96
                && (unsigned char)bytes[i+2] == 0x81) {
                buf[buf_len++] = ' ';
                i += 3;
            } else {
                buf[buf_len++] = (char)bytes[i++];
            }
        }
    } ZEND_HASH_FOREACH_END();

    /* ltrim one leading space */
    size_t start = (buf_len > 0 && buf[0] == ' ') ? 1 : 0;

    RETVAL_STRINGL(buf + start, buf_len - start);
    efree(buf);
}

PHP_METHOD(Tokenizers_Unigram, vocabSize) {
    tk_ug_obj *o = tk_ug_from(Z_OBJ_P(ZEND_THIS));
    RETURN_LONG((zend_long)tk_model_vocab_size(o->model));
}

void tk_register_ug_class(void) {
    tokenizers_ug_ce = register_class_Tokenizers_Unigram();
    tokenizers_ug_ce->create_object = tk_ug_create;
    memcpy(&tk_ug_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    tk_ug_handlers.offset    = XtOffsetOf(tk_ug_obj, std);
    tk_ug_handlers.free_obj  = tk_ug_free;
    tk_ug_handlers.clone_obj = NULL; /* disable cloning; Zend throws "uncloneable" automatically */
}
