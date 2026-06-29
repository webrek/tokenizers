#include <string.h>
#include <stdlib.h>
#include "php.h"
#include "zend_exceptions.h"
#include "php_tokenizers.h"
#include "tokenizers_arginfo.h"
#include "src/model.h"
#include "src/wordpiece.h"

extern zend_class_entry *tokenizers_exception_ce;
zend_class_entry *tokenizers_wp_ce;

typedef struct {
    const tk_model *model;
    int owns;
    tk_wp_opts opts;
    char *prefix_buf; /* owned copy of opts.prefix — valid for object lifetime */
    zend_object std;  /* MUST be last */
} tk_wp_obj;

static inline tk_wp_obj *tk_wp_from(zend_object *o) {
    return (tk_wp_obj*)((char*)o - XtOffsetOf(tk_wp_obj, std));
}

static zend_object_handlers tk_wp_handlers;

static zend_object *tk_wp_create(zend_class_entry *ce) {
    tk_wp_obj *o = zend_object_alloc(sizeof(tk_wp_obj), ce);
    zend_object_std_init(&o->std, ce);
    object_properties_init(&o->std, ce);
    o->std.handlers = &tk_wp_handlers;
    o->model = NULL; o->owns = 0; o->prefix_buf = NULL;
    memset(&o->opts, 0, sizeof(o->opts));
    return &o->std;
}

static void tk_wp_free(zend_object *obj) {
    tk_wp_obj *o = tk_wp_from(obj);
    if (o->owns && o->model) tk_model_free((tk_model*)o->model);
    if (o->prefix_buf) efree(o->prefix_buf);
    zend_object_std_dtor(&o->std);
}

static void tk_wp_throw(const char *msg) {
    zend_throw_exception(tokenizers_exception_ce, msg, 0);
}

PHP_METHOD(Tokenizers_WordPiece, fromVocab) {
    zval *vocab; zval *opts_arr = NULL;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_ARRAY(vocab)
        Z_PARAM_OPTIONAL Z_PARAM_ARRAY(opts_arr)
    ZEND_PARSE_PARAMETERS_END();

    /* defaults */
    const char *unk_token     = "[UNK]";
    size_t      unk_token_len = 5;
    const char *prefix_str    = "##";
    size_t      prefix_len    = 2;
    zend_long   max_chars     = 100;
    zend_bool   lowercase     = 1;
    zend_bool   strip_accents = 1;
    zend_bool   handle_cjk    = 1;

    if (opts_arr) {
        zval *zv;
        if ((zv = zend_hash_str_find(Z_ARRVAL_P(opts_arr), "unkToken", 8)) && Z_TYPE_P(zv) == IS_STRING) {
            unk_token = Z_STRVAL_P(zv); unk_token_len = Z_STRLEN_P(zv);
        }
        if ((zv = zend_hash_str_find(Z_ARRVAL_P(opts_arr), "continuingSubwordPrefix", 23)) && Z_TYPE_P(zv) == IS_STRING) {
            prefix_str = Z_STRVAL_P(zv); prefix_len = Z_STRLEN_P(zv);
        }
        if ((zv = zend_hash_str_find(Z_ARRVAL_P(opts_arr), "maxInputCharsPerWord", 20)) && Z_TYPE_P(zv) == IS_LONG) {
            max_chars = Z_LVAL_P(zv);
        }
        if ((zv = zend_hash_str_find(Z_ARRVAL_P(opts_arr), "lowercase", 9))) {
            lowercase = zend_is_true(zv);
        }
        if ((zv = zend_hash_str_find(Z_ARRVAL_P(opts_arr), "stripAccents", 12))) {
            strip_accents = zend_is_true(zv);
        }
        if ((zv = zend_hash_str_find(Z_ARRVAL_P(opts_arr), "handleChineseChars", 18))) {
            handle_cjk = zend_is_true(zv);
        }
    }

    /* Build the model from tokenToId array (ZEND_HASH_FOREACH_KEY_VAL handles
       integer-coerced keys — mirrors the Phase 1 fromVocab fix in bpe_class.c) */
    tk_model *m = tk_model_new(zend_hash_num_elements(Z_ARRVAL_P(vocab)));
    zend_string *k; zend_ulong idx; zval *v;
    int vocab_err = 0;
    ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL_P(vocab), idx, k, v) {
        int rc;
        if (k) {
            rc = tk_model_add(m, (const uint8_t*)ZSTR_VAL(k), ZSTR_LEN(k), (uint32_t)zval_get_long(v));
        } else {
            char buf[21];
            int n = snprintf(buf, sizeof(buf), ZEND_ULONG_FMT, idx);
            rc = tk_model_add(m, (const uint8_t*)buf, (size_t)n, (uint32_t)zval_get_long(v));
        }
        if (rc == -1) { vocab_err = 1; break; }
    } ZEND_HASH_FOREACH_END();
    if (vocab_err) { tk_model_free(m); tk_wp_throw("vocab token id out of range"); RETURN_THROWS(); }

    /* Resolve unk_token → its integer id in the model */
    uint32_t unk_id = tk_model_rank(m, (const uint8_t*)unk_token, unk_token_len);
    if (unk_id == TK_RANK_MAX) {
        tk_model_free(m);
        tk_wp_throw("unknown token not in vocab");
        RETURN_THROWS();
    }

    /* Construct the PHP object */
    object_init_ex(return_value, tokenizers_wp_ce);
    tk_wp_obj *o = tk_wp_from(Z_OBJ_P(return_value));
    o->model = m; o->owns = 1;
    /* Own a copy of the prefix string so it survives beyond any zval lifetime */
    o->prefix_buf           = estrndup(prefix_str, prefix_len);
    o->opts.unk_id          = unk_id;
    o->opts.prefix          = o->prefix_buf;
    o->opts.prefix_len      = prefix_len;
    o->opts.max_chars       = (size_t)max_chars;
    o->opts.lowercase       = (int)lowercase;
    o->opts.strip_accents   = (int)strip_accents;
    o->opts.handle_cjk      = (int)handle_cjk;
}

PHP_METHOD(Tokenizers_WordPiece, encode) {
    char *text; size_t text_len;
    ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_STRING(text, text_len) ZEND_PARSE_PARAMETERS_END();
    tk_wp_obj *o = tk_wp_from(Z_OBJ_P(ZEND_THIS));

    uint32_t *ids = NULL; size_t n = 0;
    int rc = tk_wordpiece_encode(o->model, text, text_len, &o->opts, &ids, &n);
    if (rc != 0) { tk_wp_throw("encode failed"); RETURN_THROWS(); }

    array_init_size(return_value, n);
    for (size_t i = 0; i < n; i++) add_next_index_long(return_value, ids[i]);
    if (ids) free(ids);
}

PHP_METHOD(Tokenizers_WordPiece, countTokens) {
    char *text; size_t text_len;
    ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_STRING(text, text_len) ZEND_PARSE_PARAMETERS_END();
    tk_wp_obj *o = tk_wp_from(Z_OBJ_P(ZEND_THIS));

    uint32_t *ids = NULL; size_t n = 0;
    int rc = tk_wordpiece_encode(o->model, text, text_len, &o->opts, &ids, &n);
    if (rc != 0) { tk_wp_throw("encode failed"); RETURN_THROWS(); }
    if (ids) free(ids);
    RETURN_LONG((zend_long)n);
}

PHP_METHOD(Tokenizers_WordPiece, decode) {
    zval *arr;
    ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_ARRAY(arr) ZEND_PARSE_PARAMETERS_END();
    tk_wp_obj *o = tk_wp_from(Z_OBJ_P(ZEND_THIS));

    /* Build output string manually (no smart_str dependency) */
    size_t buf_cap = 64, buf_len = 0;
    char *buf = emalloc(buf_cap);
    int first = 1;
    zval *zv;

    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(arr), zv) {
        uint32_t id = (uint32_t)zval_get_long(zv);
        size_t blen = 0;
        const uint8_t *bytes = tk_model_bytes(o->model, id, &blen);
        if (!bytes || !blen) continue;

        size_t pfx_len = o->opts.prefix_len;
        const char *pfx = o->opts.prefix;

        const char *piece;
        size_t piece_len;
        int add_space;

        if (pfx_len > 0 && blen >= pfx_len && memcmp(bytes, pfx, pfx_len) == 0) {
            /* Continuing subword: strip prefix, append directly (no space) */
            piece     = (const char*)bytes + pfx_len;
            piece_len = blen - pfx_len;
            add_space = 0;
        } else {
            /* Word-start piece: prepend space if not first token */
            piece     = (const char*)bytes;
            piece_len = blen;
            add_space = first ? 0 : 1;
        }

        size_t need = buf_len + (size_t)add_space + piece_len;
        if (need >= buf_cap) {
            while (buf_cap <= need) buf_cap *= 2;
            buf = erealloc(buf, buf_cap);
        }
        if (add_space) buf[buf_len++] = ' ';
        memcpy(buf + buf_len, piece, piece_len);
        buf_len += piece_len;
        first = 0;
    } ZEND_HASH_FOREACH_END();

    RETVAL_STRINGL(buf, buf_len);
    efree(buf);
}

PHP_METHOD(Tokenizers_WordPiece, vocabSize) {
    tk_wp_obj *o = tk_wp_from(Z_OBJ_P(ZEND_THIS));
    RETURN_LONG((zend_long)tk_model_vocab_size(o->model));
}

void tk_register_wp_class(void) {
    tokenizers_wp_ce = register_class_Tokenizers_WordPiece();
    tokenizers_wp_ce->create_object = tk_wp_create;
    memcpy(&tk_wp_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    tk_wp_handlers.offset   = XtOffsetOf(tk_wp_obj, std);
    tk_wp_handlers.free_obj = tk_wp_free;
    tk_wp_handlers.clone_obj = NULL; /* disable cloning; Zend throws "uncloneable" automatically */
}
