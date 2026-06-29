# Design Spec — `tokenizers` PHP extension, Phase 1: native BPE engine

- **Date:** 2026-06-29
- **Status:** Approved design — pending spec review before implementation planning
- **Author:** Arturo Hernandez
- **Scope of this document:** Phase 1 only (native byte-level BPE engine). Phases 2 and 3 are described at a high level for context but are out of scope for implementation.

---

## 1. Problem & motivation

PHP applications increasingly need to **count and produce tokens** for Large Language Models: cost-guarding before an API call, chunking text for RAG, enforcing context limits, and budgeting prompts. Today the de-facto solution is the pure-PHP port `yethee/tiktoken` (~3.96M installs, ~266k/month). It works, but has two structural weaknesses pure PHP cannot fix:

1. **Memory:** it rebuilds a ~26 MB encoder table **per request** (`yethee/tiktoken` issue #21).
2. **Worst case:** the merge loop degrades to quadratic time on pathological long single tokens (`openai/tiktoken` issue #391), which can hang a worker.

Its optional FFI "lib mode" binds the Rust `tiktoken-rs`, but requires a Rust toolchain (`cargo`, Rust ≥ 1.85), ships no prebuilt binary, and is frequently unavailable because `ffi.enable` is disabled on shared/managed hosts.

A compiled C extension is the right tool because it can:

- Load each vocabulary **once per worker process** into shared, read-only memory (eliminating the per-request rebuild).
- Guarantee **O(n log n)** worst-case tokenization with a heap-based merge.
- Install as a **single `.so`** with **no Rust toolchain** and work even where `ffi.enable` is off.

The key technical insight unifying the broader vision: OpenAI tiktoken, Llama 3.x, Mistral "tekken", Qwen, DeepSeek, GPT-2, and RoBERTa all use the **same algorithm** — byte-level BPE with merge ranks plus a regex pre-tokenizer. They differ only in their **vocabulary table** and **pre-tokenizer regex**. Therefore one BPE engine, fed by multiple format loaders, covers all of them.

## 2. Goals & non-goals

### Goals (Phase 1)

- A native PECL/PIE extension named **`tokenizers`** exposing a byte-level BPE tokenizer.
- **Byte-exact conformance** with the Python `tiktoken` reference for `cl100k_base` and `o200k_base`.
- Load arbitrary BPE vocabularies from **two formats**: tiktoken `.tiktoken` files and HuggingFace `tokenizer.json` (BPE models only).
- Vocab loaded **once per process**, shared read-only across all requests, **ZTS-safe**.
- **O(n log n)** worst-case tokenization.
- Idiomatic **OO API** plus a **procedural** convenience layer.
- Works on **PHP 8.3+**, both **NTS and ZTS**.

### Non-goals (deferred to later phases / versions)

- WordPiece (BERT) and Unigram/SentencePiece (T5, multilingual) algorithms → **Phase 2**.
- SentencePiece `.model` protobuf loader → Phase 2/3.
- Claude 3+ / Gemini token counting (no public local tokenizer; API-only) → **Phase 3**, as a separate thin PHP companion, *not* in the C core.
- Training new vocabularies / merges.
- Legacy OpenAI encodings `p50k_base` / `r50k_base` (trivial to add later; left out of v1 to keep the conformance surface small).
- Full generality of HuggingFace `tokenizer.json` normalizer/pre-tokenizer pipelines (only the common ByteLevel-BPE configuration is supported in v1; unsupported configs fail loudly).

## 3. Scope summary

| In v1 | Deferred |
|-------|----------|
| BPE engine (merge ranks + PCRE2-JIT pre-tokenizer + special tokens) | WordPiece, Unigram, SentencePiece |
| Loaders: tiktoken format, HF `tokenizer.json` (BPE subset) | HF normalizer/pre-tokenizer pipelines beyond ByteLevel |
| Encodings out-of-the-box: `cl100k_base`, `o200k_base` | `p50k_base`, `r50k_base` |
| API: `encode`, `decode`, `decodeSingle`, `countTokens` (OO + procedural) | Training, vocab editing |
| Shared process-global vocab cache (ZTS-safe) | Claude/Gemini API companion |
| Conformance `.phpt` suite vs Python reference | Prebuilt binary distribution matrix (best-effort later) |

## 4. Architecture

```
\Tokenizers\Bpe  (PHP-visible class)
  │
  ├─ Loaders (populate the same internal model)
  │   ├─ fromEncoding('cl100k_base'|'o200k_base')   ─ resolved + cached by a thin PHP shim, then path handed to C
  │   ├─ fromTiktokenFile(path, regex, specials)     ─ Llama 3.x, Mistral tekken, Qwen, DeepSeek, …
  │   └─ fromHuggingFace('tokenizer.json')           ─ GPT-2, RoBERTa, … (ByteLevel-BPE subset)
  │
  ├─ Internal model (the single representation all loaders build)
  │   ├─ merge-rank map:  byte-sequence → rank (uint32)
  │   ├─ vocab maps:      token-id ↔ byte-sequence
  │   ├─ special tokens:  literal string → id  (+ reverse)
  │   └─ pre-tokenizer:   compiled PCRE2 pattern (+ JIT)
  │
  ├─ Engine (C)
  │   ├─ pre-tokenize:  PCRE2 split into pieces
  │   ├─ merge:         per-piece byte-pair merge using a min-heap on ranks  → O(n log n)
  │   ├─ special-token scan: allowed/disallowed handling (tiktoken semantics)
  │   └─ count fast-path: counts tokens without materializing the id array
  │
  └─ Process-global cache
      └─ key (encoding name | canonical path+mtime) → parsed model, loaded once, read-only
```

### 4.1 Component responsibilities

- **PHP shim (`src/php/`)** — a small bundled PHP layer that resolves a known encoding name (`cl100k_base`, `o200k_base`) to a local file: download from the official public URL on first use, cache under a per-user directory (e.g. `$XDG_CACHE_HOME/tokenizers/` or system temp), verify a known checksum, and hand the **path** to the C extension. Keeps networking and licensing concerns out of the C core (we never redistribute vocab files). Cache location and the download URL/checksum table are configurable.
- **Loaders (C)** — parse a vocab source into the internal model:
  - *tiktoken loader*: each line is `base64(token_bytes) SPACE rank`. Regex and special tokens supplied by the caller (the shim supplies the canonical regex + specials for the two built-in encodings).
  - *HF loader*: parse `tokenizer.json`; extract the `model` (must be `type: "BPE"`), its `vocab` and `merges`, the ByteLevel pre-tokenizer settings, and `added_tokens` (special tokens). Reject unsupported model types with a clear exception.
- **Engine (C)** — format-agnostic; operates only on the internal model. Pre-tokenize → merge → emit ids; or count without emitting.
- **Process-global cache (C)** — owns parsed models for the lifetime of the process; loads are guarded by a mutex, reads are lock-free.

## 5. Public API

```php
namespace Tokenizers;

final class Bpe
{
    // Construction (each returns a Bpe bound to a cached, shared model)
    public static function fromEncoding(string $name): Bpe;            // 'cl100k_base' | 'o200k_base'
    public static function fromTiktokenFile(
        string $path, string $pattern, array $specialTokens = []
    ): Bpe;
    public static function fromHuggingFace(string $tokenizerJsonPath): Bpe;

    // Encoding
    /** @return int[] */
    public function encode(
        string $text,
        array|string $allowedSpecial = [],     // [] | list of allowed specials | 'all'
        array|string $disallowedSpecial = 'all' // 'all' | [] | list — throws if a disallowed special appears
    ): array;

    /** Fast path: number of tokens without building the id array. */
    public function countTokens(string $text): int;

    // Decoding
    public function decode(array $ids): string;        // returns raw bytes as a PHP string
    public function decodeSingle(int $id): string;     // bytes for a single token

    // Introspection
    public function vocabSize(): int;
    public function name(): ?string;                    // encoding name if constructed from one
}

final class TokenizerException extends \RuntimeException {}
```

### Procedural convenience

```php
function tokenizers_count(string $encoding, string $text): int;
function tokenizers_encode(string $encoding, string $text): array;
function tokenizers_decode(string $encoding, array $ids): string;
```

### Special-token semantics (must match tiktoken)

- `allowedSpecial`: special strings that, if present in `$text`, are encoded as their special id. `'all'` allows every special.
- `disallowedSpecial`: special strings that, if present, raise `TokenizerException`. Default `'all'` (minus whatever is in `allowedSpecial`) — this matches tiktoken's default of erroring on unexpected special tokens, which is essential for byte-exact parity and for avoiding prompt-injection surprises.

## 6. Data flow

**Encode:**
1. `Bpe::fromEncoding('cl100k_base')` → shim ensures the file is cached → C checks the process-global cache by key → on miss, the tiktoken loader parses it once into the internal model and stores it; on hit, reuse.
2. `encode($text)`:
   a. Scan for special tokens (respecting allowed/disallowed).
   b. Pre-tokenize the non-special spans with the compiled PCRE2 pattern (JIT).
   c. For each piece, run the heap-based byte-pair merge against the merge-rank map → ids.
   d. Splice special-token ids back in at their positions.
3. `countTokens` runs the same pipeline but only increments a counter (no id array allocation).

**Decode:**
1. For each id, look up its byte-sequence (or special string); concatenate raw bytes; return as a PHP string. No UTF-8 validation (tiktoken decode can yield partial multibyte sequences across token boundaries — matching reference behavior).

## 7. Memory model & ZTS safety (the moat)

- Parsed models live in a **process-global** structure (a true global, not per-thread ZTS module globals), so all threads/requests in a worker share one copy.
- **Load** acquires a mutex, double-checks the key, parses, and inserts. After insertion the model is **immutable**; **reads are lock-free**.
- The compiled PCRE2 pattern is created with a per-thread match context where required (PCRE2 match data is not shared between concurrent matches); the compiled *code* is shareable, match data is per-call/per-thread.
- Under **NTS**, this reduces to a plain process-lifetime cache with no locking needed beyond first-load.
- Cache eviction: none in v1 (vocab set is tiny and bounded). Freed at module shutdown (`MSHUTDOWN`).

This is what eliminates `yethee`'s ~26 MB-per-request rebuild: the table is paid for once per worker.

## 8. Error handling

- All load/parse failures (missing file, malformed tiktoken line, non-BPE `tokenizer.json`, unsupported HF pre-tokenizer config, bad base64) raise `Tokenizers\TokenizerException` with a specific message.
- Encountering a **disallowed** special token in `encode()` raises `TokenizerException` (mirrors tiktoken's `ValueError`).
- Invalid token ids in `decode()` raise `TokenizerException`.
- No fatal errors / no silent fallbacks.

## 9. Testing & conformance (make-or-break)

Conformance is where most BPE ports fail. The suite is the primary deliverable's safety net.

- **Reference generation:** a script (committed under `tests/reference/`) uses Python `tiktoken` to produce `(input, expected_ids)` fixtures for `cl100k_base` and `o200k_base`, and `transformers`/`tokenizers` for the HF-BPE loader fixtures (e.g. GPT-2). Fixtures are committed so CI needs no Python.
- **`.phpt` cases** diff **byte-for-byte** against fixtures across:
  - ASCII, multibyte UTF-8, emoji and surrogate-pair boundaries.
  - Long repeated-character inputs (worst-case / O(n log n) guard, with a time bound).
  - Special tokens: allowed, disallowed-raises, and `'all'`.
  - `decode(encode(x)) === x` round-trips, including bytes that aren't valid standalone UTF-8.
  - Empty string, whitespace-only, very long inputs.
  - HF loader: a known GPT-2 `tokenizer.json` matches the `transformers` reference.
- **Memory test:** repeated `encode` calls within one process do not grow resident vocab memory (verifies single-load caching).

## 10. Build & distribution

- Standard PHP extension layout: `config.m4`, `config.w32`, `php_tokenizers.h`, `tokenizers.c` (+ split source files for loaders/engine), `tokenizers.stub.php` for arginfo.
- **PHP 8.3+**, **NTS and ZTS** both supported.
- CI matrix: Linux + macOS × NTS + ZTS × PHP 8.3 + 8.4.
- Reuses PHP's bundled **PCRE2** (no new dependency).
- Publish to **PECL** and support **PIE** install. Prebuilt binaries are a follow-up (important for adoption but not blocking v1 correctness).

## 11. Risks & mitigations

| Risk | Mitigation |
|------|-----------|
| Byte-exact conformance (special tokens, decode round-trips, UTF-8/surrogate edges, exact regex semantics under PCRE2) | Large committed fixture suite generated from the Python reference; treat any diff as a release blocker. |
| tiktoken pre-tokenizer regex not expressible/identical in PCRE2 | Validate both `cl100k`/`o200k` patterns compile under PCRE2-JIT early; fixtures catch semantic drift. (Research indicates PCRE2 covers the required `\p{L}`/`\p{N}`/lookahead constructs.) |
| ZTS correctness of shared read-only cache | Immutable-after-load model, mutex only on first load, per-thread PCRE2 match data; explicit ZTS test in CI. |
| HF `tokenizer.json` variety beyond ByteLevel-BPE | v1 supports the common ByteLevel-BPE config only and **fails loudly** on anything else; broaden in Phase 2. |
| Vocab redistribution licensing | Never bundle vocab; the PHP shim downloads + caches from the official source on first use. |
| Adoption vs pure-PHP "good enough" for small prompts | Position on memory / worst-case / installability (not small-prompt speed); invest in frictionless install. |

## 12. Roadmap (context only — out of scope here)

- **Phase 2:** add WordPiece and Unigram algorithms in the same `.so` (BERT, T5, multilingual); SentencePiece `.model` loader; more HF `tokenizer.json` configs.
- **Phase 3:** thin PHP companion exposing a unified `TokenCounter` that routes local models to this extension and Claude 3+/Gemini to their provider `count_tokens` APIs.

Each phase gets its own spec → plan → implementation cycle.

## 13. Resolved decisions (confirmed in review)

- **Extension name:** `tokenizers`, namespace `\Tokenizers\`. Confirmed.
- **Shim cache directory:** configurable, defaulting to `$XDG_CACHE_HOME/tokenizers/` (falling back to the user cache dir / system temp when unset). Confirmed.
- **`p50k_base`/`r50k_base`:** deferred out of v1 (trivial to add later; keeps the conformance surface small now). Confirmed.
