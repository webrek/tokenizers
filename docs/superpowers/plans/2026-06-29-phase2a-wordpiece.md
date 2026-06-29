# Phase 2a — WordPiece Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use `- [ ]`.

**Goal:** Add a native WordPiece tokenizer (`\Tokenizers\WordPiece`) — BERT-style normalization + greedy longest-match with `##`/`[UNK]` — byte-exact with `transformers` `BertTokenizerFast` on a curated English/Latin conformance set.

**Architecture:** Reuse Phase 1's `tk_model` (vocab hashmap + id↔bytes + cache) and conventions. Add `src/normalize.{h,c}` (BERT normalize + word split) and `src/wordpiece.{h,c}` (greedy encode). New Zend class mirrors `bpe_class.c`.

**Tech Stack:** C (Zend ext API), PHP 8.3+, system libpcre2 (already linked), `transformers` (fixtures only, committed). Same cunit harness (`tests/cunit/run.sh`) and `gen_stub` flow as Phase 1.

## Global Constraints
- PHP 8.3+, NTS+ZTS. No build artifacts tracked in git. No Rust. System `libpcre2-8` already linked.
- WordPiece normalization in C scoped to: ASCII + Latin-1 Supplement (U+00C0–U+00FF) lowercase & accent-strip, ASCII+common punctuation isolation, CJK ideograph spacing (U+4E00–U+9FFF etc.), control/whitespace cleanup. Inputs needing full NFD of other scripts are out of v1 scope and must be documented (NOT silently mis-tokenized in the conformance set).
- Conformance is a release gate: any byte-level diff vs the committed `transformers` fixtures fails the build.
- New classes reuse `\Tokenizers\TokenizerException` and the process-global cache.

## File Structure
```
src/normalize.h / normalize.c   — tk_bert_normalize(): normalized UTF-8 buffer + word spans
src/wordpiece.h / wordpiece.c   — tk_wordpiece_encode(): greedy match over a tk_model vocab
src/wp_class.c                  — \Tokenizers\WordPiece Zend glue (mirrors bpe_class.c)
config.m4 / config.w32          — add src/normalize.c, src/wordpiece.c, src/wp_class.c
tokenizers.stub.php             — add WordPiece class; regenerate arginfo
php/Tokenizers/Encoding.php     — fromHuggingFace dispatch for model.type=="WordPiece"; fromVocabFile helper
tests/cunit/test_normalize.c, test_wordpiece.c
tests/02x-wordpiece*.phpt, tests/reference/generate_phase2_fixtures.py + fixtures
```

---

## Task 1: BERT normalizer + word splitter

**Files:** Create `src/normalize.h`, `src/normalize.c`, `tests/cunit/test_normalize.c`. Modify `config.m4`/`config.w32` (add `src/normalize.c`).

**Interfaces:**
- Produces:
  - `typedef struct { size_t start, end; } tk_span;` (byte offsets into the normalized buffer)
  - `int tk_bert_normalize(const char *text, size_t len, int lowercase, int strip_accents, int handle_cjk, char **out, size_t *out_len, tk_span **spans, size_t *nspans);` — returns 0; caller `free()`s `*out` and `*spans`. Produces the normalized UTF-8 text and the list of word/punct spans (whitespace-split; each punctuation char and each CJK char is its own span).

- [ ] **Step 1: Write the failing cunit test**

`tests/cunit/test_normalize.c`:
```c
#include "normalize.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static int span_is(const char *buf, tk_span s, const char *exp){ size_t n=s.end-s.start; return n==strlen(exp) && memcmp(buf+s.start,exp,n)==0; }
int main(void){
    char *out; size_t olen; tk_span *sp; size_t ns;
    assert(tk_bert_normalize("Hello, World!", 13, 1,1,1, &out,&olen,&sp,&ns)==0);
    assert(ns==4 && span_is(out,sp[0],"hello") && span_is(out,sp[1],",") && span_is(out,sp[2],"world") && span_is(out,sp[3],"!"));
    free(out); free(sp);
    /* accent strip: café -> cafe */
    assert(tk_bert_normalize("caf\xC3\xA9", 5, 1,1,1, &out,&olen,&sp,&ns)==0);
    assert(ns==1 && span_is(out,sp[0],"cafe")); free(out); free(sp);
    /* CJK spacing: each ideograph isolated */
    assert(tk_bert_normalize("\xE4\xBD\xA0\xE5\xA5\xBD", 6, 1,1,1, &out,&olen,&sp,&ns)==0);
    assert(ns==2); free(out); free(sp);
    /* no-lowercase keeps case */
    assert(tk_bert_normalize("AB", 2, 0,0,0, &out,&olen,&sp,&ns)==0);
    assert(ns==1 && span_is(out,sp[0],"AB")); free(out); free(sp);
    printf("test_normalize OK\n"); return 0;
}
```

- [ ] **Step 2: Run it — RED**

Run: `tests/cunit/run.sh normalize src/normalize.c`  → compile error.

- [ ] **Step 3: Implement `src/normalize.{h,c}`**

`src/normalize.h`:
```c
#ifndef TK_NORMALIZE_H
#define TK_NORMALIZE_H
#include <stddef.h>
typedef struct { size_t start, end; } tk_span;
int tk_bert_normalize(const char *text, size_t len, int lowercase, int strip_accents,
                      int handle_cjk, char **out, size_t *out_len, tk_span **spans, size_t *nspans);
#endif
```

`src/normalize.c` — UTF-8 decode → per-codepoint transform (lowercase, strip-accents for Latin-1) → emit; classify each output codepoint as space / punct / cjk / wordchar; build spans (whitespace splits; punct & cjk each isolated):
```c
#include "normalize.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* minimal UTF-8 decode: returns codepoint, advances *i */
static uint32_t u8_next(const unsigned char *s, size_t len, size_t *i){
    uint32_t c=s[*i]; size_t n=1;
    if(c<0x80){} else if((c>>5)==0x6){c&=0x1F;n=2;} else if((c>>4)==0xE){c&=0x0F;n=3;} else if((c>>3)==0x1E){c&=0x07;n=4;} else {(*i)++;return 0xFFFD;}
    if(*i+n>len){(*i)=len;return 0xFFFD;}
    for(size_t k=1;k<n;k++) c=(c<<6)|(s[*i+k]&0x3F);
    *i+=n; return c;
}
static void u8_emit(char **buf,size_t *blen,size_t *bcap,uint32_t c){
    if(*blen+4>*bcap){*bcap=*bcap?*bcap*2:64;*buf=realloc(*buf,*bcap);}
    char *o=*buf+*blen;
    if(c<0x80){o[0]=c;*blen+=1;}
    else if(c<0x800){o[0]=0xC0|(c>>6);o[1]=0x80|(c&0x3F);*blen+=2;}
    else if(c<0x10000){o[0]=0xE0|(c>>12);o[1]=0x80|((c>>6)&0x3F);o[2]=0x80|(c&0x3F);*blen+=3;}
    else {o[0]=0xF0|(c>>18);o[1]=0x80|((c>>12)&0x3F);o[2]=0x80|((c>>6)&0x3F);o[3]=0x80|(c&0x3F);*blen+=4;}
}
/* Latin-1 Supplement accent strip (U+00C0..U+00FF -> ASCII base); 0 if none */
static uint32_t strip_latin1(uint32_t c){
    static const char *map="AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
    if(c>=0xC0 && c<=0xFF){ char b=map[c-0xC0]; if(b!='x'&&b!='/') return (uint32_t)(unsigned char)b; }
    return 0;
}
static uint32_t to_lower(uint32_t c){
    if(c>='A'&&c<='Z') return c+32;
    if(c>=0xC0&&c<=0xDE&&c!=0xD7) return c+0x20; /* Latin-1 uppercase -> lower */
    return c;
}
static int is_space(uint32_t c){ return c==' '||c=='\t'||c=='\n'||c=='\r'||c==0x0c||c==0xA0; }
static int is_control(uint32_t c){ return (c<0x20&&!is_space(c))||c==0x7f; }
static int is_cjk(uint32_t c){ return (c>=0x4E00&&c<=0x9FFF)||(c>=0x3400&&c<=0x4DBF)||(c>=0xF900&&c<=0xFAFF)||(c>=0x20000&&c<=0x2A6DF); }
static int is_punct(uint32_t c){
    if((c>=33&&c<=47)||(c>=58&&c<=64)||(c>=91&&c<=96)||(c>=123&&c<=126)) return 1; /* ASCII punct */
    return 0;
}

int tk_bert_normalize(const char *text,size_t len,int lowercase,int strip_accents,int handle_cjk,
                      char **out,size_t *out_len,tk_span **spans,size_t *nspans){
    char *buf=NULL; size_t blen=0,bcap=0;
    tk_span *sp=NULL; size_t ns=0,scap=0;
    int in_word=0; size_t word_start=0;
    #define ENDWORD() do{ if(in_word){ if(ns==scap){scap=scap?scap*2:16;sp=realloc(sp,scap*sizeof(tk_span));} sp[ns].start=word_start; sp[ns].end=blen; ns++; in_word=0;} }while(0)
    #define SOLO(emitc) do{ ENDWORD(); size_t s0=blen; u8_emit(&buf,&blen,&bcap,(emitc)); if(ns==scap){scap=scap?scap*2:16;sp=realloc(sp,scap*sizeof(tk_span));} sp[ns].start=s0; sp[ns].end=blen; ns++; }while(0)
    size_t i=0;
    while(i<len){
        uint32_t c=u8_next((const unsigned char*)text,len,&i);
        if(c==0||is_control(c)) continue;                 /* drop control/replacement */
        if(is_space(c)){ ENDWORD(); continue; }
        if(handle_cjk && is_cjk(c)){ SOLO(c); continue; }
        if(is_punct(c)){ SOLO(c); continue; }
        if(strip_accents){ uint32_t b=strip_latin1(c); if(b){ c=b; } }
        if(lowercase) c=to_lower(c);
        if(strip_accents){ uint32_t b=strip_latin1(c); if(b) c=b; } /* re-strip after lower */
        if(!in_word){ in_word=1; word_start=blen; }
        u8_emit(&buf,&blen,&bcap,c);
    }
    ENDWORD();
    if(!buf){ buf=malloc(1); }
    *out=buf; *out_len=blen; *spans=sp; *nspans=ns;
    return 0;
    #undef ENDWORD
    #undef SOLO
}
```
Add `src/normalize.c` to the `PHP_NEW_EXTENSION` source list in `config.m4` and the `EXTENSION(...)` list in `config.w32`.

- [ ] **Step 4: Run — GREEN**

Run: `tests/cunit/run.sh normalize src/normalize.c` → `test_normalize OK`.

- [ ] **Step 5: Commit**

```bash
git add src/normalize.h src/normalize.c tests/cunit/test_normalize.c config.m4 config.w32
git commit -m "Add BERT normalizer + word/punct/CJK splitter"
```

---

## Task 2: WordPiece greedy encode

**Files:** Create `src/wordpiece.h`, `src/wordpiece.c`, `tests/cunit/test_wordpiece.c`. Modify `config.m4`/`config.w32` (add `src/wordpiece.c`).

**Interfaces:**
- Consumes `tk_model` (rank lookup as token→id), `tk_bert_normalize`.
- Produces:
  - `typedef struct { uint32_t unk_id; const char *prefix; size_t prefix_len; size_t max_chars; int lowercase, strip_accents, handle_cjk; } tk_wp_opts;`
  - `int tk_wordpiece_encode(const tk_model *m, const char *text, size_t len, const tk_wp_opts *o, uint32_t **out_ids, size_t *n_out);` — normalize+split, then per word: if char-count > max_chars → unk; else greedy longest-match from start (prefix `##` for non-initial pieces); on any miss the whole word → unk. Caller `free()`s `*out_ids`.

- [ ] **Step 1: Failing cunit**

`tests/cunit/test_wordpiece.c`:
```c
#include "model.h"
#include "wordpiece.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    tk_model *m=tk_model_new(16);
    /* vocab: "un"=0 "##aff"=1 "##able"=2 "[UNK]"=3 "play"=4 "##ing"=5 */
    tk_model_add(m,(const uint8_t*)"un",2,0);
    tk_model_add(m,(const uint8_t*)"##aff",5,1);
    tk_model_add(m,(const uint8_t*)"##able",6,2);
    tk_model_add(m,(const uint8_t*)"[UNK]",5,3);
    tk_model_add(m,(const uint8_t*)"play",4,4);
    tk_model_add(m,(const uint8_t*)"##ing",5,5);
    tk_wp_opts o={3,"##",2,100,1,1,1};
    uint32_t *ids; size_t n;
    assert(tk_wordpiece_encode(m,"unaffable",9,&o,&ids,&n)==0);
    assert(n==3 && ids[0]==0 && ids[1]==1 && ids[2]==2); free(ids);     /* un ##aff ##able */
    assert(tk_wordpiece_encode(m,"playing",7,&o,&ids,&n)==0);
    assert(n==2 && ids[0]==4 && ids[1]==5); free(ids);                  /* play ##ing */
    assert(tk_wordpiece_encode(m,"zzz",3,&o,&ids,&n)==0);
    assert(n==1 && ids[0]==3); free(ids);                              /* [UNK] */
    tk_model_free(m);
    printf("test_wordpiece OK\n"); return 0;
}
```

- [ ] **Step 2: RED** — `tests/cunit/run.sh wordpiece src/wordpiece.c src/normalize.c src/model.c`

- [ ] **Step 3: Implement `src/wordpiece.{h,c}`**

`src/wordpiece.h`:
```c
#ifndef TK_WORDPIECE_H
#define TK_WORDPIECE_H
#include <stdint.h>
#include "model.h"
typedef struct { uint32_t unk_id; const char *prefix; size_t prefix_len; size_t max_chars;
                 int lowercase, strip_accents, handle_cjk; } tk_wp_opts;
int tk_wordpiece_encode(const tk_model *m, const char *text, size_t len, const tk_wp_opts *o,
                        uint32_t **out_ids, size_t *n_out);
#endif
```
`src/wordpiece.c`:
```c
#include "wordpiece.h"
#include "normalize.h"
#include <stdlib.h>
#include <string.h>
/* count UTF-8 codepoints in [s,s+n) */
static size_t cp_count(const char *s,size_t n){ size_t c=0; for(size_t i=0;i<n;i++) if((s[i]&0xC0)!=0x80) c++; return c; }
/* advance one codepoint */
static size_t cp_adv(const char *s,size_t n,size_t i){ i++; while(i<n && (s[i]&0xC0)==0x80) i++; return i; }

int tk_wordpiece_encode(const tk_model *m,const char *text,size_t len,const tk_wp_opts *o,
                        uint32_t **out_ids,size_t *n_out){
    char *norm; size_t nlen; tk_span *sp; size_t ns;
    tk_bert_normalize(text,len,o->lowercase,o->strip_accents,o->handle_cjk,&norm,&nlen,&sp,&ns);
    uint32_t *ids=NULL; size_t cap=0,no=0;
    char tmp[512];
    for(size_t w=0;w<ns;w++){
        const char *word=norm+sp[w].start; size_t wlen=sp[w].end-sp[w].start;
        size_t emit_start=no; int bad=0;
        if(cp_count(word,wlen) > o->max_chars){ bad=1; }
        size_t start=0;
        while(!bad && start<wlen){
            size_t end=wlen, best_end=0; uint32_t best_id=TK_RANK_MAX;
            /* longest match from start, shrinking end by codepoints */
            while(end>start){
                size_t plen=end-start; const char *piece=word+start;
                uint32_t id;
                if(start>0){ /* prepend prefix */
                    if(o->prefix_len+plen<=sizeof(tmp)){ memcpy(tmp,o->prefix,o->prefix_len); memcpy(tmp+o->prefix_len,piece,plen);
                        id=tk_model_rank(m,(const uint8_t*)tmp,o->prefix_len+plen); }
                    else id=TK_RANK_MAX;
                } else id=tk_model_rank(m,(const uint8_t*)piece,plen);
                if(id!=TK_RANK_MAX){ best_end=end; best_id=id; break; }
                /* shrink end by one codepoint */
                size_t e=start; while(e<end){ size_t ne=cp_adv(word,wlen,e); if(ne>=end) break; e=ne; } end=e;
                if(end<=start) break;
            }
            if(best_id==TK_RANK_MAX){ bad=1; break; }
            if(no==cap){cap=cap?cap*2:16; ids=realloc(ids,cap*sizeof(uint32_t));}
            ids[no++]=best_id; start=best_end;
        }
        if(bad){ no=emit_start; if(no==cap){cap=cap?cap*2:16; ids=realloc(ids,cap*sizeof(uint32_t));} ids[no++]=o->unk_id; }
    }
    free(norm); free(sp);
    *out_ids=ids; *n_out=no; return 0;
}
```
Add `src/wordpiece.c` to `config.m4`/`config.w32`.

- [ ] **Step 4: GREEN** — `tests/cunit/run.sh wordpiece src/wordpiece.c src/normalize.c src/model.c` → `test_wordpiece OK`.

- [ ] **Step 5: Commit**
```bash
git add src/wordpiece.h src/wordpiece.c tests/cunit/test_wordpiece.c config.m4 config.w32
git commit -m "Add WordPiece greedy longest-match encode"
```

---

## Task 3: `\Tokenizers\WordPiece` class

**Files:** Create `src/wp_class.c`. Modify `tokenizers.stub.php` (+ regenerate arginfo), `config.m4`/`config.w32` (add `src/wp_class.c`), `tokenizers.c` (register the class in MINIT). Create `tests/020-wordpiece.phpt`.

**Interfaces:** Mirror `bpe_class.c`'s object pattern (owned `tk_model`, `owns=1`). PHP API:
```php
WordPiece::fromVocab(array $tokenToId, array $opts = []): WordPiece
$wp->encode(string $text): array
$wp->decode(array $ids): string         // join tokens, strip "##", spaces between words
$wp->countTokens(string $text): int
$wp->vocabSize(): int
```
`$opts` keys: `unkToken` (default `[UNK]`), `continuingSubwordPrefix` (`##`), `maxInputCharsPerWord` (100), `lowercase` (true), `stripAccents` (true), `handleChineseChars` (true). Resolve `unkToken` → its id from the vocab at construction; error if absent.

- [ ] **Step 1** Write `tests/020-wordpiece.phpt`:
```
--TEST--
WordPiece::fromVocab encode/count
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\WordPiece;
$v = ['un'=>0,'##aff'=>1,'##able'=>2,'[UNK]'=>3,'play'=>4,'##ing'=>5];
$wp = WordPiece::fromVocab($v);
echo implode(',', $wp->encode('unaffable playing')), "\n"; // 0,1,2,4,5
echo $wp->countTokens('zzz'), "\n";                         // 1  ([UNK])
echo $wp->vocabSize(), "\n";                                 // 6
?>
--EXPECT--
0,1,2,4,5
1
6
```
- [ ] **Step 2: RED** — build + run → class not found.
- [ ] **Step 3: Implement `src/wp_class.c`** — copy the object scaffolding from `src/bpe_class.c` (create_object/free_obj/handlers/offset, `owns=1` owned model, `clone_obj=NULL`). `fromVocab`: build a `tk_model` from the `$tokenToId` array (use `ZEND_HASH_FOREACH_KEY_VAL` to handle integer-coerced keys, same as Phase 1's `fromVocab` fix), resolve opts, store a `tk_wp_opts` on the object (with the resolved unk id, throwing if unk token absent). `encode`/`countTokens` call `tk_wordpiece_encode`; `decode` concatenates `tk_model_bytes(id)` stripping a leading `##` and inserting a space before non-`##` pieces (except the first). Register `\Tokenizers\WordPiece` in MINIT (a `tk_register_wp_class()` called alongside the Bpe registration).
- [ ] **Step 4: GREEN** — `phpize && ./configure --enable-tokenizers && make && php -d extension=modules/tokenizers.so tests/020-wordpiece.phpt`. Confirm Phase 1 phpts still pass.
- [ ] **Step 5: Commit** (stub, arginfo, src/wp_class.c, tokenizers.c, config.m4/w32, test — explicit paths).

---

## Task 4: Loaders — `vocab.txt` + HF `tokenizer.json` (WordPiece)

**Files:** Modify `php/Tokenizers/Encoding.php` (add `WordPiece::fromVocabFile` helper as `Encoding::wordPieceFromVocabFile` OR a static on WordPiece via the shim; and extend `fromHuggingFace` to dispatch model.type). Create `tests/021-wordpiece-hf.phpt`, `tests/fixtures/mini_wp_vocab.txt`, `tests/fixtures/mini_wp.json`.

**Interfaces:** `Encoding::fromHuggingFace($path)` returns `WordPiece` when `model.type=="WordPiece"`: read `model.vocab` (token→id), `model.unk_token`, `model.continuing_subword_prefix`, `model.max_input_chars_per_word`, and the `normalizer` (BertNormalizer: lowercase/strip_accents/handle_chinese_chars) → build opts → `WordPiece::fromVocab`. `vocab.txt` loader: read lines (token per line, id = line number) → `WordPiece::fromVocab`.

- [ ] Steps mirror Phase 1's loader tasks: write the two phpts + tiny fixtures first (RED), implement the PHP dispatch + vocab.txt reader (GREEN), commit. The `mini_wp.json` fixture uses `{"model":{"type":"WordPiece","vocab":{...},"unk_token":"[UNK]","continuing_subword_prefix":"##","max_input_chars_per_word":100},"normalizer":{"type":"BertNormalizer","lowercase":true,"strip_accents":null,"handle_chinese_chars":true}}` and asserts encode matches Task 3's expectation.

---

## Task 5: Byte-exact conformance vs `transformers` BertTokenizerFast

**Files:** Create `tests/reference/generate_phase2_fixtures.py` (WordPiece section), `tests/reference/fixtures/wordpiece_conformance.json` (committed), `tests/022-wordpiece-conformance.phpt`.

**Interfaces:** The generator loads `BertTokenizerFast.from_pretrained('bert-base-uncased')` and emits `(text, input_ids_without_special_tokens)` for a curated English/Latin set (words, subwords, punctuation, numbers, accented Latin like "café", mixed case, simple CJK). The `.phpt` loads the same vocab via `Encoding::fromHuggingFace` on the model's `tokenizer.json` (downloaded+cached on first use; SKIPIF when offline) and asserts byte-exact `encode` for every case; prints `ALL CONFORMANT` only on zero mismatches.

- [ ] **Steps:** write the generator (only cases within the documented normalization coverage — no exotic scripts needing full NFD), generate + commit the fixtures, write the conformance `.phpt` (strict `!==`, failure counter, SKIPIF), run it. If mismatches appear, report the exact cases; a small, principled fix to `normalize.c`'s tables (e.g. an accent or punctuation gap) is allowed, but do NOT change expected values. Commit.

> If conformance cannot reach byte-exact for the full set, narrow the documented coverage (drop the unsupported case class from the set AND note it in the README/spec as a v1 limitation) rather than masking a real bug.

---

## Plan Self-Review
- Spec coverage: normalizer (T1) + WordPiece encode (T2) + class (T3) + loaders (T4) + conformance (T5) cover the WordPiece sub-phase goals. ✓
- No placeholders: algorithmic cores (normalize, greedy encode) have full code; class/loader/conformance reference the established Phase 1 patterns with concrete fixtures and expectations.
- Type consistency: `tk_span`, `tk_wp_opts`, `tk_wordpiece_encode`, `tk_model_*` consistent across tasks. ✓
- Risk: normalization coverage is the conformance variable — T5 handles it explicitly (narrow-and-document, never fake).

## Execution Handoff
Subagent-driven (this is part of an autonomous goal): dispatch one implementer per task, per-task review, fix loop, then a final whole-branch review before merge to `main`.
