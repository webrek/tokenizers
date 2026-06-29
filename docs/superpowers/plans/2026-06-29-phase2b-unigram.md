# Phase 2b — Unigram Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use `- [ ]`.

**Goal:** Add a native Unigram (SentencePiece) tokenizer (`\Tokenizers\Unigram`) — Metaspace + Viterbi best-path — byte-exact with `transformers` on a curated ASCII/Latin set for one cased Unigram model (target: `t5-small`; fall back to `albert-base-v2` if cleaner).

**Architecture:** Reuse `tk_model` for piece→id; the Unigram object additionally holds a parallel `float scores[id]` array and `max_piece_len`. Encode = Metaspace (replace spaces with ▁ U+2581, optional prefix ▁) → Viterbi over the score lattice → backtrack. Class mirrors `wp_class.c`/`bpe_class.c`.

**Tech Stack:** C (Zend ext API), PHP 8.3+, `transformers` (fixtures only, committed). Same cunit + gen_stub + conformance patterns as Phase 1/2a.

## Global Constraints
- PHP 8.3+, NTS+ZTS. No build artifacts tracked. No Rust.
- **Normalization scope for v1:** Metaspace (▁ + add_prefix_space) only. SentencePiece "Precompiled"/NFKC normalization is treated as **identity on ASCII** — the conformance set is curated to ASCII/Latin where this holds. Inputs needing real NFKC decomposition are out of v1 scope and must be documented (NOT silently mis-tokenized in the conformance set). No lowercasing in v1 (target a cased model).
- Conformance is a release gate: any byte-level diff vs the committed `transformers` fixtures (on the curated set) fails the build.
- Reuse `\Tokenizers\TokenizerException` and the process-global cache.

## File Structure
```
src/unigram.h / unigram.c   — tk_unigram_encode(): Metaspace + Viterbi
src/ug_class.c              — \Tokenizers\Unigram Zend glue (holds tk_model + scores[] + max_piece_len)
config.m4 / config.w32      — add src/unigram.c, src/ug_class.c
tokenizers.stub.php         — add Unigram class; regenerate arginfo
php/Tokenizers/Encoding.php — fromHuggingFace dispatch for model.type=="Unigram"
tests/cunit/test_unigram.c, tests/03x-unigram*.phpt, generate_phase2_fixtures.py (Unigram section) + fixtures
```

---

## Task 1: Unigram Viterbi encode

**Files:** Create `src/unigram.h`, `src/unigram.c`, `tests/cunit/test_unigram.c`. Modify `config.m4`/`config.w32` (add `src/unigram.c`).

**Interfaces:**
- Consumes `tk_model` (piece→id via `tk_model_rank`).
- Produces:
  - `typedef struct { uint32_t unk_id; float unk_score; int add_prefix_space; size_t max_piece_len; } tk_ug_opts;`
  - `int tk_unigram_encode(const tk_model *m, const float *scores, const char *text, size_t len, const tk_ug_opts *o, uint32_t **out_ids, size_t *n_out);`
  - Metaspace: build a working buffer = (add_prefix_space ? "\xE2\x96\x81" : "") then copy text replacing each ASCII space (0x20) with "\xE2\x96\x81" (▁, U+2581). Then Viterbi over the buffer: `best[j]` = max total score to reach byte offset `j` (at a codepoint boundary); for each boundary `i`, for each end `j` in `(i, min(i+max_piece_len,len)]` at codepoint boundaries, if `buf[i:j]` is a known piece, relax `best[j] = best[i] + scores[id]` with back-pointer `(i, id)`. Single-codepoint fallback to `unk_id`/`unk_score` when no piece covers a position. Backtrack from `len` to 0, collect ids, reverse. Caller `free()`s `*out_ids`.

- [ ] **Step 1: Failing cunit**

`tests/cunit/test_unigram.c` (tiny vocab; ▁ = U+2581 = `\xE2\x96\x81`):
```c
#include "model.h"
#include "unigram.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    tk_model *m=tk_model_new(16);
    /* pieces: "▁he"=0 "llo"=1 "▁"=2 "h"=3 "e"=4 "l"=5 "o"=6 "<unk>"=7 */
    tk_model_add(m,(const uint8_t*)"\xE2\x96\x81he",5,0);
    tk_model_add(m,(const uint8_t*)"llo",3,1);
    tk_model_add(m,(const uint8_t*)"\xE2\x96\x81",3,2);
    tk_model_add(m,(const uint8_t*)"h",1,3);
    tk_model_add(m,(const uint8_t*)"e",1,4);
    tk_model_add(m,(const uint8_t*)"l",1,5);
    tk_model_add(m,(const uint8_t*)"o",1,6);
    tk_model_add(m,(const uint8_t*)"<unk>",5,7);
    /* scores: prefer "▁he"(-1) + "llo"(-1) over char-by-char (-2 each) */
    float sc[8]={-1,-1,-3,-2,-2,-2,-2,-10};
    tk_ug_opts o={7,-10.0f,1,5}; /* unk id 7, add prefix ▁, max piece len 5 bytes */
    uint32_t *ids; size_t n;
    assert(tk_unigram_encode(m,sc,"hello",5,&o,&ids,&n)==0);
    /* with add_prefix_space: "▁hello" -> "▁he"+"llo" = [0,1] */
    assert(n==2 && ids[0]==0 && ids[1]==1); free(ids);
    tk_model_free(m);
    printf("test_unigram OK\n"); return 0;
}
```

- [ ] **Step 2: RED** — `tests/cunit/run.sh unigram src/unigram.c src/model.c`

- [ ] **Step 3: Implement `src/unigram.{h,c}`**

`src/unigram.h`:
```c
#ifndef TK_UNIGRAM_H
#define TK_UNIGRAM_H
#include <stdint.h>
#include <stddef.h>
#include "model.h"
typedef struct { uint32_t unk_id; float unk_score; int add_prefix_space; size_t max_piece_len; } tk_ug_opts;
int tk_unigram_encode(const tk_model *m, const float *scores, const char *text, size_t len,
                      const tk_ug_opts *o, uint32_t **out_ids, size_t *n_out);
#endif
```
`src/unigram.c`:
```c
#include "unigram.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>

static const char MS[3]={(char)0xE2,(char)0x96,(char)0x81}; /* U+2581 */
static int boundary(const char *s,size_t i){ return (s[i]&0xC0)!=0x80; } /* codepoint start */

int tk_unigram_encode(const tk_model *m,const float *scores,const char *text,size_t len,
                      const tk_ug_opts *o,uint32_t **out_ids,size_t *n_out){
    /* Metaspace: prefix + replace spaces with ▁ */
    size_t cap=len*3+3; char *buf=malloc(cap?cap:1); size_t blen=0;
    if(o->add_prefix_space){ memcpy(buf,MS,3); blen=3; }
    for(size_t i=0;i<len;i++){
        if(text[i]==' '){ memcpy(buf+blen,MS,3); blen+=3; }
        else buf[blen++]=text[i];
    }
    size_t n=blen;
    /* Viterbi */
    float *best=malloc((n+1)*sizeof(float));
    size_t *bp_i=malloc((n+1)*sizeof(size_t));
    uint32_t *bp_id=malloc((n+1)*sizeof(uint32_t));
    for(size_t j=0;j<=n;j++){ best[j]=-FLT_MAX; bp_i[j]=(size_t)-1; bp_id[j]=o->unk_id; }
    best[0]=0;
    for(size_t i=0;i<n;i++){
        if(best[i]==-FLT_MAX) continue;
        if(!boundary(buf,i)) continue;
        int matched=0;
        size_t jmax = i+o->max_piece_len; if(jmax>n) jmax=n;
        for(size_t j=i+1;j<=jmax;j++){
            if(j<n && !boundary(buf,j)) continue; /* only at codepoint boundaries */
            uint32_t id=tk_model_rank(m,(const uint8_t*)(buf+i),j-i);
            if(id==TK_RANK_MAX) continue;
            float s=best[i]+scores[id];
            if(s>best[j]){ best[j]=s; bp_i[j]=i; bp_id[j]=id; matched=1; }
        }
        /* unk fallback: one codepoint */
        size_t e=i+1; while(e<n && !boundary(buf,e)) e++;
        if(best[i]+o->unk_score > best[e]){ best[e]=best[i]+o->unk_score; bp_i[e]=i; bp_id[e]=o->unk_id; }
        (void)matched;
    }
    /* backtrack */
    uint32_t *rev=malloc((n+1)*sizeof(uint32_t)); size_t rn=0;
    size_t j=n;
    while(j>0 && bp_i[j]!=(size_t)-1){ rev[rn++]=bp_id[j]; j=bp_i[j]; }
    uint32_t *ids=malloc((rn?rn:1)*sizeof(uint32_t));
    for(size_t k=0;k<rn;k++) ids[k]=rev[rn-1-k];
    free(rev); free(best); free(bp_i); free(bp_id); free(buf);
    *out_ids=ids; *n_out=rn; return 0;
}
```
Add `src/unigram.c` to `config.m4`/`config.w32`.

- [ ] **Step 4: GREEN** — `tests/cunit/run.sh unigram src/unigram.c src/model.c` → `test_unigram OK`.

- [ ] **Step 5: Commit** (explicit paths; no build artifacts).

---

## Task 2: `\Tokenizers\Unigram` class

**Files:** Create `src/ug_class.c`. Modify `tokenizers.stub.php` (+ regenerate arginfo), `config.m4`/`config.w32` (add `src/ug_class.c`), `tokenizers.c` (register in MINIT). Create `tests/030-unigram.phpt`.

**Interfaces:** Mirror `wp_class.c`. The object holds: an owned `tk_model` (pieces→id), a malloc'd `float *scores` (indexed by id), `max_piece_len`, and a `tk_ug_opts`. PHP API:
```php
Unigram::fromVocab(array $pieces, array $opts = []): Unigram   // $pieces = [[piece, score], ...] in id order
$ug->encode(string $text): array
$ug->decode(array $ids): string      // concat pieces, replace ▁ with space, ltrim one leading space
$ug->countTokens(string $text): int
$ug->vocabSize(): int
```
`$opts`: `unkId` (default: the id whose piece is `<unk>`, else 0), `addPrefixSpace` (default true). Construction: iterate `$pieces` (a list; index = id); add each piece string to the `tk_model` with its id; record `scores[id]` (cast to float) and track `max_piece_len` = max byte length of any piece. Resolve `unkId`.

- [ ] Steps mirror wp_class.c (Task 2a-3): write `tests/030-unigram.phpt` first (use the Task-1 tiny vocab as a `$pieces` list with scores; assert `encode('hello')` matches and `vocabSize`), build RED→GREEN, register class in MINIT, confirm the full prior phpt suite passes. Commit.

> Memory note: free `scores` in `free_obj` (it is malloc'd and owned). The decode ▁→space replacement: `▁` is the 3-byte `\xE2\x96\x81`.

---

## Task 3: HF `tokenizer.json` Unigram loader

**Files:** Modify `php/Tokenizers/Encoding.php` (add `model.type=="Unigram"` branch). Create `tests/031-unigram-hf.phpt`, `tests/fixtures/mini_ug.json`.

**Interfaces:** `Encoding::fromHuggingFace($path)` for `model.type=="Unigram"`: read `model.vocab` (a LIST of `[piece, score]` pairs, index = id), `model.unk_id`, the `pre_tokenizer` (Metaspace → `add_prefix_space`, replacement char — assert it is `▁`/U+2581 or default), and the `normalizer` (v1: accept Precompiled/NFKC/Sequence but apply NONE beyond Metaspace — document that normalization is identity-on-ASCII; if the normalizer would change ASCII, that's out of v1 scope). Build `$pieces` + opts → `Unigram::fromVocab`. The `mini_ug.json` fixture mirrors the Task-1 vocab as a Unigram tokenizer.json and asserts `encode('hello')`.

- [ ] Steps mirror Phase 2a-T4: phpt + fixture first (RED), implement the dispatch branch (GREEN), confirm BPE + WordPiece paths still pass. Commit.

---

## Task 4: Byte-exact conformance vs `transformers` (Unigram)

**Files:** Extend `tests/reference/generate_phase2_fixtures.py` (Unigram section), create `tests/reference/fixtures/unigram_conformance.json` + `tests/reference/fixtures/<model>_tokenizer.json` (committed), `tests/032-unigram-conformance.phpt`.

**Interfaces:** The generator loads `AutoTokenizer.from_pretrained('t5-small')` (a Unigram/SentencePiece Fast tokenizer; if its normalization makes byte-exact infeasible on ASCII, try `albert-base-v2` and document the choice), saves its `tokenizer.json`, and emits `{text, ids}` with `tok.encode(text, add_special_tokens=False)` for a **curated ASCII/Latin set** (English words, sentences, subwords, punctuation, numbers, multiple spaces, leading/trailing space, empty). The `.phpt` loads the committed tokenizer.json via `Encoding::fromHuggingFace` (→ Unigram) and asserts byte-exact with strict `!==`, a `$fail` counter, an empty-fixture guard, `ALL CONFORMANT` gate, and SKIPIF when the fixture is missing.

- [ ] **Steps:** write the generator + phpt, generate + commit fixtures, run. **Handle mismatches honestly:** report exact failing cases; a small principled fix to `src/unigram.c` (Viterbi tie-break order, Metaspace prefix handling) or the loader (unk_id, add_prefix_space) is allowed if a mismatch points to a specific bug; if a case-class needs real NFKC, narrow-and-document it. The Unigram tie-breaking and any normalization residue are the likely divergence points — pin them against the fixtures. Goal: ALL CONFORMANT on the curated set. If t5-small cannot reach byte-exact on ASCII within scope, switch the target model and document; if NO cased Unigram model reaches byte-exact, ship the class + loader with conformance marked as a documented known-limitation (do NOT fake it) and record it for follow-up.

---

## Plan Self-Review
- Spec coverage: Viterbi encode (T1) + class (T2) + HF loader (T3) + conformance (T4) cover the Unigram sub-phase. ✓
- No placeholders: the Viterbi core has full code; class/loader/conformance reference the established Phase 2a patterns with concrete fixtures.
- Type consistency: `tk_ug_opts`, `tk_unigram_encode`, scores-by-id consistent across tasks. ✓
- Risk (high, acknowledged in spec): Unigram byte-exact conformance (tie-break + normalization). T4 handles it with narrow-and-document / model-switch / honest known-limitation — never faking.

## Execution Handoff
Subagent-driven (autonomous goal). After Unigram T4, run ONE final whole-branch review covering ALL of Phase 2 (WordPiece + Unigram), then merge `phase2-wordpiece-unigram` → `main`.
