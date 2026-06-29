#include <string.h>
#include "php.h"
#include "zend_exceptions.h"
#include "php_tokenizers.h"
#include "tokenizers_arginfo.h"
#include "src/model.h"
#include "src/engine.h"
#include "src/cache.h"
#include "src/loader_tiktoken.h"

zend_class_entry *tokenizers_bpe_ce;
zend_class_entry *tokenizers_exception_ce;

typedef struct { const tk_model *model; int owns; char *name; zend_object std; } tk_bpe_obj;
static inline tk_bpe_obj *tk_bpe_from(zend_object *o){ return (tk_bpe_obj*)((char*)o - XtOffsetOf(tk_bpe_obj, std)); }

static zend_object_handlers tk_bpe_handlers;

static zend_object *tk_bpe_create(zend_class_entry *ce) {
    tk_bpe_obj *o = zend_object_alloc(sizeof(tk_bpe_obj), ce);
    zend_object_std_init(&o->std, ce);
    object_properties_init(&o->std, ce);
    o->std.handlers = &tk_bpe_handlers;
    o->model = NULL; o->owns = 0; o->name = NULL;
    return &o->std;
}
static void tk_bpe_free(zend_object *obj) {
    tk_bpe_obj *o = tk_bpe_from(obj);
    if (o->owns && o->model) tk_model_free((tk_model*)o->model);
    if (o->name) efree(o->name);
    zend_object_std_dtor(&o->std);
}
static void tk_throw(const char *msg) { zend_throw_exception(tokenizers_exception_ce, msg, 0); }

/* loader ctx for cache */
typedef struct {
    const char *path; const char *pattern;
    const char **spec_str; const uint32_t *spec_id; size_t spec_n;
} load_ctx;
static tk_model *cache_load_tiktoken(void *ud, char **err) {
    load_ctx *c = ud;
    tk_model *m = tk_load_tiktoken_file(c->path, c->pattern, err);
    if (!m) return NULL;
    if (tk_model_set_pattern(m, c->pattern) != 0) { tk_model_free(m); if (err){ const char *p = "invalid pre-tokenizer pattern"; char *e = malloc(strlen(p) + 1); if (e) strcpy(e, p); *err = e; } return NULL; }
    for (size_t i = 0; i < c->spec_n; i++)
        tk_model_add_special(m, c->spec_str[i], strlen(c->spec_str[i]), c->spec_id[i]);
    return m;
}

PHP_METHOD(Tokenizers_Bpe, fromTiktokenFile) {
    char *path, *pattern; size_t path_len, pat_len; zval *specials = NULL;
    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_STRING(path, path_len) Z_PARAM_STRING(pattern, pat_len)
        Z_PARAM_OPTIONAL Z_PARAM_ARRAY(specials)
    ZEND_PARSE_PARAMETERS_END();

    /* flatten the special-tokens map (string => int) into parallel arrays */
    const char **sstr = NULL; uint32_t *sid = NULL; size_t sn = 0;
    if (specials) {
        sn = zend_hash_num_elements(Z_ARRVAL_P(specials));
        if (sn) {
            sstr = emalloc(sn * sizeof(char*)); sid = emalloc(sn * sizeof(uint32_t));
            size_t i = 0; zend_string *k; zval *v;
            ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(specials), k, v) {
                if (!k) continue; sstr[i] = ZSTR_VAL(k); sid[i] = (uint32_t)zval_get_long(v); i++;
            } ZEND_HASH_FOREACH_END();
            sn = i;
        }
    }
    /* cache key includes the specials fingerprint so the same file with different
       specials yields distinct models */
    char key[4096]; int koff = snprintf(key, sizeof key, "tiktoken:%s|%s|s%zu", path, pattern, sn);
    for (size_t i = 0; i < sn && koff < (int)sizeof key - 24; i++)
        koff += snprintf(key + koff, sizeof key - koff, ":%s=%u", sstr[i], sid[i]);

    load_ctx lc = { path, pattern, sstr, sid, sn }; char *err = NULL;
    const tk_model *m = tk_cache_get_or_load(key, cache_load_tiktoken, &lc, &err);
    if (sstr) efree(sstr); if (sid) efree(sid);
    if (!m) { tk_throw(err ? err : "failed to load tiktoken file"); if (err) free(err); RETURN_THROWS(); }

    object_init_ex(return_value, tokenizers_bpe_ce);
    tk_bpe_obj *o = tk_bpe_from(Z_OBJ_P(return_value));
    o->model = m; o->owns = 0;
    o->name = estrndup(path, path_len);
}

PHP_METHOD(Tokenizers_Bpe, encode) {
    char *text; size_t text_len; zval *allowed = NULL, *disallowed = NULL;
    ZEND_PARSE_PARAMETERS_START(1, 3)
        Z_PARAM_STRING(text, text_len)
        Z_PARAM_OPTIONAL Z_PARAM_ZVAL(allowed) Z_PARAM_ZVAL(disallowed)
    ZEND_PARSE_PARAMETERS_END();
    tk_bpe_obj *o = tk_bpe_from(Z_OBJ_P(ZEND_THIS));

    /* build allowed[] from array; 'all' string handled by allowing every special */
    const char **alist = NULL; size_t an = 0; int free_alist = 0;
    if (allowed && Z_TYPE_P(allowed) == IS_ARRAY) {
        an = zend_hash_num_elements(Z_ARRVAL_P(allowed));
        alist = emalloc(an * sizeof(char*)); free_alist = 1;
        size_t i = 0; zval *zv;
        ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(allowed), zv) {
            if (Z_TYPE_P(zv) == IS_STRING) alist[i++] = Z_STRVAL_P(zv);
        } ZEND_HASH_FOREACH_END();
        an = i; /* real count of valid string specials; non-strings skipped */
    }
    int disallow_unlisted = 1;
    if (disallowed && Z_TYPE_P(disallowed) == IS_ARRAY) disallow_unlisted = 0; /* explicit empty/list => only listed disallowed; v1 simplification */

    tk_ids ids; tk_ids_init(&ids); char *err = NULL;
    int rc = tk_encode(o->model, (const uint8_t*)text, text_len, alist, an, disallow_unlisted, &ids, &err);
    if (free_alist) efree(alist);
    if (rc != 0) { tk_ids_free(&ids); tk_throw(err ? err : "encode failed"); if (err) free(err); RETURN_THROWS(); }

    array_init_size(return_value, ids.len);
    for (size_t i = 0; i < ids.len; i++) add_next_index_long(return_value, ids.data[i]);
    tk_ids_free(&ids);
}

PHP_METHOD(Tokenizers_Bpe, countTokens) {
    char *text; size_t text_len;
    ZEND_PARSE_PARAMETERS_START(1,1) Z_PARAM_STRING(text, text_len) ZEND_PARSE_PARAMETERS_END();
    tk_bpe_obj *o = tk_bpe_from(Z_OBJ_P(ZEND_THIS));
    RETURN_LONG((zend_long)tk_count(o->model, (const uint8_t*)text, text_len));
}

PHP_METHOD(Tokenizers_Bpe, decode) {
    zval *arr;
    ZEND_PARSE_PARAMETERS_START(1,1) Z_PARAM_ARRAY(arr) ZEND_PARSE_PARAMETERS_END();
    tk_bpe_obj *o = tk_bpe_from(Z_OBJ_P(ZEND_THIS));
    size_t n = zend_hash_num_elements(Z_ARRVAL_P(arr));
    uint32_t *ids = emalloc((n ? n : 1) * sizeof(uint32_t));
    size_t i = 0; zval *zv;
    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(arr), zv) { ids[i++] = (uint32_t)zval_get_long(zv); } ZEND_HASH_FOREACH_END();
    uint8_t *out; size_t olen; char *err = NULL;
    int rc = tk_decode(o->model, ids, n, &out, &olen, &err);
    efree(ids);
    if (rc != 0) { tk_throw(err ? err : "decode failed"); if (err) free(err); RETURN_THROWS(); }
    RETVAL_STRINGL((char*)out, olen); free(out);
}

PHP_METHOD(Tokenizers_Bpe, decodeSingle) {
    zend_long id;
    ZEND_PARSE_PARAMETERS_START(1,1) Z_PARAM_LONG(id) ZEND_PARSE_PARAMETERS_END();
    tk_bpe_obj *o = tk_bpe_from(Z_OBJ_P(ZEND_THIS));
    uint32_t one = (uint32_t)id; uint8_t *out; size_t olen; char *err = NULL;
    if (tk_decode(o->model, &one, 1, &out, &olen, &err) != 0) { tk_throw(err?err:"bad id"); if(err)free(err); RETURN_THROWS(); }
    RETVAL_STRINGL((char*)out, olen); free(out);
}

PHP_METHOD(Tokenizers_Bpe, vocabSize) {
    tk_bpe_obj *o = tk_bpe_from(Z_OBJ_P(ZEND_THIS));
    RETURN_LONG((zend_long)tk_model_vocab_size(o->model));
}
PHP_METHOD(Tokenizers_Bpe, name) {
    tk_bpe_obj *o = tk_bpe_from(Z_OBJ_P(ZEND_THIS));
    if (o->name) RETURN_STRING(o->name); RETURN_NULL();
}

PHP_METHOD(Tokenizers_Bpe, fromVocab) {
    zval *vocab, *merges = NULL, *specials = NULL; char *pattern; size_t pat_len;
    ZEND_PARSE_PARAMETERS_START(3, 4)
        Z_PARAM_ARRAY(vocab) Z_PARAM_ARRAY(merges) Z_PARAM_STRING(pattern, pat_len)
        Z_PARAM_OPTIONAL Z_PARAM_ARRAY(specials)
    ZEND_PARSE_PARAMETERS_END();
    (void)merges; /* v1: id order encodes merge priority */

    tk_model *m = tk_model_new(zend_hash_num_elements(Z_ARRVAL_P(vocab)));
    zend_string *k; zend_ulong idx; zval *v;
    ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL_P(vocab), idx, k, v) {
        if (k) {
            tk_model_add(m, (const uint8_t*)ZSTR_VAL(k), ZSTR_LEN(k), (uint32_t)zval_get_long(v));
        } else {
            char buf[21];
            int n = snprintf(buf, sizeof(buf), ZEND_ULONG_FMT, idx);
            tk_model_add(m, (const uint8_t*)buf, (size_t)n, (uint32_t)zval_get_long(v));
        }
    } ZEND_HASH_FOREACH_END();

    if (tk_model_set_pattern(m, pattern) != 0) { tk_model_free(m); tk_throw("invalid pre-tokenizer pattern"); RETURN_THROWS(); }
    if (specials) {
        zend_string *sk; zval *sv;
        ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(specials), sk, sv) {
            if (!sk) continue;
            tk_model_add_special(m, ZSTR_VAL(sk), ZSTR_LEN(sk), (uint32_t)zval_get_long(sv));
        } ZEND_HASH_FOREACH_END();
    }

    object_init_ex(return_value, tokenizers_bpe_ce);
    tk_bpe_obj *o = tk_bpe_from(Z_OBJ_P(return_value));
    o->model = m; o->owns = 1; o->name = NULL;
}

/* procedural API helpers */
static const tk_model *bpe_model_arg(zval *z) {
    return tk_bpe_from(Z_OBJ_P(z))->model;
}
PHP_FUNCTION(tokenizers_count) {
    zval *obj; char *text; size_t tlen;
    ZEND_PARSE_PARAMETERS_START(2,2) Z_PARAM_OBJECT_OF_CLASS(obj, tokenizers_bpe_ce) Z_PARAM_STRING(text, tlen) ZEND_PARSE_PARAMETERS_END();
    RETURN_LONG((zend_long)tk_count(bpe_model_arg(obj), (const uint8_t*)text, tlen));
}
PHP_FUNCTION(tokenizers_encode) {
    zval *obj; char *text; size_t tlen; zval *allowed = NULL, *disallowed = NULL;
    ZEND_PARSE_PARAMETERS_START(2,4) Z_PARAM_OBJECT_OF_CLASS(obj, tokenizers_bpe_ce) Z_PARAM_STRING(text, tlen)
        Z_PARAM_OPTIONAL Z_PARAM_ZVAL(allowed) Z_PARAM_ZVAL(disallowed) ZEND_PARSE_PARAMETERS_END();
    const char **alist = NULL; size_t an = 0; int freea = 0;
    if (allowed && Z_TYPE_P(allowed) == IS_ARRAY) { an = zend_hash_num_elements(Z_ARRVAL_P(allowed));
        alist = emalloc(an*sizeof(char*)); freea = 1; size_t i=0; zval *zv;
        ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(allowed), zv){
            if (Z_TYPE_P(zv) == IS_STRING) alist[i++]=Z_STRVAL_P(zv);
        } ZEND_HASH_FOREACH_END();
        an = i; /* real count of valid string specials; non-strings skipped */ }
    int dis = (disallowed && Z_TYPE_P(disallowed) == IS_ARRAY) ? 0 : 1;
    tk_ids ids; tk_ids_init(&ids); char *err=NULL;
    int rc = tk_encode(bpe_model_arg(obj), (const uint8_t*)text, tlen, alist, an, dis, &ids, &err);
    if (freea) efree(alist);
    if (rc != 0) { tk_ids_free(&ids); zend_throw_exception(tokenizers_exception_ce, err?err:"encode failed", 0); if(err)free(err); RETURN_THROWS(); }
    array_init_size(return_value, ids.len);
    for (size_t i=0;i<ids.len;i++) add_next_index_long(return_value, ids.data[i]);
    tk_ids_free(&ids);
}
PHP_FUNCTION(tokenizers_decode) {
    zval *obj, *arr;
    ZEND_PARSE_PARAMETERS_START(2,2) Z_PARAM_OBJECT_OF_CLASS(obj, tokenizers_bpe_ce) Z_PARAM_ARRAY(arr) ZEND_PARSE_PARAMETERS_END();
    size_t n = zend_hash_num_elements(Z_ARRVAL_P(arr)); uint32_t *ids = emalloc((n?n:1)*sizeof(uint32_t));
    size_t i=0; zval *zv; ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(arr), zv){ ids[i++]=(uint32_t)zval_get_long(zv); } ZEND_HASH_FOREACH_END();
    uint8_t *out; size_t olen; char *err=NULL; int rc = tk_decode(bpe_model_arg(obj), ids, n, &out, &olen, &err); efree(ids);
    if (rc != 0) { zend_throw_exception(tokenizers_exception_ce, err?err:"decode failed", 0); if(err)free(err); RETURN_THROWS(); }
    RETVAL_STRINGL((char*)out, olen); free(out);
}

PHP_FUNCTION(tokenizers_cache_count) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG((zend_long)tk_cache_count()); }

void tk_register_bpe_class(void) {
    tokenizers_exception_ce = register_class_Tokenizers_TokenizerException(zend_ce_exception);
    tokenizers_bpe_ce = register_class_Tokenizers_Bpe();
    tokenizers_bpe_ce->create_object = tk_bpe_create;
    memcpy(&tk_bpe_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    tk_bpe_handlers.offset = XtOffsetOf(tk_bpe_obj, std);
    tk_bpe_handlers.free_obj = tk_bpe_free;
    tk_bpe_handlers.clone_obj = NULL; /* disable cloning; Zend throws "uncloneable" automatically */
}
