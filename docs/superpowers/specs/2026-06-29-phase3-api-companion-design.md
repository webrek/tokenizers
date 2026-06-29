# Design Spec — `tokenizers` Phase 3: Remote-API companion (Claude / Gemini)

- **Date:** 2026-06-29
- **Status:** Approved (autonomous design under standing goal "dale con los dos")
- **Builds on:** Phases 1–2 (native BPE / WordPiece / Unigram, merged to `main`).

## 1. Problem & motivation

Claude 3+ and Gemini publish **no local tokenizer** — accurate token counts come only from each provider's API. The native extension cannot count them locally. Phase 3 adds a small **PHP-only companion** that calls the providers' token-counting endpoints and a unified facade that routes a model to the right backend: local (the C extension) for OpenAI/open-weight models, remote for Claude/Gemini. (The official Anthropic guidance is explicit: *never use tiktoken for Claude* — use `count_tokens`. This companion is the correct path.)

## 2. Goals & non-goals

### Goals (Phase 3)
- `\Tokenizers\Remote\Anthropic::countTokens(string $model, string|array $messages, ?string $system=null): int` → `POST https://api.anthropic.com/v1/messages/count_tokens`, returns `input_tokens`.
- `\Tokenizers\Remote\Gemini::countTokens(string $model, string $text): int` → `POST .../v1beta/models/{model}:countTokens`, returns `totalTokens`.
- A **unified facade** `\Tokenizers\TokenCounter::count(string $model, string $text): int` that routes by model: known local encodings/HF models → the extension; `claude*`/`anthropic*` → Anthropic; `gemini*`/`models/gemini*` → Gemini. Explicit provider override supported.
- **Testable without network:** every client takes an injectable HTTP transport (a callable). Tests pass a fake transport returning canned provider responses and assert the request (URL, headers, body) and the parsed count. Live calls are documented but not run in CI.
- Errors → `\Tokenizers\TokenizerException` (or a `RemoteException` subclass) with the HTTP status + provider message.
- PHP-only; no C changes. No new hard dependencies beyond `ext-curl` (ubiquitous) + the already-present `ext-json`.

### Non-goals (deferred)
- Streaming, batching, tool/system token accounting beyond a simple `system` string for Anthropic.
- Caching of counts, retries/backoff beyond a single configurable timeout (a thin retry can be a v3.1 add).
- Wrapping the providers' full chat APIs — this is **token counting only**.
- Reproducing provider tokenization locally (impossible for Claude 3+/Gemini — that's the whole point).

## 3. Architecture

```
php/Tokenizers/Remote/
  Http.php          — interface Transport { post(string $url, array $headers, string $body, int $timeout): array{status:int, body:string} }
  CurlTransport.php — default Transport using ext-curl (timeout, no shared state)
  Anthropic.php     — countTokens(); builds /v1/messages/count_tokens request, parses input_tokens
  Gemini.php        — countTokens(); builds models/{model}:countTokens request, parses totalTokens
php/Tokenizers/
  TokenCounter.php  — unified facade: routes model -> {extension | Anthropic | Gemini}
```

- **Transport seam (the testability core):** clients depend on the `Transport` interface, not curl directly. `CurlTransport` is the production impl; tests inject a closure-backed fake. This is what lets request-construction and response-parsing be verified offline.
- **Auth:** API keys from the constructor, else env — Anthropic `ANTHROPIC_API_KEY`; Gemini `GEMINI_API_KEY` then `GOOGLE_API_KEY`. Missing key → `TokenizerException` at call time.
- **TokenCounter routing:** a model→provider resolver. Local: names matching the Phase-1 registry (`cl100k_base`, `o200k_base`) or a caller-supplied local `Bpe`/`WordPiece`/`Unigram` instance. Remote: prefix match (`claude`/`anthropic` → Anthropic, `gemini` → Gemini). An explicit `provider:` argument overrides the heuristic.

### Wire formats (authoritative; Anthropic confirmed via the claude-api skill, Gemini to be confirmed by WebFetch during the build)
- **Anthropic** — `POST https://api.anthropic.com/v1/messages/count_tokens`; headers `x-api-key: <key>`, `anthropic-version: 2023-06-01`, `content-type: application/json`; body `{"model": "<id>", "messages": [{"role":"user","content":"<text>"}] , "system"?: "<text>"}`; response `{"input_tokens": N}`. Default model `claude-opus-4-8` when the caller doesn't pass one.
- **Gemini** — `POST https://generativelanguage.googleapis.com/v1beta/models/{model}:countTokens?key=<key>`; header `content-type: application/json`; body `{"contents":[{"parts":[{"text":"<text>"}]}]}`; response `{"totalTokens": N}`. (The build task MUST WebFetch the current Google `countTokens` doc and pin the exact field names before shipping.)

## 4. Design decisions (documented)
- **D1 — Raw HTTP via `ext-curl`, not the official `anthropic-ai/sdk`.** The official SDK exists and is normally preferred, but pulling it (and its transitive deps) into a zero-dependency tokenizer extension's companion contradicts the project's dependency-light ethos and bloats install. A single `count_tokens` POST is a few lines of curl. The companion stays dependency-free; users already on `anthropic-ai/sdk` can call its `messages->countTokens(...)` directly instead. This is an explicit, justified deviation — **not** an OpenAI-shim or guesswork (the request shape comes from the official claude-api reference).
- **D2 — Transport injection for testability.** No network in CI; correctness is verified by asserting the exact request bytes against the documented wire format + parsing canned responses.
- **D3 — Companion is PHP, separate from the C extension.** It ships in `php/Tokenizers/` and is consumed via `require`/Composer autoload; the `.so` is unchanged. The unified `TokenCounter` is the only place local and remote meet.
- **D4 — Honest capability boundary.** The README/companion docs state plainly that Claude/Gemini counts require a network call + API key and are exact only as of the provider's current tokenizer; there is no offline path.

## 5. Public API

```php
namespace Tokenizers\Remote;

interface Transport {
    /** @return array{status:int, body:string} */
    public function post(string $url, array $headers, string $body, int $timeout): array;
}

final class Anthropic {
    public function __construct(?string $apiKey = null, ?Transport $transport = null,
                                string $version = '2026-06-01', int $timeout = 30);
    /** $messages: a plain string (treated as one user turn) or a full messages array. */
    public function countTokens(string $model, string|array $messages, ?string $system = null): int;
}

final class Gemini {
    public function __construct(?string $apiKey = null, ?Transport $transport = null, int $timeout = 30);
    public function countTokens(string $model, string $text): int;
}
```

```php
namespace Tokenizers;

final class TokenCounter {
    public function __construct(?Remote\Anthropic $anthropic = null, ?Remote\Gemini $gemini = null);
    /** Routes by model name (or explicit $provider: 'local'|'anthropic'|'gemini'). */
    public function count(string $model, string $text, ?string $provider = null): int;
}
```

> `anthropic-version`: the spec defaults the header to `2023-06-01` per the official reference; the constructor exposes it so callers can pin a newer version. (The `$version` default above will be set to the documented stable value `2023-06-01` in the plan — shown here as configurable.)

## 6. Testing
- `Transport` fake (closure) → assert request URL/headers/body and parse canned `{"input_tokens":N}` / `{"totalTokens":N}` for each client (`.phpt`, offline).
- Error paths: non-2xx → `TokenizerException` with status; missing API key → throws; malformed JSON → throws.
- `TokenCounter` routing: model-name → correct backend (with fakes for remote; the real extension for local).
- A clearly-marked, SKIP-by-default live smoke test (runs only when `ANTHROPIC_API_KEY` / `GEMINI_API_KEY` are set) to sanity-check against the real endpoints.

## 7. Risks
- **Provider API drift** (field names, versions): mitigated by the injectable transport (easy to adjust), the documented wire formats, and the build-time WebFetch of the current Gemini doc. Anthropic shape is pinned from the official reference.
- **No offline correctness check**: by nature, Claude/Gemini counts can't be validated without the live API — the live smoke test (opt-in) is the only ground truth; CI verifies request/response *handling*, not provider parity.

## 8. Plan ordering
1. `Transport` + `CurlTransport`.
2. `Anthropic::countTokens` (+ offline tests).
3. `Gemini::countTokens` (WebFetch the current doc first; + offline tests).
4. `TokenCounter` facade (routing + tests).
5. Docs (README "Remote providers" section, honest boundary) + opt-in live smoke test.
