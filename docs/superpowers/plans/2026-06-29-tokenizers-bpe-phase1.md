# `tokenizers` Extension — Phase 1 (Native BPE Engine) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a PHP 8.3+ PECL extension named `tokenizers` that performs byte-level BPE tokenization, byte-exact with the Python `tiktoken` reference for `cl100k_base` and `o200k_base`, and also loads HuggingFace `tokenizer.json` BPE models.

**Architecture:** A single C BPE engine operates on a format-agnostic internal model (merge-rank map + vocab + special tokens + compiled PCRE2 pre-tokenizer). Two loaders populate that model: a C tiktoken-file parser and a PHP-side `tokenizer.json` parser that hands extracted arrays to C. Parsed models live in a process-global, immutable-after-load cache shared across all requests/threads in a worker.

**Tech Stack:** C (PHP/Zend extension API), PHP's bundled PCRE2 (UTF + UCP + JIT), PHP 8.3/8.4, PECL/PIE packaging. Dev-only: system `libpcre2-8` and `clang` for C unit tests; Python `tiktoken`/`transformers` to generate committed conformance fixtures.

## Global Constraints

- **PHP version floor:** 8.3. Must build and pass on PHP 8.3 and 8.4.
- **Thread safety:** must build and pass under both NTS and ZTS.
- **No new runtime dependencies:** use PHP's bundled PCRE2; do **not** link a second regex engine. No Rust toolchain. The HF `tokenizer.json` parser uses PHP's native `json_decode` in the bundled shim (no JSON parser in C).
- **Extension name:** `tokenizers`. PHP namespace: `\Tokenizers\`. Primary class: `\Tokenizers\Bpe`. Exception: `\Tokenizers\TokenizerException` (extends `\RuntimeException`).
- **Never redistribute vocab files:** built-in encodings are downloaded + cached on first use by the PHP shim from their official URLs and checksum-verified.
- **Conformance is a release blocker:** any byte-level diff vs the committed reference fixtures fails the build.
- **Worst-case complexity:** tokenization of a single pre-token piece must be O(n log n), not O(n²).
- **License:** Apache-2.0 (matches the broader PHP/PECL ecosystem and the permissive vocab-download model).

## Refinements from the spec (intentional, please flag if unwanted)

To keep JSON parsing out of C and keep the public API clean, the constructors are split by who can implement them:

- `\Tokenizers\Bpe` (C class) exposes the **primitive** constructors that need C:
  - `Bpe::fromTiktokenFile(string $path, string $pattern, array $specialTokens = []): Bpe`
  - `Bpe::fromVocab(array $tokenBytesToId, array $merges, string $pattern, array $specialTokens = []): Bpe`
- `\Tokenizers\Encoding` (bundled PHP shim) exposes the **convenience** resolvers from the spec:
  - `Encoding::load(string $name): Bpe` — download/cache a known encoding, then delegate to `Bpe::fromTiktokenFile`.
  - `Encoding::fromHuggingFace(string $jsonPath): Bpe` — `json_decode` + extract, then delegate to `Bpe::fromVocab`.
- Procedural helpers operate on a `Bpe` instance (avoids C→PHP→network coupling): `tokenizers_encode(Bpe $t, string $text, ...)`, `tokenizers_decode(Bpe $t, array $ids)`, `tokenizers_count(Bpe $t, string $text)`.

All spec capabilities are preserved; only the placement of name-resolution moves from the C class to the PHP shim.

## File Structure

```
php-tokenizers/
├── config.m4                     # *nix build: detect PCRE2 from PHP, list sources
├── config.w32                    # Windows build
├── php_tokenizers.h              # module header: globals, version, decls
├── tokenizers.c                  # MINIT/MSHUTDOWN/MINFO, module entry, registration
├── tokenizers.stub.php           # arginfo source → tokenizers_arginfo.h (generated)
├── src/
│   ├── model.h  / model.c        # tk_model: rank hashmap, id↔bytes vocab, specials, pcre2_code*
│   ├── base64.h / base64.c       # base64 decode (tiktoken vocab lines)
│   ├── heap.h   / heap.c         # binary min-heap of (rank,pos) for the merge
│   ├── engine.h / engine.c       # pre-tokenize + heap BPE merge + encode/decode/count
│   ├── loader_tiktoken.h / .c    # parse .tiktoken file → tk_model
│   ├── cache.h  / cache.c        # process-global model cache (mutex load, lock-free read)
│   └── bpe_class.c               # \Tokenizers\Bpe registration + method glue
├── php/
│   └── Tokenizers/Encoding.php   # shim: known-encoding download/cache + HF json loader
├── tests/
│   ├── cunit/                    # standalone C unit tests (clang-compiled)
│   │   └── run.sh
│   ├── reference/
│   │   ├── generate_fixtures.py  # uses tiktoken/transformers; output committed
│   │   └── fixtures/*.json
│   ├── fixtures/                 # tiny hand-made vocabs for fast tests
│   └── *.phpt
├── .github/workflows/ci.yml
├── package.xml                   # PECL
├── LICENSE                       # Apache-2.0
└── README.md
```

## Dev & Test Conventions (read before Task 1)

- **PHP-visible behavior** is tested with `.phpt` files run via `make test` (`TEST_PHP_EXECUTABLE=$(which php) php run-tests.php -p $(which php) tests/foo.phpt`).
- **Pure-C units** (model, base64, heap, engine, tiktoken loader) are tested with standalone C programs under `tests/cunit/`, compiled directly with clang. This gives a real red/green cycle independent of the Zend runtime. They link system `libpcre2-8` (install once: `brew install pcre2`). The shipped extension instead uses PHP's bundled PCRE2 — same API, no extra link.
- `tests/cunit/run.sh <name>` compiles `tests/cunit/test_<name>.c` plus the sources it needs and runs it.
- After each task: rebuild (`phpize && ./configure && make`) and run that task's tests before committing.
- Commit after every green step group as indicated.

---

## Task 1: Build scaffold that compiles and loads

**Files:**
- Create: `config.m4`, `config.w32`, `php_tokenizers.h`, `tokenizers.c`, `tokenizers.stub.php`, `LICENSE`
- Test: `tests/001-load.phpt`

**Interfaces:**
- Produces: a loadable extension exposing constant `Tokenizers\VERSION` (string) and function `tokenizers_version(): string`.

- [ ] **Step 1: Write the failing test**

`tests/001-load.phpt`:
```
--TEST--
Extension loads and reports its version
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip extension not built'; ?>
--FILE--
<?php
var_dump(extension_loaded('tokenizers'));
var_dump(tokenizers_version() === \Tokenizers\VERSION);
echo \Tokenizers\VERSION, "\n";
?>
--EXPECT--
bool(true)
bool(true)
0.1.0
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `cd ~/projects/php-tokenizers && php tests/001-load.phpt 2>&1 || true`
Expected: fails / `extension not built` — nothing is built yet.

- [ ] **Step 3: Write the build files and minimal module**

`config.m4`:
```m4
PHP_ARG_ENABLE([tokenizers],
  [whether to enable tokenizers support],
  [AS_HELP_STRING([--enable-tokenizers], [Enable tokenizers])], [no])

if test "$PHP_TOKENIZERS" != "no"; then
  AC_DEFINE(HAVE_TOKENIZERS, 1, [ Have tokenizers ])
  PHP_NEW_EXTENSION(tokenizers,
    [tokenizers.c src/model.c src/base64.c src/heap.c src/engine.c src/loader_tiktoken.c src/cache.c src/bpe_class.c],
    $ext_shared,, [-DZEND_ENABLE_STATIC_TSRMLS_CACHE=1])
  PHP_ADD_BUILD_DIR([$ext_builddir/src])
  PHP_ADD_EXTENSION_DEP(tokenizers, json)
  PHP_ADD_EXTENSION_DEP(tokenizers, pcre)
fi
```

`config.w32`:
```js
ARG_ENABLE("tokenizers", "Enable tokenizers", "no");
if (PHP_TOKENIZERS != "no") {
  EXTENSION("tokenizers", "tokenizers.c src/model.c src/base64.c src/heap.c src/engine.c src/loader_tiktoken.c src/cache.c src/bpe_class.c");
  ADD_FLAG("CFLAGS_TOKENIZERS", "/I " + configure_module_dirname + "\\src");
  ADD_EXTENSION_DEP("tokenizers", "json");
  ADD_EXTENSION_DEP("tokenizers", "pcre");
}
```

`php_tokenizers.h`:
```c
#ifndef PHP_TOKENIZERS_H
#define PHP_TOKENIZERS_H

extern zend_module_entry tokenizers_module_entry;
#define phpext_tokenizers_ptr &tokenizers_module_entry

#define PHP_TOKENIZERS_VERSION "0.1.0"

#if defined(ZTS) && defined(COMPILE_DL_TOKENIZERS)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#endif /* PHP_TOKENIZERS_H */
```

`tokenizers.stub.php`:
```php
<?php
/** @generate-class-entries */

namespace Tokenizers {
    /** @var string */
    const VERSION = "0.1.0";
}

namespace {
    function tokenizers_version(): string {}
}
```

`tokenizers.c` (minimal; class registration added in later tasks):
```c
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "php.h"
#include "ext/standard/info.h"
#include "php_tokenizers.h"
#include "tokenizers_arginfo.h"

PHP_FUNCTION(tokenizers_version)
{
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_STRING(PHP_TOKENIZERS_VERSION);
}

PHP_MINFO_FUNCTION(tokenizers)
{
    php_info_print_table_start();
    php_info_print_table_row(2, "tokenizers support", "enabled");
    php_info_print_table_row(2, "version", PHP_TOKENIZERS_VERSION);
    php_info_print_table_end();
}

PHP_MINIT_FUNCTION(tokenizers)
{
    REGISTER_STRING_CONSTANT("Tokenizers\\VERSION", PHP_TOKENIZERS_VERSION, CONST_PERSISTENT);
    return SUCCESS;
}

zend_module_entry tokenizers_module_entry = {
    STANDARD_MODULE_HEADER,
    "tokenizers",
    ext_functions,            /* from arginfo */
    PHP_MINIT(tokenizers),
    NULL, NULL, NULL,
    PHP_MINFO(tokenizers),
    PHP_TOKENIZERS_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_TOKENIZERS
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(tokenizers)
#endif
```

`LICENSE`: the standard Apache-2.0 text (fetch from https://www.apache.org/licenses/LICENSE-2.0.txt and save verbatim).

- [ ] **Step 4: Generate arginfo, build, and create empty src files so it links**

Create empty stubs so the listed sources compile: `src/model.c`, `src/base64.c`, `src/heap.c`, `src/engine.c`, `src/loader_tiktoken.c`, `src/cache.c`, `src/bpe_class.c` each containing only `#include "php.h"` for now.

Run:
```bash
cd ~/projects/php-tokenizers
php $(dirname $(which php))/../share/php/build/gen_stub.php tokenizers.stub.php 2>/dev/null \
  || php tokenizers.stub.php  # if gen_stub unavailable, hand-write tokenizers_arginfo.h (below)
phpize && ./configure --enable-tokenizers && make -j4
```
If `gen_stub.php` isn't found, hand-write `tokenizers_arginfo.h`:
```c
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tokenizers_version, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()
ZEND_FUNCTION(tokenizers_version);
static const zend_function_entry ext_functions[] = {
    ZEND_FE(tokenizers_version, arginfo_tokenizers_version)
    ZEND_FE_END
};
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `php -d extension=$(pwd)/modules/tokenizers.so tests/001-load.phpt` then
`php -d extension=$(pwd)/modules/tokenizers.so -r 'var_dump(extension_loaded("tokenizers"));'`
Expected: `bool(true)`; the `.phpt` `--EXPECT--` block matches.

- [ ] **Step 6: Commit**

```bash
git add config.m4 config.w32 php_tokenizers.h tokenizers.c tokenizers.stub.php tokenizers_arginfo.h src LICENSE tests/001-load.phpt
git commit -m "Build scaffold: loadable tokenizers extension with version"
```

---

## Task 2: Internal model with a merge-rank hashmap

**Files:**
- Create: `src/model.h`, `src/model.c`, `tests/cunit/test_model.c`, `tests/cunit/run.sh`

**Interfaces:**
- Produces:
  - `typedef struct tk_model tk_model;`
  - `tk_model *tk_model_new(uint32_t vocab_hint);`
  - `void tk_model_free(tk_model *m);`
  - `int tk_model_add(tk_model *m, const uint8_t *bytes, size_t len, uint32_t id);` — inserts a token's bytes↔id and registers its rank (rank == id for BPE). Returns 0 on success.
  - `uint32_t tk_model_rank(const tk_model *m, const uint8_t *bytes, size_t len);` — returns the id/rank, or `TK_RANK_MAX` (`UINT32_MAX`) if absent.
  - `const uint8_t *tk_model_bytes(const tk_model *m, uint32_t id, size_t *len_out);` — bytes for an id, or NULL.
  - `uint32_t tk_model_vocab_size(const tk_model *m);`

- [ ] **Step 1: Write the failing C unit test**

`tests/cunit/test_model.c`:
```c
#include "model.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    tk_model *m = tk_model_new(8);
    assert(tk_model_add(m, (const uint8_t*)"a", 1, 10) == 0);
    assert(tk_model_add(m, (const uint8_t*)"b", 1, 11) == 0);
    assert(tk_model_add(m, (const uint8_t*)"ab", 2, 42) == 0);

    assert(tk_model_rank(m, (const uint8_t*)"ab", 2) == 42);
    assert(tk_model_rank(m, (const uint8_t*)"a", 1) == 10);
    assert(tk_model_rank(m, (const uint8_t*)"zz", 2) == 0xFFFFFFFFu); /* absent */

    size_t n = 0;
    const uint8_t *b = tk_model_bytes(m, 42, &n);
    assert(n == 2 && memcmp(b, "ab", 2) == 0);
    assert(tk_model_vocab_size(m) == 3);

    tk_model_free(m);
    printf("test_model OK\n");
    return 0;
}
```

`tests/cunit/run.sh`:
```bash
#!/usr/bin/env bash
set -euo pipefail
name="$1"; shift
root="$(cd "$(dirname "$0")/../.." && pwd)"
pcre2_cflags="$(pkg-config --cflags libpcre2-8 2>/dev/null || echo)"
pcre2_libs="$(pkg-config --libs libpcre2-8 2>/dev/null || echo -lpcre2-8)"
clang -std=c11 -g -O1 -Wall -Wextra -I"$root/src" $pcre2_cflags \
  "$root/tests/cunit/test_${name}.c" "$@" $pcre2_libs -o "/tmp/tk_test_${name}"
"/tmp/tk_test_${name}"
```
`chmod +x tests/cunit/run.sh`

- [ ] **Step 2: Run it to confirm it fails**

Run: `tests/cunit/run.sh model src/model.c`
Expected: compile error (`model.h` not found / functions undefined).

- [ ] **Step 3: Implement the model**

`src/model.h`:
```c
#ifndef TK_MODEL_H
#define TK_MODEL_H
#include <stddef.h>
#include <stdint.h>

#define TK_RANK_MAX 0xFFFFFFFFu

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
```

`src/model.c` (open-addressing hashmap keyed by byte-string → id; reverse array id → bytes; FNV-1a hash):
```c
#include "model.h"
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *bytes; uint32_t len; uint32_t id; uint8_t used; } slot;
typedef struct { uint8_t *s; uint32_t len; uint32_t id; } special;

struct tk_model {
    slot *tab; uint32_t cap; uint32_t count;          /* bytes->id */
    uint8_t **id_bytes; uint32_t *id_len; uint32_t id_cap; /* id->bytes */
    special *specials; size_t spec_count, spec_cap;
    void *pcre2;                                       /* pcre2_code* */
    void (*pcre2_free)(void*);                          /* set by pretok when compiling */
    char *pattern;
};

static uint64_t fnv1a(const uint8_t *b, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}
static void tab_insert(slot *tab, uint32_t cap, const uint8_t *b, uint32_t len, uint32_t id) {
    uint32_t i = (uint32_t)(fnv1a(b, len) & (cap - 1));
    while (tab[i].used) i = (i + 1) & (cap - 1);
    tab[i].bytes = (uint8_t*)malloc(len ? len : 1);
    memcpy(tab[i].bytes, b, len);
    tab[i].len = len; tab[i].id = id; tab[i].used = 1;
}
static uint32_t next_pow2(uint32_t x){ uint32_t p=8; while(p<x) p<<=1; return p; }

tk_model *tk_model_new(uint32_t vocab_hint) {
    tk_model *m = calloc(1, sizeof *m);
    m->cap = next_pow2(vocab_hint ? vocab_hint * 2 : 16);
    m->tab = calloc(m->cap, sizeof(slot));
    m->id_cap = vocab_hint ? vocab_hint : 16;
    m->id_bytes = calloc(m->id_cap, sizeof(uint8_t*));
    m->id_len = calloc(m->id_cap, sizeof(uint32_t));
    return m;
}
static void rehash(tk_model *m) {
    uint32_t ncap = m->cap << 1;
    slot *nt = calloc(ncap, sizeof(slot));
    for (uint32_t i = 0; i < m->cap; i++)
        if (m->tab[i].used) tab_insert(nt, ncap, m->tab[i].bytes, m->tab[i].len, m->tab[i].id);
    for (uint32_t i = 0; i < m->cap; i++) if (m->tab[i].used) free(m->tab[i].bytes);
    free(m->tab); m->tab = nt; m->cap = ncap;
}
int tk_model_add(tk_model *m, const uint8_t *bytes, size_t len, uint32_t id) {
    if ((m->count + 1) * 4 >= m->cap * 3) rehash(m);
    tab_insert(m->tab, m->cap, bytes, (uint32_t)len, id);
    m->count++;
    if (id >= m->id_cap) {
        uint32_t nc = m->id_cap; while (nc <= id) nc <<= 1;
        m->id_bytes = realloc(m->id_bytes, nc * sizeof(uint8_t*));
        m->id_len = realloc(m->id_len, nc * sizeof(uint32_t));
        memset(m->id_bytes + m->id_cap, 0, (nc - m->id_cap) * sizeof(uint8_t*));
        m->id_cap = nc;
    }
    m->id_bytes[id] = malloc(len ? len : 1); memcpy(m->id_bytes[id], bytes, len);
    m->id_len[id] = (uint32_t)len;
    return 0;
}
uint32_t tk_model_rank(const tk_model *m, const uint8_t *bytes, size_t len) {
    uint32_t i = (uint32_t)(fnv1a(bytes, len) & (m->cap - 1));
    while (m->tab[i].used) {
        if (m->tab[i].len == len && memcmp(m->tab[i].bytes, bytes, len) == 0) return m->tab[i].id;
        i = (i + 1) & (m->cap - 1);
    }
    return TK_RANK_MAX;
}
const uint8_t *tk_model_bytes(const tk_model *m, uint32_t id, size_t *len_out) {
    if (id >= m->id_cap || !m->id_bytes[id]) return NULL;
    if (len_out) *len_out = m->id_len[id];
    return m->id_bytes[id];
}
uint32_t tk_model_vocab_size(const tk_model *m) { return m->count; }

int tk_model_add_special(tk_model *m, const char *s, size_t len, uint32_t id) {
    if (m->spec_count == m->spec_cap) {
        m->spec_cap = m->spec_cap ? m->spec_cap * 2 : 8;
        m->specials = realloc(m->specials, m->spec_cap * sizeof(special));
    }
    special *sp = &m->specials[m->spec_count++];
    sp->s = malloc(len); memcpy(sp->s, s, len); sp->len = (uint32_t)len; sp->id = id;
    if (id < m->id_cap) { /* allow decode of specials too */ }
    return 0;
}
uint32_t tk_model_special_id(const tk_model *m, const uint8_t *s, size_t len) {
    for (size_t i = 0; i < m->spec_count; i++)
        if (m->specials[i].len == len && memcmp(m->specials[i].s, s, len) == 0) return m->specials[i].id;
    return TK_RANK_MAX;
}
size_t tk_model_special_count(const tk_model *m) { return m->spec_count; }
const char *tk_model_special_at(const tk_model *m, size_t i, size_t *len_out, uint32_t *id_out) {
    if (i >= m->spec_count) return NULL;
    if (len_out) *len_out = m->specials[i].len;
    if (id_out) *id_out = m->specials[i].id;
    return (const char*)m->specials[i].s;
}

/* pattern setters defined in Task 5 (pretok) to keep PCRE2 there; declared weakly here */
void tk_model_free(tk_model *m) {
    if (!m) return;
    for (uint32_t i = 0; i < m->cap; i++) if (m->tab[i].used) free(m->tab[i].bytes);
    free(m->tab);
    for (uint32_t i = 0; i < m->id_cap; i++) free(m->id_bytes[i]);
    free(m->id_bytes); free(m->id_len);
    for (size_t i = 0; i < m->spec_count; i++) free(m->specials[i].s);
    free(m->specials);
    if (m->pcre2 && m->pcre2_free) m->pcre2_free(m->pcre2);
    free(m->pattern);
    free(m);
}
/* expose struct internals to pretok.c via accessors */
void tk_model__set_pcre2(tk_model *m, void *code, void (*freefn)(void*)) {
    if (m->pcre2 && m->pcre2_free) m->pcre2_free(m->pcre2);
    m->pcre2 = code; m->pcre2_free = freefn;
}
char **tk_model__pattern_slot(tk_model *m) { return &m->pattern; }
const void *tk_model_pattern_code(const tk_model *m) { return m->pcre2; }
```

> Note: `tk_model_set_pattern` is implemented in Task 5 (it needs PCRE2). For Task 2 the cunit test does not touch patterns, so leave `tk_model_set_pattern` undeclared-as-defined until Task 5 — the test links only `model.c`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `tests/cunit/run.sh model src/model.c`
Expected: `test_model OK`.

- [ ] **Step 5: Commit**

```bash
git add src/model.h src/model.c tests/cunit/test_model.c tests/cunit/run.sh
git commit -m "Add internal model: merge-rank hashmap + id<->bytes vocab + specials"
```

---

## Task 3: base64 decoder for tiktoken vocab lines

**Files:**
- Create: `src/base64.h`, `src/base64.c`, `tests/cunit/test_base64.c`

**Interfaces:**
- Produces: `int tk_b64_decode(const char *in, size_t in_len, uint8_t *out, size_t *out_len);` — standard base64 (RFC 4648, with `=` padding). Writes decoded bytes to `out` (caller sizes it `>= in_len/4*3`); sets `*out_len`. Returns 0 on success, -1 on invalid input.

- [ ] **Step 1: Write the failing C unit test**

`tests/cunit/test_base64.c`:
```c
#include "base64.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void chk(const char *b64, const char *expect, size_t elen) {
    uint8_t out[256]; size_t n = 0;
    assert(tk_b64_decode(b64, strlen(b64), out, &n) == 0);
    assert(n == elen && memcmp(out, expect, elen) == 0);
}
int main(void) {
    chk("IQ==", "!", 1);          /* 0x21 */
    chk("aGVsbG8=", "hello", 5);
    chk("AA==", "\x00", 1);
    chk("", "", 0);
    uint8_t out[8]; size_t n;
    assert(tk_b64_decode("!!", 2, out, &n) == -1); /* invalid */
    printf("test_base64 OK\n");
    return 0;
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `tests/cunit/run.sh base64 src/base64.c`
Expected: compile error (header/functions missing).

- [ ] **Step 3: Implement base64 decode**

`src/base64.h`:
```c
#ifndef TK_BASE64_H
#define TK_BASE64_H
#include <stddef.h>
#include <stdint.h>
int tk_b64_decode(const char *in, size_t in_len, uint8_t *out, size_t *out_len);
#endif
```

`src/base64.c`:
```c
#include "base64.h"
static int8_t dec_tab(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
int tk_b64_decode(const char *in, size_t in_len, uint8_t *out, size_t *out_len) {
    while (in_len > 0 && in[in_len - 1] == '=') in_len--;
    size_t o = 0; uint32_t acc = 0; int bits = 0;
    for (size_t i = 0; i < in_len; i++) {
        int8_t v = dec_tab((unsigned char)in[i]);
        if (v < 0) return -1;
        acc = (acc << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; out[o++] = (uint8_t)((acc >> bits) & 0xFF); }
    }
    *out_len = o;
    return 0;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `tests/cunit/run.sh base64 src/base64.c`
Expected: `test_base64 OK`.

- [ ] **Step 5: Commit**

```bash
git add src/base64.h src/base64.c tests/cunit/test_base64.c
git commit -m "Add base64 decoder for tiktoken vocab parsing"
```

---

## Task 4: tiktoken-file loader

**Files:**
- Create: `src/loader_tiktoken.h`, `src/loader_tiktoken.c`, `tests/fixtures/mini.tiktoken`, `tests/cunit/test_loader_tiktoken.c`

**Interfaces:**
- Consumes: `tk_model` (Task 2), `tk_b64_decode` (Task 3).
- Produces: `tk_model *tk_load_tiktoken_file(const char *path, const char *pattern, char **err);` — parses a `.tiktoken` file (each line: `base64(token) SPACE rank`), builds and returns a `tk_model` with `pattern` stored (compiled lazily in Task 5; for Task 4 the pattern is stored as a string only). On error returns NULL and sets `*err` to a malloc'd message.

- [ ] **Step 1: Write the failing C unit test + fixture**

`tests/fixtures/mini.tiktoken` (base64 of `a`,`b`,`c`,`ab`,`bc` with ranks 0..4):
```
YQ== 0
Yg== 1
Yw== 2
YWI= 3
YmM= 4
```

`tests/cunit/test_loader_tiktoken.c`:
```c
#include "model.h"
#include "loader_tiktoken.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    char *err = NULL;
    tk_model *m = tk_load_tiktoken_file("tests/fixtures/mini.tiktoken", "x", &err);
    assert(m != NULL && err == NULL);
    assert(tk_model_rank(m, (const uint8_t*)"a", 1) == 0);
    assert(tk_model_rank(m, (const uint8_t*)"ab", 2) == 3);
    assert(tk_model_rank(m, (const uint8_t*)"bc", 2) == 4);
    assert(tk_model_vocab_size(m) == 5);
    tk_model_free(m);

    tk_model *bad = tk_load_tiktoken_file("tests/fixtures/does-not-exist", "x", &err);
    assert(bad == NULL && err != NULL);
    printf("test_loader_tiktoken OK\n");
    return 0;
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `tests/cunit/run.sh loader_tiktoken src/loader_tiktoken.c src/model.c src/base64.c`
Expected: compile error (loader header/functions missing).

- [ ] **Step 3: Implement the loader**

`src/loader_tiktoken.h`:
```c
#ifndef TK_LOADER_TIKTOKEN_H
#define TK_LOADER_TIKTOKEN_H
#include "model.h"
tk_model *tk_load_tiktoken_file(const char *path, const char *pattern, char **err);
#endif
```

`src/loader_tiktoken.c`:
```c
#include "loader_tiktoken.h"
#include "base64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_err(const char *s){ char *p=malloc(strlen(s)+1); strcpy(p,s); return p; }

tk_model *tk_load_tiktoken_file(const char *path, const char *pattern, char **err) {
    if (err) *err = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) { if (err) *err = dup_err("cannot open tiktoken file"); return NULL; }
    tk_model *m = tk_model_new(100000);
    char *line = NULL; size_t cap = 0; ssize_t n;
    uint8_t buf[512];
    while ((n = getline(&line, &cap, f)) != -1) {
        if (n == 0) continue;
        char *sp = memchr(line, ' ', (size_t)n);
        if (!sp) continue;
        size_t b64len = (size_t)(sp - line);
        size_t outlen = 0;
        if (tk_b64_decode(line, b64len, buf, &outlen) != 0) {
            free(line); fclose(f); tk_model_free(m);
            if (err) *err = dup_err("invalid base64 in tiktoken file"); return NULL;
        }
        uint32_t rank = (uint32_t)strtoul(sp + 1, NULL, 10);
        tk_model_add(m, buf, outlen, rank);
    }
    free(line); fclose(f);
    /* store pattern string on the model; compiled in Task 5 */
    extern int tk_model_set_pattern_str(tk_model *, const char *);
    tk_model_set_pattern_str(m, pattern);
    return m;
}
```

Add the tiny string-only setter to `src/model.c` (compilation needs it now; PCRE2 compile comes in Task 5):
```c
int tk_model_set_pattern_str(tk_model *m, const char *pattern) {
    char **slot = tk_model__pattern_slot(m);
    free(*slot);
    *slot = malloc(strlen(pattern) + 1); strcpy(*slot, pattern);
    return 0;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `tests/cunit/run.sh loader_tiktoken src/loader_tiktoken.c src/model.c src/base64.c`
Expected: `test_loader_tiktoken OK`.

- [ ] **Step 5: Commit**

```bash
git add src/loader_tiktoken.h src/loader_tiktoken.c tests/fixtures/mini.tiktoken tests/cunit/test_loader_tiktoken.c src/model.c
git commit -m "Add tiktoken-file loader building the internal model"
```

---

## Task 5: PCRE2 pre-tokenizer

**Files:**
- Create: `src/pretok.h`, `src/pretok.c`, `tests/cunit/test_pretok.c`
- Modify: `config.m4` (add `src/pretok.c` to sources), `config.w32` (same)

**Interfaces:**
- Consumes: `tk_model` (Task 2), its `tk_model__set_pcre2` / `tk_model__pattern_slot` accessors.
- Produces:
  - `int tk_model_set_pattern(tk_model *m, const char *pattern);` — compiles `pattern` with PCRE2 (`PCRE2_UTF | PCRE2_UCP`), JIT-compiles it, stores `pcre2_code*` on the model via `tk_model__set_pcre2`. Returns 0 on success, -1 on compile error.
  - `int tk_pretok_split(const tk_model *m, const uint8_t *text, size_t len, void (*emit)(void *ud, size_t start, size_t end), void *ud);` — runs the compiled pattern as a global match, invoking `emit` once per matched piece. Returns 0, or -1 if no pattern is compiled.

- [ ] **Step 1: Write the failing C unit test**

`tests/cunit/test_pretok.c`:
```c
#include "model.h"
#include "pretok.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static char pieces[16][32]; static int np;
static void emit(void *ud, size_t s, size_t e) {
    (void)ud; const uint8_t *t = (const uint8_t*)"ab cd";
    memcpy(pieces[np], t + s, e - s); pieces[np][e - s] = 0; np++;
}
int main(void) {
    tk_model *m = tk_model_new(8);
    /* simple pre-tokenizer: words or runs of spaces */
    assert(tk_model_set_pattern(m, "\\w+| +") == 0);
    np = 0;
    assert(tk_pretok_split(m, (const uint8_t*)"ab cd", 5, emit, NULL) == 0);
    assert(np == 3);
    assert(strcmp(pieces[0], "ab") == 0);
    assert(strcmp(pieces[1], " ") == 0);
    assert(strcmp(pieces[2], "cd") == 0);
    tk_model_free(m);
    printf("test_pretok OK\n");
    return 0;
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `tests/cunit/run.sh pretok src/pretok.c src/model.c`
Expected: compile/link error (pretok symbols missing).

- [ ] **Step 3: Implement the PCRE2 wrapper**

`src/pretok.h`:
```c
#ifndef TK_PRETOK_H
#define TK_PRETOK_H
#include "model.h"
int tk_pretok_split(const tk_model *m, const uint8_t *text, size_t len,
                    void (*emit)(void *ud, size_t start, size_t end), void *ud);
#endif
```

`src/pretok.c`:
```c
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include "model.h"
#include "pretok.h"
#include <string.h>

static void pcre2_code_free_void(void *code) { pcre2_code_free((pcre2_code*)code); }

int tk_model_set_pattern(tk_model *m, const char *pattern) {
    int errnum; PCRE2_SIZE erroff;
    pcre2_code *code = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED,
                                     PCRE2_UTF | PCRE2_UCP, &errnum, &erroff, NULL);
    if (!code) return -1;
    pcre2_jit_compile(code, PCRE2_JIT_COMPLETE); /* best-effort; matching falls back if unavailable */
    tk_model__set_pcre2(m, code, pcre2_code_free_void);
    char **slot = tk_model__pattern_slot(m);
    free(*slot); *slot = malloc(strlen(pattern) + 1); strcpy(*slot, pattern);
    return 0;
}

int tk_pretok_split(const tk_model *m, const uint8_t *text, size_t len,
                    void (*emit)(void *ud, size_t start, size_t end), void *ud) {
    const pcre2_code *code = (const pcre2_code*)tk_model_pattern_code(m);
    if (!code) return -1;
    pcre2_match_data *md = pcre2_match_data_create(1, NULL);
    PCRE2_SIZE off = 0;
    while (off < len) {
        int rc = pcre2_match(code, (PCRE2_SPTR)text, len, off, 0, md, NULL);
        if (rc < 0) break; /* no more matches */
        PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
        size_t s = ov[0], e = ov[1];
        if (e == s) { e = s + 1; } /* guard against zero-width to ensure progress */
        emit(ud, s, e);
        off = e;
    }
    pcre2_match_data_free(md);
    return 0;
}
```

> The shipped extension compiles `src/pretok.c` against PHP's bundled PCRE2 (same `pcre2.h` API; `config.m4`'s `PHP_ADD_EXTENSION_DEP(tokenizers, pcre)` ensures availability). The cunit test links system `libpcre2-8`.

Modify `config.m4`: change the `PHP_NEW_EXTENSION` source list to include `src/pretok.c`:
```m4
  PHP_NEW_EXTENSION(tokenizers,
    [tokenizers.c src/model.c src/base64.c src/heap.c src/engine.c src/pretok.c src/loader_tiktoken.c src/cache.c src/bpe_class.c],
    $ext_shared,, [-DZEND_ENABLE_STATIC_TSRMLS_CACHE=1])
```
Modify `config.w32` `EXTENSION(...)` source list the same way (add `src/pretok.c`).

- [ ] **Step 4: Run the test to verify it passes**

Run: `tests/cunit/run.sh pretok src/pretok.c src/model.c`
Expected: `test_pretok OK`.

- [ ] **Step 5: Commit**

```bash
git add src/pretok.h src/pretok.c tests/cunit/test_pretok.c config.m4 config.w32
git commit -m "Add PCRE2 pre-tokenizer (UTF+UCP+JIT) and pattern compilation"
```

---

## Task 6: binary min-heap

**Files:**
- Create: `src/heap.h`, `src/heap.c`, `tests/cunit/test_heap.c`

**Interfaces:**
- Produces:
  - `typedef struct tk_heap tk_heap;`
  - `tk_heap *tk_heap_new(size_t cap_hint);`
  - `void tk_heap_push(tk_heap *h, uint64_t key);`
  - `int tk_heap_pop(tk_heap *h, uint64_t *out);` — pops the smallest key; returns 0 or -1 if empty.
  - `void tk_heap_free(tk_heap *h);`

  Keys encode `(rank << 32) | pos`, so popping smallest yields min rank, leftmost-on-tie.

- [ ] **Step 1: Write the failing C unit test**

`tests/cunit/test_heap.c`:
```c
#include "heap.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    tk_heap *h = tk_heap_new(2);
    uint64_t vals[] = { 5, 1, 9, 1, 3, 7 };
    for (int i = 0; i < 6; i++) tk_heap_push(h, vals[i]);
    uint64_t out, prev = 0; int n = 0;
    while (tk_heap_pop(h, &out) == 0) { assert(out >= prev); prev = out; n++; }
    assert(n == 6);
    assert(tk_heap_pop(h, &out) == -1);
    tk_heap_free(h);
    printf("test_heap OK\n");
    return 0;
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `tests/cunit/run.sh heap src/heap.c`
Expected: compile error.

- [ ] **Step 3: Implement the heap**

`src/heap.h`:
```c
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
```

`src/heap.c`:
```c
#include "heap.h"
#include <stdlib.h>
struct tk_heap { uint64_t *a; size_t len, cap; };
tk_heap *tk_heap_new(size_t cap_hint) {
    tk_heap *h = malloc(sizeof *h);
    h->cap = cap_hint < 4 ? 4 : cap_hint; h->len = 0;
    h->a = malloc(h->cap * sizeof(uint64_t));
    return h;
}
void tk_heap_push(tk_heap *h, uint64_t key) {
    if (h->len == h->cap) { h->cap *= 2; h->a = realloc(h->a, h->cap * sizeof(uint64_t)); }
    size_t i = h->len++; h->a[i] = key;
    while (i > 0) { size_t p = (i - 1) / 2; if (h->a[p] <= h->a[i]) break;
        uint64_t t = h->a[p]; h->a[p] = h->a[i]; h->a[i] = t; i = p; }
}
int tk_heap_pop(tk_heap *h, uint64_t *out) {
    if (h->len == 0) return -1;
    *out = h->a[0]; h->a[0] = h->a[--h->len];
    size_t i = 0;
    for (;;) { size_t l = 2*i+1, r = 2*i+2, m = i;
        if (l < h->len && h->a[l] < h->a[m]) m = l;
        if (r < h->len && h->a[r] < h->a[m]) m = r;
        if (m == i) break;
        uint64_t t = h->a[m]; h->a[m] = h->a[i]; h->a[i] = t; i = m; }
    return 0;
}
void tk_heap_free(tk_heap *h) { if (h) { free(h->a); free(h); } }
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `tests/cunit/run.sh heap src/heap.c`
Expected: `test_heap OK`.

- [ ] **Step 5: Commit**

```bash
git add src/heap.h src/heap.c tests/cunit/test_heap.c
git commit -m "Add binary min-heap for the BPE merge"
```

---

## Task 7: BPE merge of a single piece (the core algorithm)

**Files:**
- Create: `src/engine.h`, `src/engine.c`, `tests/cunit/test_engine.c`

**Interfaces:**
- Consumes: `tk_model` (rank lookups), `tk_heap` (Task 6).
- Produces: `size_t tk_bpe_merge(const tk_model *m, const uint8_t *piece, size_t len, uint32_t *out_ids);` — tokenizes one pre-token piece with the heap-based merge (always merges the globally minimum rank pair, leftmost on tie — byte-exact with tiktoken). Writes ids into `out_ids` (caller sizes `>= len`); returns the count. Every single byte is assumed present in the vocab (true for byte-level BPE).

- [ ] **Step 1: Write the failing C unit test**

`tests/cunit/test_engine.c`:
```c
#include "model.h"
#include "engine.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    tk_model *m = tk_model_new(8);
    tk_model_add(m, (const uint8_t*)"a", 1, 0);
    tk_model_add(m, (const uint8_t*)"b", 1, 1);
    tk_model_add(m, (const uint8_t*)"c", 1, 2);
    tk_model_add(m, (const uint8_t*)"bc", 2, 3);
    tk_model_add(m, (const uint8_t*)"aa", 2, 4);
    uint32_t out[16];

    size_t n = tk_bpe_merge(m, (const uint8_t*)"abc", 3, out);
    assert(n == 2 && out[0] == 0 && out[1] == 3);          /* a + bc */

    n = tk_bpe_merge(m, (const uint8_t*)"bc", 2, out);
    assert(n == 1 && out[0] == 3);

    n = tk_bpe_merge(m, (const uint8_t*)"a", 1, out);
    assert(n == 1 && out[0] == 0);

    n = tk_bpe_merge(m, (const uint8_t*)"aaaa", 4, out);
    assert(n == 2 && out[0] == 4 && out[1] == 4);          /* aa + aa */

    tk_model_free(m);
    printf("test_engine OK\n");
    return 0;
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `tests/cunit/run.sh engine src/engine.c src/model.c src/heap.c`
Expected: compile error (engine symbols missing).

- [ ] **Step 3: Implement the merge**

`src/engine.h`:
```c
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
```

`src/engine.c` (merge only for this task; encode/decode/count appended in Task 8):
```c
#include "engine.h"
#include "heap.h"
#include <stdlib.h>

static uint32_t pair_rank(const tk_model *m, const uint8_t *piece, size_t p,
                          const size_t *next, size_t len) {
    size_t q = next[p];
    if (q >= len) return TK_RANK_MAX;
    size_t s = next[q];
    return tk_model_rank(m, piece + p, s - p);
}

size_t tk_bpe_merge(const tk_model *m, const uint8_t *piece, size_t len, uint32_t *out_ids) {
    if (len == 0) return 0;
    if (len == 1) { out_ids[0] = tk_model_rank(m, piece, 1); return 1; }

    size_t *next = malloc((len + 1) * sizeof(size_t));
    size_t *prev = malloc((len + 1) * sizeof(size_t));
    uint8_t *alive = malloc((len + 1) * sizeof(uint8_t));
    for (size_t i = 0; i <= len; i++) { next[i] = i + 1; prev[i] = (i == 0) ? 0 : i - 1; alive[i] = 1; }
    next[len] = len; alive[len] = 0;

    tk_heap *h = tk_heap_new(len);
    for (size_t p = 0; p < len; p = next[p]) {
        uint32_t r = pair_rank(m, piece, p, next, len);
        if (r != TK_RANK_MAX) tk_heap_push(h, ((uint64_t)r << 32) | (uint32_t)p);
    }

    uint64_t kv;
    while (tk_heap_pop(h, &kv) == 0) {
        uint32_t r = (uint32_t)(kv >> 32);
        size_t p = (size_t)(uint32_t)kv;
        if (!alive[p]) continue;
        if (pair_rank(m, piece, p, next, len) != r) continue;
        size_t q = next[p], s = next[q];
        next[p] = s; if (s <= len) prev[s] = p; alive[q] = 0;
        uint32_t rp = pair_rank(m, piece, p, next, len);
        if (rp != TK_RANK_MAX) tk_heap_push(h, ((uint64_t)rp << 32) | (uint32_t)p);
        if (p != 0) { size_t pp = prev[p];
            uint32_t rpp = pair_rank(m, piece, pp, next, len);
            if (rpp != TK_RANK_MAX) tk_heap_push(h, ((uint64_t)rpp << 32) | (uint32_t)pp); }
    }
    tk_heap_free(h);

    size_t n = 0;
    for (size_t p = 0; p < len; p = next[p]) out_ids[n++] = tk_model_rank(m, piece + p, next[p] - p);
    free(next); free(prev); free(alive);
    return n;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `tests/cunit/run.sh engine src/engine.c src/model.c src/heap.c`
Expected: `test_engine OK`.

- [ ] **Step 5: Commit**

```bash
git add src/engine.h src/engine.c tests/cunit/test_engine.c
git commit -m "Add heap-based BPE merge (byte-exact, O(n log n) worst case)"
```

---

## Task 8: full encode / decode / count (with special tokens)

**Files:**
- Modify: `src/engine.c` (append `tk_ids`, `tk_encode_ordinary`, `tk_encode`, `tk_count`, `tk_decode`)
- Create: `tests/cunit/test_encode.c`

**Interfaces:**
- Consumes: `tk_bpe_merge` (Task 7), `tk_pretok_split` (Task 5), model specials (Task 2).
- Produces: the functions declared in `engine.h` in Task 7 (`tk_ids_*`, `tk_encode_ordinary`, `tk_encode`, `tk_count`, `tk_decode`).
- Special-token semantics: `allowed` lists strings encoded as their special id. If `disallow_unlisted` is non-zero, any model special found in the text that is **not** in `allowed` raises an error (matches tiktoken default). If zero, unlisted specials are encoded as ordinary bytes.

- [ ] **Step 1: Write the failing C unit test**

`tests/cunit/test_encode.c`:
```c
#include "model.h"
#include "engine.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
    tk_model *m = tk_model_new(8);
    tk_model_add(m, (const uint8_t*)"a", 1, 0);
    tk_model_add(m, (const uint8_t*)"b", 1, 1);
    tk_model_add(m, (const uint8_t*)"c", 1, 2);
    tk_model_add(m, (const uint8_t*)" ", 1, 3);
    tk_model_add(m, (const uint8_t*)"ab", 2, 4);
    assert(tk_model_set_pattern(m, "\\w+| +") == 0);
    tk_model_add_special(m, "<|end|>", 7, 100);

    tk_ids ids; tk_ids_init(&ids);
    assert(tk_encode_ordinary(m, (const uint8_t*)"ab c", 4, &ids) == 0);
    /* "ab" -> [4] ; " " -> [3] ; "c" -> [2] */
    assert(ids.len == 3 && ids.data[0] == 4 && ids.data[1] == 3 && ids.data[2] == 2);
    tk_ids_free(&ids);

    assert(tk_count(m, (const uint8_t*)"ab c", 4) == 3);

    /* disallowed special present -> error */
    tk_ids_init(&ids); char *err = NULL;
    int rc = tk_encode(m, (const uint8_t*)"a<|end|>", 8, NULL, 0, 1, &ids, &err);
    assert(rc != 0 && err != NULL); free(err); tk_ids_free(&ids);

    /* allowed special -> emitted as id 100 */
    tk_ids_init(&ids); err = NULL;
    const char *allow[] = { "<|end|>" };
    rc = tk_encode(m, (const uint8_t*)"a<|end|>", 8, allow, 1, 1, &ids, &err);
    assert(rc == 0 && ids.len == 2 && ids.data[0] == 0 && ids.data[1] == 100);
    tk_ids_free(&ids);

    /* decode round-trip of ordinary ids */
    uint8_t *out; size_t olen; uint32_t r[] = {4,3,2};
    assert(tk_decode(m, r, 3, &out, &olen, &err) == 0);
    assert(olen == 4 && memcmp(out, "ab c", 4) == 0); free(out);

    tk_model_free(m);
    printf("test_encode OK\n");
    return 0;
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `tests/cunit/run.sh encode src/engine.c src/model.c src/heap.c src/pretok.c`
Expected: link error (`tk_encode_ordinary` etc. undefined).

- [ ] **Step 3: Append encode/decode/count to `src/engine.c`**

```c
#include "pretok.h"
#include <string.h>

void tk_ids_init(tk_ids *v) { v->data = NULL; v->len = 0; v->cap = 0; }
void tk_ids_push(tk_ids *v, uint32_t id) {
    if (v->len == v->cap) { v->cap = v->cap ? v->cap * 2 : 64; v->data = realloc(v->data, v->cap * sizeof(uint32_t)); }
    v->data[v->len++] = id;
}
void tk_ids_free(tk_ids *v) { free(v->data); v->data = NULL; v->len = v->cap = 0; }

typedef struct { const tk_model *m; const uint8_t *text; tk_ids *out; uint32_t *scr; size_t scrcap; size_t count; } enc_ctx;

static void enc_emit(void *ud, size_t s, size_t e) {
    enc_ctx *c = ud; size_t plen = e - s;
    if (plen > c->scrcap) { c->scrcap = plen; c->scr = realloc(c->scr, plen * sizeof(uint32_t)); }
    size_t n = tk_bpe_merge(c->m, c->text + s, plen, c->scr);
    if (c->out) for (size_t i = 0; i < n; i++) tk_ids_push(c->out, c->scr[i]);
    c->count += n;
}

int tk_encode_ordinary(const tk_model *m, const uint8_t *text, size_t len, tk_ids *out) {
    enc_ctx c = { m, text, out, NULL, 0, 0 };
    int rc = tk_pretok_split(m, text, len, enc_emit, &c);
    free(c.scr);
    return rc;
}

size_t tk_count(const tk_model *m, const uint8_t *text, size_t len) {
    enc_ctx c = { m, text, NULL, NULL, 0, 0 };
    if (tk_pretok_split(m, text, len, enc_emit, &c) != 0) { free(c.scr); return 0; }
    free(c.scr);
    return c.count;
}

/* find earliest occurrence (>= from) of any model special; returns 1 if found */
static int find_special(const tk_model *m, const uint8_t *text, size_t len, size_t from,
                        size_t *pos_out, size_t *idx_out) {
    size_t best = len; int found = 0; size_t bi = 0;
    for (size_t i = 0; i < tk_model_special_count(m); i++) {
        size_t slen; uint32_t sid; const char *s = tk_model_special_at(m, i, &slen, &sid);
        if (slen == 0 || slen > len) continue;
        for (size_t p = from; p + slen <= len && p < best + 1; p++) {
            if (text[p] == (uint8_t)s[0] && memcmp(text + p, s, slen) == 0) {
                if (!found || p < best) { best = p; bi = i; found = 1; }
                break;
            }
        }
    }
    if (found) { *pos_out = best; *idx_out = bi; }
    return found;
}

static int is_allowed(const char **allowed, size_t n, const char *s, size_t slen) {
    for (size_t i = 0; i < n; i++) if (strlen(allowed[i]) == slen && memcmp(allowed[i], s, slen) == 0) return 1;
    return 0;
}

int tk_encode(const tk_model *m, const uint8_t *text, size_t len,
              const char **allowed, size_t n_allowed, int disallow_unlisted,
              tk_ids *out, char **err) {
    if (err) *err = NULL;
    if (tk_model_special_count(m) == 0) return tk_encode_ordinary(m, text, len, out);
    size_t cur = 0;
    while (cur < len) {
        size_t pos, idx;
        if (!find_special(m, text, len, cur, &pos, &idx)) {
            return tk_encode_ordinary(m, text + cur, len - cur, out) == 0 ? 0 : -1;
        }
        size_t slen; uint32_t sid; const char *s = tk_model_special_at(m, idx, &slen, &sid);
        int allowed_hit = is_allowed(allowed, n_allowed, s, slen);
        if (!allowed_hit) {
            if (disallow_unlisted) {
                if (err) { const char *p = "encountered a disallowed special token"; char *e = malloc(strlen(p)+1); strcpy(e,p); *err = e; }
                return -1;
            }
            /* treat as ordinary: extend ordinary span to include it */
            if (tk_encode_ordinary(m, text + cur, (pos + slen) - cur, out) != 0) return -1;
            cur = pos + slen; continue;
        }
        if (pos > cur && tk_encode_ordinary(m, text + cur, pos - cur, out) != 0) return -1;
        tk_ids_push(out, sid);
        cur = pos + slen;
    }
    return 0;
}

int tk_decode(const tk_model *m, const uint32_t *ids, size_t n,
              uint8_t **out, size_t *out_len, char **err) {
    if (err) *err = NULL;
    size_t cap = 64, o = 0; uint8_t *buf = malloc(cap);
    for (size_t i = 0; i < n; i++) {
        size_t blen; const uint8_t *b = tk_model_bytes(m, ids[i], &blen);
        const char *sb = NULL; size_t slen = 0;
        if (!b) { /* maybe a special id */
            for (size_t k = 0; k < tk_model_special_count(m); k++) {
                size_t l; uint32_t sid; const char *s = tk_model_special_at(m, k, &l, &sid);
                if (sid == ids[i]) { sb = s; slen = l; break; }
            }
            if (!sb) { free(buf); if (err) { const char *p="invalid token id in decode"; char *e=malloc(strlen(p)+1); strcpy(e,p); *err=e; } return -1; }
            b = (const uint8_t*)sb; blen = slen;
        }
        if (o + blen > cap) { while (o + blen > cap) cap *= 2; buf = realloc(buf, cap); }
        memcpy(buf + o, b, blen); o += blen;
    }
    *out = buf; *out_len = o;
    return 0;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `tests/cunit/run.sh encode src/engine.c src/model.c src/heap.c src/pretok.c`
Expected: `test_encode OK`.

- [ ] **Step 5: Commit**

```bash
git add src/engine.c tests/cunit/test_encode.c
git commit -m "Add full encode/decode/count with tiktoken special-token semantics"
```

---

## Task 9: process-global model cache (ZTS-safe)

**Files:**
- Create: `src/cache.h`, `src/cache.c`, `tests/cunit/test_cache.c`

**Interfaces:**
- Produces:
  - `void tk_cache_init(void);` — called from MINIT; allocates the mutex under ZTS.
  - `typedef tk_model *(*tk_loader_fn)(void *ud, char **err);`
  - `const tk_model *tk_cache_get_or_load(const char *key, tk_loader_fn loader, void *ud, char **err);` — returns the cached model for `key`, loading it once via `loader` on miss. Returned model is immutable; safe to read lock-free. NULL + `*err` on loader failure.
  - `size_t tk_cache_count(void);`
  - `void tk_cache_shutdown(void);` — called from MSHUTDOWN; frees all models and keys.

- [ ] **Step 1: Write the failing C unit test**

`tests/cunit/test_cache.c`:
```c
#include "cache.h"
#include "model.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static int calls = 0;
static tk_model *ld(void *ud, char **err) {
    (void)ud; (void)err; calls++;
    tk_model *m = tk_model_new(4); tk_model_add(m, (const uint8_t*)"x", 1, 0); return m;
}
int main(void) {
    tk_cache_init();
    const tk_model *a = tk_cache_get_or_load("k1", ld, NULL, NULL);
    const tk_model *b = tk_cache_get_or_load("k1", ld, NULL, NULL);
    assert(a == b && calls == 1);
    const tk_model *c = tk_cache_get_or_load("k2", ld, NULL, NULL);
    assert(c != a && calls == 2);
    assert(tk_cache_count() == 2);
    tk_cache_shutdown();
    printf("test_cache OK\n");
    return 0;
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `tests/cunit/run.sh cache src/cache.c src/model.c`
Expected: compile error.

- [ ] **Step 3: Implement the cache**

`src/cache.h`:
```c
#ifndef TK_CACHE_H
#define TK_CACHE_H
#include "model.h"
typedef tk_model *(*tk_loader_fn)(void *ud, char **err);
void tk_cache_init(void);
const tk_model *tk_cache_get_or_load(const char *key, tk_loader_fn loader, void *ud, char **err);
size_t tk_cache_count(void);
void tk_cache_shutdown(void);
#endif
```

`src/cache.c`:
```c
#include "cache.h"
#include <stdlib.h>
#include <string.h>

#ifdef ZTS
#include "TSRM.h"
static MUTEX_T tk_cache_mx;
#define TK_LOCK()   tsrm_mutex_lock(tk_cache_mx)
#define TK_UNLOCK() tsrm_mutex_unlock(tk_cache_mx)
#else
#define TK_LOCK()
#define TK_UNLOCK()
#endif

typedef struct { char *key; tk_model *model; } entry;
static entry *g_entries = NULL; static size_t g_count = 0, g_cap = 0;

void tk_cache_init(void) {
#ifdef ZTS
    if (!tk_cache_mx) tk_cache_mx = tsrm_mutex_alloc();
#endif
}

static const tk_model *find(const char *key) {
    for (size_t i = 0; i < g_count; i++) if (strcmp(g_entries[i].key, key) == 0) return g_entries[i].model;
    return NULL;
}

const tk_model *tk_cache_get_or_load(const char *key, tk_loader_fn loader, void *ud, char **err) {
    if (err) *err = NULL;
    TK_LOCK();
    const tk_model *m = find(key);
    if (m) { TK_UNLOCK(); return m; }
    char *lerr = NULL;
    tk_model *nm = loader(ud, &lerr);
    if (!nm) { TK_UNLOCK(); if (err) *err = lerr; else free(lerr); return NULL; }
    if (g_count == g_cap) { g_cap = g_cap ? g_cap * 2 : 8; g_entries = realloc(g_entries, g_cap * sizeof(entry)); }
    g_entries[g_count].key = malloc(strlen(key) + 1); strcpy(g_entries[g_count].key, key);
    g_entries[g_count].model = nm; g_count++;
    TK_UNLOCK();
    return nm;
}

size_t tk_cache_count(void) { return g_count; }

void tk_cache_shutdown(void) {
    for (size_t i = 0; i < g_count; i++) { free(g_entries[i].key); tk_model_free(g_entries[i].model); }
    free(g_entries); g_entries = NULL; g_count = g_cap = 0;
#ifdef ZTS
    if (tk_cache_mx) { tsrm_mutex_free(tk_cache_mx); tk_cache_mx = NULL; }
#endif
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `tests/cunit/run.sh cache src/cache.c src/model.c`
Expected: `test_cache OK`.

- [ ] **Step 5: Commit**

```bash
git add src/cache.h src/cache.c tests/cunit/test_cache.c
git commit -m "Add process-global immutable model cache (ZTS-safe)"
```

---

## Task 10: `\Tokenizers\Bpe` class + exception (PHP-visible API)

**Files:**
- Modify: `tokenizers.stub.php` (add classes), `tokenizers.c` (call registration + cache init/shutdown), `src/bpe_class.c` (implement)
- Create: `tests/010-bpe-tiktoken.phpt`

**Interfaces:**
- Consumes: `tk_cache_get_or_load`, `tk_load_tiktoken_file`, `tk_model_set_pattern`, `tk_encode`, `tk_decode`, `tk_count`, model accessors.
- Produces (PHP): class `\Tokenizers\Bpe` with `fromTiktokenFile`, `fromVocab`, `encode`, `decode`, `decodeSingle`, `countTokens`, `vocabSize`, `name`; exception `\Tokenizers\TokenizerException`; debug `tokenizers_cache_count(): int`.

- [ ] **Step 1: Write the failing test**

`tests/010-bpe-tiktoken.phpt`:
```
--TEST--
Bpe::fromTiktokenFile encode/decode/count round-trip
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Bpe;
$bpe = Bpe::fromTiktokenFile(__DIR__ . '/fixtures/mini.tiktoken', '\w+| +', []);
$ids = $bpe->encode('abc');
echo implode(',', $ids), "\n";          // expect 3,2  (ab + c)
echo $bpe->decode($ids), "\n";          // expect abc
echo $bpe->countTokens('abc'), "\n";    // expect 2
echo $bpe->vocabSize(), "\n";           // expect 5
// caching: loading the same file again does not add a cache entry
$n1 = tokenizers_cache_count();
$bpe2 = Bpe::fromTiktokenFile(__DIR__ . '/fixtures/mini.tiktoken', '\w+| +', []);
var_dump(tokenizers_cache_count() === $n1);
?>
--EXPECT--
3,2
abc
2
5
bool(true)
```

- [ ] **Step 2: Run it to confirm it fails**

Run (after a build with the current code): `php -d extension=$(pwd)/modules/tokenizers.so tests/010-bpe-tiktoken.phpt`
Expected: class `Tokenizers\Bpe` not found.

- [ ] **Step 3: Add the classes to the stub**

Append to `tokenizers.stub.php` inside `namespace Tokenizers`:
```php
    class TokenizerException extends \RuntimeException {}

    final class Bpe {
        public static function fromTiktokenFile(string $path, string $pattern, array $specialTokens = []): Bpe {}
        public static function fromVocab(array $tokenBytesToId, array $merges, string $pattern, array $specialTokens = []): Bpe {}
        public function encode(string $text, array|string $allowedSpecial = [], array|string $disallowedSpecial = "all"): array {}
        public function countTokens(string $text): int {}
        public function decode(array $ids): string {}
        public function decodeSingle(int $id): string {}
        public function vocabSize(): int {}
        public function name(): ?string {}
    }
```
And in the global namespace add `function tokenizers_cache_count(): int {}`.
Regenerate: `php <php-build-dir>/gen_stub.php tokenizers.stub.php` → updates `tokenizers_arginfo.h`.

- [ ] **Step 4: Implement `src/bpe_class.c`**

```c
#include "php.h"
#include "zend_exceptions.h"
#include "php_tokenizers.h"
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
    if (tk_model_set_pattern(m, c->pattern) != 0) { tk_model_free(m); if (err){*err=estrdup("invalid pre-tokenizer pattern");} return NULL; }
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
        koff += snprintf(key + koff, sizeof key - koff, ":%u", sid[i]);

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
        ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(allowed), zv) { alist[i++] = Z_STRVAL_P(zv); } ZEND_HASH_FOREACH_END();
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

/* fromVocab implemented in Task 13 */
PHP_METHOD(Tokenizers_Bpe, fromVocab) { tk_throw("fromVocab not yet implemented"); RETURN_THROWS(); }

PHP_FUNCTION(tokenizers_cache_count) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG((zend_long)tk_cache_count()); }

void tk_register_bpe_class(void) {
    tokenizers_exception_ce = register_class_Tokenizers_TokenizerException(zend_ce_exception);
    tokenizers_bpe_ce = register_class_Tokenizers_Bpe();
    tokenizers_bpe_ce->create_object = tk_bpe_create;
    memcpy(&tk_bpe_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    tk_bpe_handlers.offset = XtOffsetOf(tk_bpe_obj, std);
    tk_bpe_handlers.free_obj = tk_bpe_free;
}
```

> The `register_class_*` functions are emitted by `gen_stub.php` into `tokenizers_arginfo.h`. The `PHP_METHOD` names use the `Tokenizers_Bpe` mangling that `gen_stub` expects for `namespace Tokenizers; class Bpe`.

- [ ] **Step 5: Wire registration + cache lifecycle into `tokenizers.c`**

In `tokenizers.c`, add `extern void tk_register_bpe_class(void);` and `#include "src/cache.h"`, then:
```c
PHP_MINIT_FUNCTION(tokenizers) {
    REGISTER_STRING_CONSTANT("Tokenizers\\VERSION", PHP_TOKENIZERS_VERSION, CONST_PERSISTENT);
    tk_cache_init();
    tk_register_bpe_class();
    return SUCCESS;
}
PHP_MSHUTDOWN_FUNCTION(tokenizers) { tk_cache_shutdown(); return SUCCESS; }
```
And put `PHP_MSHUTDOWN(tokenizers)` into the module entry (replace the first `NULL` after MINIT).

- [ ] **Step 6: Build and run the test to verify it passes**

```bash
cd ~/projects/php-tokenizers && phpize >/dev/null && ./configure --enable-tokenizers >/dev/null && make -j4 2>&1 | tail -3
php -d extension=$(pwd)/modules/tokenizers.so tests/010-bpe-tiktoken.phpt && echo "PHPT OK"
```
Expected: matches `--EXPECT--`.

- [ ] **Step 7: Commit**

```bash
git add tokenizers.stub.php tokenizers_arginfo.h tokenizers.c src/bpe_class.c tests/010-bpe-tiktoken.phpt
git commit -m "Add \Tokenizers\Bpe class: fromTiktokenFile + encode/decode/count"
```

---

## Task 11: procedural API

**Files:**
- Modify: `tokenizers.stub.php` (add 3 functions), `src/bpe_class.c` (implement), regenerate arginfo
- Create: `tests/011-procedural.phpt`

**Interfaces:**
- Consumes: `\Tokenizers\Bpe` instances + the same `tk_*` core calls.
- Produces (PHP): `tokenizers_encode(Bpe $t, string $text, array $allowedSpecial = [], array|string $disallowedSpecial = "all"): array`, `tokenizers_decode(Bpe $t, array $ids): string`, `tokenizers_count(Bpe $t, string $text): int`.

- [ ] **Step 1: Write the failing test**

`tests/011-procedural.phpt`:
```
--TEST--
Procedural helpers operate on a Bpe instance
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Bpe;
$b = Bpe::fromTiktokenFile(__DIR__ . '/fixtures/mini.tiktoken', '\w+| +', []);
$ids = tokenizers_encode($b, 'abc');
echo implode(',', $ids), "\n";        // 3,2
echo tokenizers_count($b, 'abc'), "\n"; // 2
echo tokenizers_decode($b, $ids), "\n"; // abc
?>
--EXPECT--
3,2
2
abc
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `php -d extension=$(pwd)/modules/tokenizers.so tests/011-procedural.phpt`
Expected: call to undefined function `tokenizers_encode`.

- [ ] **Step 3: Add stubs + implement**

Add to `tokenizers.stub.php` (global namespace):
```php
function tokenizers_encode(\Tokenizers\Bpe $t, string $text, array $allowedSpecial = [], array|string $disallowedSpecial = "all"): array {}
function tokenizers_decode(\Tokenizers\Bpe $t, array $ids): string {}
function tokenizers_count(\Tokenizers\Bpe $t, string $text): int {}
```
In `src/bpe_class.c`, add an accessor and the three functions:
```c
extern zend_class_entry *tokenizers_bpe_ce;
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
        ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(allowed), zv){ alist[i++]=Z_STRVAL_P(zv); } ZEND_HASH_FOREACH_END(); }
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
```
Regenerate arginfo (`gen_stub.php tokenizers.stub.php`) and ensure the three `ZEND_FE` entries are present in `ext_functions` (gen_stub appends them automatically when the functions are in the stub).

- [ ] **Step 4: Build, run the test**

```bash
make -j4 2>&1 | tail -2 && php -d extension=$(pwd)/modules/tokenizers.so tests/011-procedural.phpt && echo OK
```
Expected: matches `--EXPECT--`.

- [ ] **Step 5: Commit**

```bash
git add tokenizers.stub.php tokenizers_arginfo.h src/bpe_class.c tests/011-procedural.phpt
git commit -m "Add procedural encode/decode/count helpers"
```

---

## Task 12: PHP shim — known-encoding resolver (download + cache)

**Files:**
- Create: `php/Tokenizers/Encoding.php`, `tests/012-encoding-download.phpt`, `tests/fixtures/fake_cl.tiktoken`
- Modify: `config.m4` (install the php/ dir), `package.xml` later

**Interfaces:**
- Produces (PHP):
  - `\Tokenizers\Encoding::load(string $name): \Tokenizers\Bpe` — resolves a known encoding (download+cache+checksum on first use), returns a ready `Bpe`.
  - `\Tokenizers\Encoding::download(string $url, ?string $sha256, string $dest): void` — testable unit: fetch `$url` (supports `file://`), verify sha256 if given, write to `$dest`. Throws `TokenizerException` on mismatch/failure.
  - `\Tokenizers\Encoding::cacheDir(): string`.

- [ ] **Step 1: Write the failing test (offline, uses `file://`)**

`tests/fixtures/fake_cl.tiktoken` — same content as `mini.tiktoken`.

`tests/012-encoding-download.phpt`:
```
--TEST--
Encoding::download fetches file:// and verifies checksum
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Encoding;
require __DIR__ . '/../php/Tokenizers/Encoding.php';
$src = __DIR__ . '/fixtures/fake_cl.tiktoken';
$sha = hash_file('sha256', $src);
$dest = sys_get_temp_dir() . '/tk_test_' . getmypid() . '.tiktoken';
Encoding::download('file://' . $src, $sha, $dest);
var_dump(file_exists($dest));
// wrong checksum throws
try { Encoding::download('file://' . $src, str_repeat('0', 64), $dest . '.x'); echo "no throw\n"; }
catch (\Tokenizers\TokenizerException $e) { echo "checksum throws\n"; }
@unlink($dest);
?>
--EXPECT--
bool(true)
checksum throws
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `php -d extension=$(pwd)/modules/tokenizers.so tests/012-encoding-download.phpt`
Expected: class `Tokenizers\Encoding` not found.

- [ ] **Step 3: Implement the shim**

`php/Tokenizers/Encoding.php`:
```php
<?php
namespace Tokenizers;

final class Encoding
{
    private const CL100K_PATTERN =
        "(?i:'s|'t|'re|'ve|'m|'ll|'d)|[^\\r\\n\\p{L}\\p{N}]?\\p{L}+|\\p{N}{1,3}| ?[^\\s\\p{L}\\p{N}]+[\\r\\n]*|\\s*[\\r\\n]+|\\s+(?!\\S)|\\s+";
    private const O200K_PATTERN =
        "[^\\r\\n\\p{L}\\p{N}]?[\\p{Lu}\\p{Lt}\\p{Lm}\\p{Lo}\\p{M}]*[\\p{Ll}\\p{Lm}\\p{Lo}\\p{M}]+(?i:'s|'t|'re|'ve|'m|'ll|'d)?|".
        "[^\\r\\n\\p{L}\\p{N}]?[\\p{Lu}\\p{Lt}\\p{Lm}\\p{Lo}\\p{M}]+[\\p{Ll}\\p{Lm}\\p{Lo}\\p{M}]*(?i:'s|'t|'re|'ve|'m|'ll|'d)?|".
        "\\p{N}{1,3}| ?[^\\s\\p{L}\\p{N}]+[\\r\\n/]*|\\s*[\\r\\n]+|\\s+(?!\\S)|\\s+";

    /** name => [url, sha256, pattern, specials]. sha256 may be null to skip verification. */
    private static function registry(): array
    {
        return [
            'cl100k_base' => [
                'url' => 'https://openaipublic.blob.core.windows.net/encodings/cl100k_base.tiktoken',
                'sha256' => null, // pin in Step 5
                'pattern' => self::CL100K_PATTERN,
                'specials' => [
                    '<|endoftext|>' => 100257, '<|fim_prefix|>' => 100258, '<|fim_middle|>' => 100259,
                    '<|fim_suffix|>' => 100260, '<|endofprompt|>' => 100276,
                ],
            ],
            'o200k_base' => [
                'url' => 'https://openaipublic.blob.core.windows.net/encodings/o200k_base.tiktoken',
                'sha256' => null,
                'pattern' => self::O200K_PATTERN,
                'specials' => ['<|endoftext|>' => 199999, '<|endofprompt|>' => 200018],
            ],
        ];
    }

    public static function cacheDir(): string
    {
        $base = getenv('TOKENIZERS_CACHE_DIR')
            ?: (getenv('XDG_CACHE_HOME') ?: (getenv('HOME') ? getenv('HOME') . '/.cache' : sys_get_temp_dir()));
        $dir = rtrim($base, '/') . '/tokenizers';
        if (!is_dir($dir) && !@mkdir($dir, 0775, true) && !is_dir($dir)) {
            throw new TokenizerException("cannot create cache dir: $dir");
        }
        return $dir;
    }

    public static function download(string $url, ?string $sha256, string $dest): void
    {
        $data = @file_get_contents($url);
        if ($data === false) throw new TokenizerException("download failed: $url");
        if ($sha256 !== null && !hash_equals(strtolower($sha256), hash('sha256', $data))) {
            throw new TokenizerException("checksum mismatch for $url");
        }
        $tmp = $dest . '.' . getmypid() . '.part';
        if (@file_put_contents($tmp, $data) === false || !@rename($tmp, $dest)) {
            @unlink($tmp);
            throw new TokenizerException("cannot write cache file: $dest");
        }
    }

    public static function load(string $name): Bpe
    {
        $reg = self::registry();
        if (!isset($reg[$name])) throw new TokenizerException("unknown encoding: $name");
        $e = $reg[$name];
        $path = self::cacheDir() . '/' . $name . '.tiktoken';
        if (!is_file($path)) self::download($e['url'], $e['sha256'], $path);
        return Bpe::fromTiktokenFile($path, $e['pattern'], $e['specials']);
    }
}
```

- [ ] **Step 4: Build/run the test**

Run: `php -d extension=$(pwd)/modules/tokenizers.so tests/012-encoding-download.phpt`
Expected: matches `--EXPECT--`.

- [ ] **Step 5: Pin checksums (real values, not placeholders)**

Run once with network to fetch and record the authoritative hashes, then paste them into `registry()` replacing the two `null`s:
```bash
for n in cl100k_base o200k_base; do
  curl -fsSL "https://openaipublic.blob.core.windows.net/encodings/$n.tiktoken" -o "/tmp/$n.tiktoken"
  printf "%s sha256 = %s\n" "$n" "$(shasum -a 256 /tmp/$n.tiktoken | cut -d' ' -f1)"
done
```
Edit `registry()` so each `'sha256' =>` holds the printed value. Commit the pinned values.

- [ ] **Step 6: Install the php/ dir from the build**

In `config.m4`, add after `PHP_NEW_EXTENSION`:
```m4
  PHP_INSTALL_HEADERS([ext/tokenizers], [php_tokenizers.h])
  PHP_ADD_MAKEFILE_FRAGMENT
```
Create `Makefile.frag` to install `php/Tokenizers/Encoding.php` next to the extension (or document that frameworks autoload it via Composer in Phase 3). For v1, document requiring the file directly; CI references it by path.

- [ ] **Step 7: Commit**

```bash
git add php/Tokenizers/Encoding.php tests/012-encoding-download.phpt tests/fixtures/fake_cl.tiktoken config.m4 Makefile.frag
git commit -m "Add Encoding shim: known-encoding download/cache + checksum verify"
```

---

## Task 13: HuggingFace `tokenizer.json` BPE loader

**Files:**
- Modify: `src/bpe_class.c` (implement `Bpe::fromVocab`), `php/Tokenizers/Encoding.php` (add `fromHuggingFace`)
- Create: `tests/013-fromvocab.phpt`, `tests/013-hf.phpt`, `tests/fixtures/mini_hf.json`

**Interfaces:**
- Consumes: `tk_model_new/add/add_special`, `tk_model_set_pattern`.
- Produces (PHP):
  - `Bpe::fromVocab(array $tokenBytesToId, array $merges, string $pattern, array $specialTokens = []): Bpe` — builds an **owned** model. Keys of `$tokenBytesToId` are raw token bytes (binary strings); values are ids used as both rank and output id. `$merges` is accepted for forward-compat and ignored in v1 (id order encodes merge priority).
  - `Encoding::fromHuggingFace(string $jsonPath): Bpe` — parse a `tokenizer.json` whose `model.type == "BPE"` with a ByteLevel pre-tokenizer; map vocab byte-chars back to raw bytes; delegate to `fromVocab`.

- [ ] **Step 1: Write failing tests**

`tests/013-fromvocab.phpt`:
```
--TEST--
Bpe::fromVocab builds a working model from raw-byte vocab
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Bpe;
$b = Bpe::fromVocab(['a' => 0, 'b' => 1, 'ab' => 2], [], '\w+', []);
echo implode(',', $b->encode('ab')), "\n";   // 2
echo $b->decode([2]), "\n";                    // ab
echo $b->vocabSize(), "\n";                     // 3
?>
--EXPECT--
2
ab
3
```

`tests/fixtures/mini_hf.json` (minimal GPT-2-style: vocab keys in byte-char space; for ASCII a/b these equal themselves):
```json
{"model":{"type":"BPE","vocab":{"a":0,"b":1,"ab":2},"merges":["a b"]},
 "pre_tokenizer":{"type":"ByteLevel"},
 "added_tokens":[{"id":2,"content":"<|x|>","special":true}]}
```
> Note: id 2 collides with "ab" here only to keep the fixture tiny; in Step 3 the special is registered separately and decode prefers vocab bytes, which is acceptable for this structural test. Use a non-colliding id if asserting special encode.

`tests/013-hf.phpt`:
```
--TEST--
Encoding::fromHuggingFace loads a BPE tokenizer.json
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Encoding;
require __DIR__ . '/../php/Tokenizers/Encoding.php';
$b = Encoding::fromHuggingFace(__DIR__ . '/fixtures/mini_hf.json');
echo implode(',', $b->encode('ab')), "\n";  // 2
?>
--EXPECT--
2
```

- [ ] **Step 2: Run to confirm failure**

Run both; expected: `fromVocab not yet implemented` / `Encoding::fromHuggingFace` undefined.

- [ ] **Step 3: Implement `Bpe::fromVocab` in `src/bpe_class.c`**

Replace the stub `PHP_METHOD(Tokenizers_Bpe, fromVocab)` with:
```c
PHP_METHOD(Tokenizers_Bpe, fromVocab) {
    zval *vocab, *merges = NULL, *specials = NULL; char *pattern; size_t pat_len;
    ZEND_PARSE_PARAMETERS_START(3, 4)
        Z_PARAM_ARRAY(vocab) Z_PARAM_ARRAY(merges) Z_PARAM_STRING(pattern, pat_len)
        Z_PARAM_OPTIONAL Z_PARAM_ARRAY(specials)
    ZEND_PARSE_PARAMETERS_END();
    (void)merges; /* v1: id order encodes merge priority */

    tk_model *m = tk_model_new(zend_hash_num_elements(Z_ARRVAL_P(vocab)));
    zend_string *k; zval *v;
    ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(vocab), k, v) {
        if (!k) continue;
        tk_model_add(m, (const uint8_t*)ZSTR_VAL(k), ZSTR_LEN(k), (uint32_t)zval_get_long(v));
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
```

- [ ] **Step 4: Implement `Encoding::fromHuggingFace` (byte-char → raw bytes)**

Add to `php/Tokenizers/Encoding.php`:
```php
    /** GPT-2 byte-level: map of unicode codepoint => raw byte. */
    private static function byteDecoder(): array
    {
        static $map = null;
        if ($map !== null) return $map;
        $bs = array_merge(range(0x21, 0x7E), range(0xA1, 0xAC), range(0xAE, 0xFF));
        $cs = $bs; $n = 0;
        for ($b = 0; $b < 256; $b++) { if (!in_array($b, $bs, true)) { $bs[] = $b; $cs[] = 256 + $n; $n++; } }
        $map = [];
        foreach ($bs as $i => $b) $map[$cs[$i]] = $b;
        return $map;
    }

    private static function tokenCharsToBytes(string $token): string
    {
        $dec = self::byteDecoder();
        $out = '';
        // iterate unicode codepoints of $token
        $cps = preg_split('//u', $token, -1, PREG_SPLIT_NO_EMPTY);
        foreach ($cps as $ch) {
            $cp = \IntlChar::ord($ch) ?? self::utf8ord($ch);
            if (!isset($dec[$cp])) throw new TokenizerException("vocab token char U+" . dechex($cp) . " not in byte map");
            $out .= chr($dec[$cp]);
        }
        return $out;
    }

    private static function utf8ord(string $ch): int
    {
        $ord = unpack('N', str_pad(mb_convert_encoding($ch, 'UTF-32BE', 'UTF-8'), 4, "\0", STR_PAD_LEFT));
        return $ord[1];
    }

    public static function fromHuggingFace(string $jsonPath): Bpe
    {
        $raw = @file_get_contents($jsonPath);
        if ($raw === false) throw new TokenizerException("cannot read $jsonPath");
        $j = json_decode($raw, true);
        if (!is_array($j) || ($j['model']['type'] ?? null) !== 'BPE') {
            throw new TokenizerException("unsupported tokenizer.json (only model.type=BPE is supported in v1)");
        }
        $vocab = $j['model']['vocab'] ?? [];
        $rawVocab = [];
        foreach ($vocab as $token => $id) {
            $rawVocab[self::tokenCharsToBytes((string)$token)] = (int)$id;
        }
        $specials = [];
        foreach (($j['added_tokens'] ?? []) as $t) {
            if (!empty($t['special'])) $specials[(string)$t['content']] = (int)$t['id'];
        }
        // ByteLevel BPE uses the GPT-2 split regex
        $pattern = "(?i:'s|'t|'re|'ve|'m|'ll|'d)| ?\\p{L}+| ?\\p{N}+| ?[^\\s\\p{L}\\p{N}]+|\\s+(?!\\S)|\\s+";
        return Bpe::fromVocab($rawVocab, $j['model']['merges'] ?? [], $pattern, $specials);
    }
```

- [ ] **Step 5: Build and run both tests**

```bash
make -j4 2>&1 | tail -2
php -d extension=$(pwd)/modules/tokenizers.so tests/013-fromvocab.phpt && \
php -d extension=$(pwd)/modules/tokenizers.so tests/013-hf.phpt && echo OK
```
Expected: both match.

- [ ] **Step 6: Commit**

```bash
git add src/bpe_class.c php/Tokenizers/Encoding.php tests/013-fromvocab.phpt tests/013-hf.phpt tests/fixtures/mini_hf.json
git commit -m "Add HuggingFace tokenizer.json BPE loader (fromVocab + byte-level mapping)"
```

---

## Task 14: byte-exact conformance against the Python reference

**Files:**
- Create: `tests/reference/generate_fixtures.py`, `tests/reference/fixtures/conformance.json` (committed output), `tests/014-conformance.phpt`, `tests/015-worstcase.phpt`

**Interfaces:**
- Consumes: `Encoding::load`, `Bpe::encode`/`decode`/`countTokens`.
- Produces: a committed fixture file and tests that fail on any byte-level divergence from `tiktoken`.

- [ ] **Step 1: Write the fixture generator and generate fixtures**

`tests/reference/generate_fixtures.py`:
```python
#!/usr/bin/env python3
# Requires: pip install tiktoken
import json, os, tiktoken

CASES = [
    "", " ", "\n", "hello world", "Hello, World!",
    "Hola, ¿cómo estás?", "ünïcödé NFC", "naïve café",
    "👍🏽 multi-byte emoji 🇲🇽", "a" * 1000, "ab" * 500,
    "123 4567 89", "  leading and  double  spaces ",
    "tabs\tand\tnewlines\n\n", "<|endoftext|> as text",
    "snake_case CamelCase kebab-case", "function foo(){ return 42; }",
    "El rápido zorro marrón salta sobre el perro perezoso.",
]
out = {}
for name in ["cl100k_base", "o200k_base"]:
    enc = tiktoken.get_encoding(name)
    out[name] = [{"text": c, "ids": enc.encode(c, disallowed_special=())} for c in CASES]
os.makedirs("tests/reference/fixtures", exist_ok=True)
with open("tests/reference/fixtures/conformance.json", "w") as f:
    json.dump(out, f, ensure_ascii=False, indent=0)
print("wrote tests/reference/fixtures/conformance.json")
```
Run (one-time, needs network + Python):
```bash
python3 -m pip install --quiet tiktoken
python3 tests/reference/generate_fixtures.py
```

- [ ] **Step 2: Write the conformance test**

`tests/014-conformance.phpt`:
```
--TEST--
Byte-exact tokenization vs the tiktoken reference (cl100k_base, o200k_base)
--SKIPIF--
<?php
if (!extension_loaded('tokenizers')) { echo 'skip'; exit; }
require __DIR__ . '/../php/Tokenizers/Encoding.php';
try { \Tokenizers\Encoding::load('cl100k_base'); }
catch (\Throwable $e) { echo 'skip vocab unavailable (offline)'; }
?>
--FILE--
<?php
use Tokenizers\Encoding;
require __DIR__ . '/../php/Tokenizers/Encoding.php';
$fix = json_decode(file_get_contents(__DIR__ . '/reference/fixtures/conformance.json'), true);
$fail = 0;
foreach (['cl100k_base', 'o200k_base'] as $name) {
    $enc = Encoding::load($name);
    foreach ($fix[$name] as $i => $case) {
        $got = $enc->encode($case['text'], [], []); // [] disallowed => specials-as-ordinary, matches python disallowed_special=()
        if ($got !== $case['ids']) {
            $fail++;
            echo "MISMATCH $name#$i text=" . json_encode($case['text']) . "\n";
            echo "  expected " . implode(',', $case['ids']) . "\n  got      " . implode(',', $got) . "\n";
        }
        if ($enc->decode($got) !== $case['text']) { $fail++; echo "DECODE MISMATCH $name#$i\n"; }
        if ($enc->countTokens($case['text']) !== count($case['ids'])) { $fail++; echo "COUNT MISMATCH $name#$i\n"; }
    }
}
echo $fail === 0 ? "ALL CONFORMANT\n" : "FAILURES: $fail\n";
?>
--EXPECT--
ALL CONFORMANT
```

`tests/015-worstcase.phpt`:
```
--TEST--
Pathological long single token does not hang (O(n log n))
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Bpe;
// vocab of single byte 'a' plus 'aa' forces repeated merges; 200k chars must finish fast
$vocab = ['a' => 0, 'aa' => 1];
$b = Bpe::fromVocab($vocab, [], '\w+', []);
$t = str_repeat('a', 200000);
$start = microtime(true);
$n = $b->countTokens($t);
$elapsed = microtime(true) - $start;
var_dump($n === 100000);          // 200k 'a' -> 100k 'aa'
var_dump($elapsed < 2.0);          // must not be quadratic
?>
--EXPECT--
bool(true)
bool(true)
```

- [ ] **Step 3: Run the tests**

```bash
php -d extension=$(pwd)/modules/tokenizers.so tests/014-conformance.phpt
php -d extension=$(pwd)/modules/tokenizers.so tests/015-worstcase.phpt
```
Expected: `ALL CONFORMANT`; both `bool(true)`. If the conformance test reports a MISMATCH, the pre-tokenizer regex semantics under PCRE2 differ from the reference — inspect the failing case, adjust the pattern in `Encoding.php`, and re-run. Do not proceed until conformant.

- [ ] **Step 4: Commit**

```bash
git add tests/reference/generate_fixtures.py tests/reference/fixtures/conformance.json tests/014-conformance.phpt tests/015-worstcase.phpt
git commit -m "Add byte-exact conformance suite vs tiktoken + worst-case guard"
```

---

## Task 15: CI matrix, packaging, and `.gitignore`

**Files:**
- Create: `.github/workflows/ci.yml`, `package.xml`, `.gitignore`

**Interfaces:**
- Produces: green CI across Linux/macOS × NTS/ZTS × PHP 8.3/8.4; a PECL-installable package.

- [ ] **Step 1: Add `.gitignore`**

```
*.o
*.lo
*.la
.libs/
modules/
autom4te.cache/
build/
config.h
config.h.in
config.nice
configure
configure.ac
libtool
Makefile
Makefile.fragments
Makefile.global
Makefile.objects
run-tests.php
*.dep
tests/*.diff
tests/*.out
tests/*.exp
tests/*.log
tests/*.php
tests/*.sh.bak
```

- [ ] **Step 2: Add CI**

`.github/workflows/ci.yml`:
```yaml
name: ci
on: [push, pull_request]
jobs:
  build-test:
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-latest, macos-latest]
        php: ['8.3', '8.4']
        zts: ['nts', 'zts']
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - uses: shivammathur/setup-php@v2
        with:
          php-version: ${{ matrix.php }}
          extensions: json
          ini-values: ${{ matrix.zts == 'zts' && 'zend.enable_gc=1' || '' }}
        # setup-php provides ZTS builds when available; otherwise the nts leg runs.
      - name: Install dev libs (pcre2 for cunit)
        run: |
          if [ "${{ matrix.os }}" = "macos-latest" ]; then brew install pcre2; else sudo apt-get update && sudo apt-get install -y libpcre2-dev; fi
      - name: C unit tests
        run: for t in model base64 loader_tiktoken pretok heap engine encode cache; do
               bash tests/cunit/run.sh "$t" $(ls src/*.c | grep -v bpe_class | tr '\n' ' '); done
      - name: Build extension
        run: phpize && ./configure --enable-tokenizers && make -j4
      - name: Cache vocab
        uses: actions/cache@v4
        with:
          path: ~/.cache/tokenizers
          key: vocab-v1
      - name: Run phpt
        run: |
          export NO_INTERACTION=1 REPORT_EXIT_STATUS=1
          php -d extension=$(pwd)/modules/tokenizers.so run-tests.php -p $(which php) -q tests/*.phpt || (cat tests/*.log; exit 1)
```
> The cunit step compiles each `tests/cunit/test_<t>.c` with the non-glue sources; `run.sh` ignores sources a given test doesn't reference at link time only if unused symbols are dropped — to be safe, pass the specific sources each test needs (as in each task's Step) rather than all. Adjust the loop to the per-test source lists from Tasks 2–9 if the broad link fails.

- [ ] **Step 3: Add `package.xml` for PECL**

Create a `package.xml` (PECL 2.0 schema) listing all `src/*.c`, `*.h`, `php/Tokenizers/Encoding.php`, `tokenizers.stub.php`, `tests/*.phpt`, `LICENSE`, `README.md`, with `<min>8.3.0</min>` in the PHP dependency and the extension `<providesextension>tokenizers</providesextension>`. Validate with `pecl package-validate`.

- [ ] **Step 4: Verify a clean package builds**

```bash
pecl package-validate package.xml && echo "package OK"
```
Expected: `package OK` (no errors).

- [ ] **Step 5: Commit**

```bash
git add .gitignore .github/workflows/ci.yml package.xml
git commit -m "Add CI matrix (Linux/macOS x NTS/ZTS x 8.3/8.4) and PECL packaging"
```

---

## Task 16: README and install docs

**Files:**
- Create: `README.md`

**Interfaces:** documentation only.

- [ ] **Step 1: Write `README.md`**

Cover, with runnable examples:
- **What it is:** native byte-level BPE tokenizer, tiktoken-compatible (`cl100k_base`, `o200k_base`) + HuggingFace `tokenizer.json` BPE models.
- **Why (positioning):** memory (vocab loaded once per worker, no ~26 MB/request rebuild), worst-case O(n log n), single `.so` with no Rust and no `ffi.enable` requirement. Explicitly state it is **not** faster than pure-PHP on small prompts — the win is memory / worst-case / installability.
- **Supported today / not:** OpenAI + open-weight tiktoken-style and HF-BPE models locally; **Claude 3+ and Gemini are not supported locally** (no public tokenizer — use the provider `count_tokens` API; planned as a separate Phase 3 companion).
- **Install:** `pecl install tokenizers` / PIE; enable in `php.ini`.
- **Usage:**
  ```php
  use Tokenizers\Encoding;
  $enc = Encoding::load('cl100k_base');          // downloads+caches vocab on first use
  $n   = $enc->countTokens($prompt);
  $ids = $enc->encode($prompt);
  echo $enc->decode($ids);
  $hf  = Encoding::fromHuggingFace('/path/tokenizer.json');
  ```
- **Cache dir:** `TOKENIZERS_CACHE_DIR` / `$XDG_CACHE_HOME/tokenizers`.
- **Roadmap:** Phase 2 (WordPiece/Unigram), Phase 3 (Claude/Gemini API companion).

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "Add README: positioning, install, usage, supported models, roadmap"
```

---

## Plan Self-Review

**1. Spec coverage** (each spec section → task):
- Native BPE engine, byte-exact cl100k/o200k → Tasks 2,3,4,5,6,7,8 + conformance Task 14. ✓
- Two loaders (tiktoken + HF `tokenizer.json` BPE) → Task 4 + Task 13. ✓
- Vocab loaded once per process, ZTS-safe → Task 9 (cache) + Task 10 (wiring). ✓
- O(n log n) worst case → Task 7 + Task 15 (`015-worstcase.phpt`). ✓
- OO API (`encode`/`decode`/`decodeSingle`/`countTokens`/`vocabSize`/`name`) → Task 10. ✓
- Procedural API → Task 11. ✓
- Special-token allowed/disallowed semantics → Task 8 + Task 10. ✓
- Known-encoding download/cache (no redistribution) + checksum → Task 12. ✓
- PHP 8.3+, NTS+ZTS, PECL/PIE → Task 1 (build), Task 15 (CI+package). ✓
- Conformance fixtures vs Python reference → Task 14. ✓
- Error handling via `TokenizerException` → Task 10 (registered), used across Tasks 10–13. ✓
- Cache dir = `$XDG_CACHE_HOME/tokenizers` configurable → Task 12 (`cacheDir`). ✓
- `p50k`/`r50k` deferred; Claude/Gemini deferred → documented in Task 16, not implemented. ✓

**2. Placeholder scan:** No "TBD/TODO". The only deferred-by-design items (checksums in Task 12, `package.xml` body in Task 15) are accompanied by exact commands to produce the real values — not placeholders. ✓

**3. Type/name consistency** (checked across tasks):
- `tk_model` accessors (`tk_model_rank`, `tk_model_bytes`, `tk_model_add_special`, `tk_model_special_*`, `tk_model__set_pcre2`, `tk_model__pattern_slot`, `tk_model_set_pattern`, `tk_model_set_pattern_str`) consistent between `model.h` (Tasks 2,5), `pretok.c` (5), `loader_tiktoken.c` (4), `engine.c` (7,8), `bpe_class.c` (10,13). ✓
- `tk_ids` / `tk_encode` / `tk_encode_ordinary` / `tk_count` / `tk_decode` signatures identical in `engine.h` (Task 7) and uses (Tasks 8,10,11). ✓
- `tk_cache_get_or_load` / `tk_cache_init` / `tk_cache_shutdown` / `tk_cache_count` consistent (Tasks 9,10,11). ✓
- PHP API names (`fromTiktokenFile`, `fromVocab`, `encode`, `decode`, `decodeSingle`, `countTokens`, `vocabSize`, `name`, `Encoding::load`, `Encoding::fromHuggingFace`, `tokenizers_encode/decode/count`, `tokenizers_cache_count`) consistent between stub and tests. ✓
- Heap key encoding `(rank<<32)|pos` identical in `heap` usage (Task 7) and `heap.c` ordering (Task 6). ✓

No issues found.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-29-tokenizers-bpe-phase1.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
