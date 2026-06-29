# Design Spec — `tokenizers` Phase 2: WordPiece + Unigram

- **Date:** 2026-06-29
- **Status:** Approved (autonomous design under standing goal "dale con los dos"; decisions documented for audit)
- **Builds on:** Phase 1 (native BPE engine, merged to `main`). Reuses `tk_model`, the cache, the cunit harness, `gen_stub` flow, and the conformance-fixture pattern.

## 1. Problem & motivation

Phase 1 covers byte-level **BPE** (OpenAI tiktoken + HF-BPE models). The two other algorithms that the HuggingFace `tokenizer.json` format expresses — **WordPiece** (BERT family) and **Unigram** (SentencePiece: T5, ALBERT, many multilingual) — are not BPE and need their own encode paths. Adding them in the same `.so` unlocks BERT/DistilBERT/Electra (WordPiece) and T5/ALBERT/mBART-style models (Unigram), keeping the "one native extension, no Rust" story.

## 2. Goals & non-goals

### Goals (Phase 2)
- **WordPiece** tokenizer: greedy longest-match-first with `##` continuation and `[UNK]` fallback; BERT normalization (lowercase/strip-accents/CJK/punctuation-split, configurable cased/uncased). Byte-exact with `transformers` `BertTokenizerFast` on a curated conformance set (target: `bert-base-uncased`).
- **Unigram** tokenizer: Viterbi best-path over the vocab lattice using per-piece log-scores; `<unk>` fallback; Metaspace pre-tokenizer (`▁`, add_prefix_space) + NFKC-style normalization. Byte-exact with `transformers` on a curated set (target: a small Unigram model, e.g. `albert-base-v2` or `t5-small`).
- Load both from **HuggingFace `tokenizer.json`** (`model.type` of `"WordPiece"` / `"Unigram"`), plus a direct `vocab.txt` loader for WordPiece.
- New PHP classes `\Tokenizers\WordPiece` and `\Tokenizers\Unigram` with `encode`/`decode`/`countTokens`/`vocabSize`; share the process-global cache and `TokenizerException`.
- Same build/test rigor as Phase 1: cunit for pure-C units, `.phpt` for PHP-visible behavior, byte-exact `.phpt` conformance gated on committed fixtures.

### Non-goals (deferred)
- Training models. Full generality of every HF normalizer/pre-tokenizer (only the configs the target models use are supported in v1; unsupported configs **fail loudly**).
- Byte-fallback Unigram variants beyond the targeted models; SentencePiece `.model` protobuf loading (HF `tokenizer.json` only in v1).
- BPE-dropout, custom merges re-ranking.

## 3. Scope summary

| In Phase 2 v1 | Deferred |
|---|---|
| WordPiece encode (greedy `##`, `[UNK]`) + BERT normalizer/pre-tokenizer | Arbitrary normalizer pipelines |
| Unigram encode (Viterbi) + Metaspace + NFKC normalizer | SentencePiece `.model` protobuf |
| Loaders: HF `tokenizer.json` (WordPiece, Unigram) + `vocab.txt` (WordPiece) | Training, byte-fallback edge variants |
| Classes `\Tokenizers\WordPiece`, `\Tokenizers\Unigram` | A unified auto-dispatch facade (Phase 2.1) |
| Conformance `.phpt` vs `transformers` for one model each | — |

## 4. Architecture

Reuse the Phase 1 internal `tk_model` (vocab hashmap + id↔bytes + specials + cache). Add two algorithm modules and one normalizer module:

```
src/normalize.{h,c}   — text normalization primitives:
    NFC/NFKC (via PHP's intl/Normalizer at the PHP-shim layer where possible; C-side: a
    minimal, table-driven NFKC for the target models + lowercase + strip-accents + CJK
    spacing + control-char cleanup). Metaspace (▁ + add_prefix_space).
src/wordpiece.{h,c}   — tk_wordpiece_encode(model, text, opts, out_ids):
    pre-normalize → split (whitespace + punctuation) → per-word greedy longest-match
    using vocab and '##' continuation; [UNK] on miss / over-max-chars.
src/unigram.{h,c}     — tk_unigram_encode(model, text, opts, out_ids):
    normalize → Metaspace → Viterbi best-path over the score lattice; <unk> on miss.
src/wp_class.c        — \Tokenizers\WordPiece (Zend glue)
src/ug_class.c        — \Tokenizers\Unigram (Zend glue)
php/Tokenizers/Encoding.php — extend fromHuggingFace to dispatch on model.type
```

The `tk_model` gains optional parallel arrays for Unigram **scores** (float per id) and config fields (unk id, continuation prefix, max_input_chars). These are populated only by the relevant loader; BPE/WordPiece ignore scores.

### Decisions (documented)
- **D1 — Separate classes, not a unified facade.** `WordPiece`/`Unigram`/`Bpe` are distinct classes (matches the Phase 1 roadmap and keeps each encode path/contract clear). A unified auto-dispatch `Encoding::fromHuggingFace` returns the right concrete type by `model.type`.
- **D2 — Normalization split.** Heavy/standard Unicode normalization (NFC/NFKC) is done where a reliable implementation exists: the **PHP shim normalizes vocab/keys at load** and the C encode path applies the model's **lightweight** runtime normalization (lowercase, strip-accents for the BERT case, Metaspace, control cleanup). Full NFKC of arbitrary input text in C is scoped to a table covering the target models; inputs needing unsupported normalization are documented. This keeps conformance achievable without a full ICU dependency.
- **D3 — Conformance per algorithm, one model each, committed fixtures** generated by `transformers` (no network/Python in CI). Byte-exact gate; any mismatch fails the build. WordPiece lands first (more tractable); Unigram second.
- **D4 — `tk_model` reuse.** No new core data structure; extend with optional score array + a small `algo_config` struct. Keeps the cache and memory model unchanged.

## 5. Public API

```php
namespace Tokenizers;

final class WordPiece {
    public static function fromVocabFile(string $path, array $opts = []): WordPiece; // vocab.txt
    public static function fromVocab(array $tokenToId, array $opts = []): WordPiece;
    public function encode(string $text): array;       // int[]
    public function countTokens(string $text): int;
    public function decode(array $ids): string;
    public function vocabSize(): int;
}
final class Unigram {
    public static function fromVocab(array $pieces, array $opts = []): Unigram; // [[piece, score], ...]
    public function encode(string $text): array;
    public function countTokens(string $text): int;
    public function decode(array $ids): string;
    public function vocabSize(): int;
}
// Encoding::fromHuggingFace(path) now returns Bpe|WordPiece|Unigram by model.type.
```
`$opts` for WordPiece: `unkToken` (default `[UNK]`), `continuingSubwordPrefix` (default `##`), `maxInputCharsPerWord` (default 100), `lowercase`/`stripAccents` (default from model). For Unigram: `unkId`, `addPrefixSpace`.

## 6. Algorithms (the substance)

- **WordPiece encode:** normalize → split into words (whitespace; each punctuation char its own token) → for each word: if `len > maxInputCharsPerWord` → `[UNK]`; else greedy: from `start`, find the longest substring `word[start:end]` in vocab (prefixed `##` when `start>0`), advance; if no substring matches → whole word is `[UNK]`. O(word_len²) per word, fine for real words.
- **Unigram encode:** normalize → Metaspace → build a lattice: `best[i]` = best score to reach char `i`; for each `i`, for each piece matching at `i`, relax `best[j] = max(best[j], best[i] + score(piece))`; backtrack for the segmentation. Unknown single chars → `<unk>` with the model's unk score. Ties broken to match SentencePiece (longest/first by id — to be pinned against the reference during conformance).

## 7. Testing & conformance
- Pure-C cunit for `wordpiece_encode`, `unigram_encode` (Viterbi), and `normalize` primitives, with hand-built tiny vocabs and known outputs.
- `.phpt` for class behavior + loaders.
- **Conformance `.phpt`**: `tests/reference/generate_phase2_fixtures.py` uses `transformers` to emit `(text, ids)` for one WordPiece model and one Unigram model; committed JSON; the `.phpt` asserts byte-exact `encode` (SKIPIF if the model/vocab can't be fetched offline).
- Worst-case guard for Unigram Viterbi (long input stays linear-ish, no blowup).

## 8. Build & risks
- Same toolchain; new source files added to `config.m4`/`config.w32`. Still links system `libpcre2-8` (reused for any regex pre-tokenization).
- **Risk (high): Unigram byte-exact conformance** — Viterbi tie-breaking, score precision (store scores as the model's float; compare paths with a stable rule), and SentencePiece normalization quirks (NFKC, `▁`, prefix space, multiple-space collapse) are where most ports diverge. Mitigation: pin behavior against the committed `transformers` fixtures; land WordPiece first so Phase 2 has a shippable deliverable even if Unigram needs iteration; document any residual edge cases as known limitations rather than faking conformance.
- **Risk (medium): normalization in C** without ICU — mitigated by D2 (shim-side normalization + a target-model-scoped C table) and by failing loudly on unsupported configs.

## 9. Sub-phase order (for the plan)
1. **WordPiece** (normalizer subset → encode → class → vocab.txt + HF loader → conformance vs BERT).
2. **Unigram** (Metaspace/NFKC subset → Viterbi encode → class → HF loader → conformance vs the target model).
Each is independently shippable; if Unigram conformance proves intractable in v1, WordPiece still merges and Unigram ships with documented limitations.
