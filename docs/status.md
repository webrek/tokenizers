# Status & Limitations

This page gives an honest account of what is verified, what is limited, and what is planned. Nothing is hidden.

---

## Current version

**v0.1.0** — early release.

Developed and tested on macOS + PHP 8.3 NTS. Linux and ZTS are supported by design: the process-global vocab cache uses a TSRM mutex under ZTS, so parallel threads do not race on cache population. The CI matrix does not include macOS × ZTS (setup-php cannot provide that combination); Linux covers the ZTS case.

---

## What's verified

- `make install` produces a loadable extension: `php -m` lists `tokenizers`, `\Tokenizers\VERSION` returns `"0.1.0"`.
- The full test suite passes against the installed copy.
- Byte-exact conformance for all three tokenizer algorithms (see table below).

---

## Conformance

| Algorithm | Reference | Test cases | Result |
|---|---|---|---|
| BPE (`cl100k_base` + `o200k_base`) | Python `tiktoken` | 18 | Byte-exact. CI fails on any diff. |
| WordPiece (`bert-base-uncased`) | HuggingFace `BertTokenizerFast` | 44 | Byte-exact. CI fails on any diff. |
| Unigram (`t5-small`) | HuggingFace `transformers` | 24 | Byte-exact. CI fails on any diff. |

Conformance fixtures are committed to the repository. Any regression breaks CI immediately.

---

## Limitations

None of these are hidden. They are the honest boundaries of v0.1.0.

**PIE end-to-end install not yet verified.**
The `composer.json` `php-ext` manifest is ready and the `pie install webrek/tokenizers` command is correct, but it has not been run on a clean machine (`pie` was not available in the development environment). Use [getting-started.md](getting-started.md) "Install from source" as the known-good path.

**WordPiece normalization is Latin-1 + CJK-spacing only.**
The implementation handles Latin-1 character folding and CJK codepoint spacing (pad with spaces) but does not perform full Unicode NFD decomposition. Non-Latin scripts that rely on NFD normalization are out of scope for v1. Tokens for such inputs may differ from the HuggingFace reference on characters outside the covered range.

**Unigram normalization is Metaspace + identity-on-ASCII.**
The implementation applies Metaspace (`▁`, U+2581) and passes ASCII through unchanged. It does not perform NFKC normalization. Some whitespace edge cases (leading spaces, trailing spaces, multiple consecutive spaces) can also produce results that differ from the SentencePiece reference.

**Claude 3+ and Gemini have no local tokenizer.**
Counting tokens for Claude or Gemini models requires a live network call and a valid API key. Results are exact only as of the provider's current tokenizer; providers may change their tokenizer without notice.

**`Bpe::name()` returns `null` in v0.1.**
Name tracking for `Bpe` instances is not implemented. The method exists and returns `null`.

**OOM policy: crash-on-OOM is accepted for v1.**
If the system runs out of memory during a large vocab load or encode operation, PHP will crash rather than throw an exception. This is standard PECL behavior. Known realloc-leak sites were hardened, but full OOM-safe error paths are deferred to a later release.

**Legacy OpenAI encodings `p50k_base` and `r50k_base` are not bundled.**
Only `cl100k_base` and `o200k_base` are built-in. `p50k_base` and `r50k_base` can be added in a future release; the architecture supports them.

---

## Roadmap

| Phase | Status | Description |
|---|---|---|
| Phase 1 | Done | Byte-level BPE: `cl100k_base`, `o200k_base`, HuggingFace BPE via `tokenizer.json`. O(n log n) merge. Tiktoken-conformant. |
| Phase 2 | Done | WordPiece (BERT family) + Unigram (T5/SentencePiece). Byte-exact. `Encoding::fromHuggingFace()` dispatches all three algorithms. |
| Phase 3 | Done | Claude/Gemini API companion (pure PHP, standalone, no HTTP library dependencies). |
| Future | Not built | PECL/PIE publish + prebuilt binaries; `p50k_base`/`r50k_base` legacy encodings; full NFD/NFKC normalization; OOM-hardening backlog. |

Note: older versions of the README described Phase 2 and Phase 3 as future work. That was incorrect. All three phases are complete as of v0.1.0.

---

## Related pages

- [getting-started.md](getting-started.md) — install, verify, first tokenization
- [api-reference.md](api-reference.md) — complete API reference
- [guides/loading-models.md](guides/loading-models.md) — loading OpenAI and HuggingFace models
- [guides/estimating-costs.md](guides/estimating-costs.md) — estimating LLM API costs
- [guides/remote-providers.md](guides/remote-providers.md) — Claude/Gemini companion setup and limitations
