# Remote Providers: Claude and Gemini Token Counting

This guide covers the pure-PHP companion that counts tokens for Claude and Gemini models by calling their official APIs. If you are looking for local, offline tokenization, see [loading-models.md](loading-models.md).

---

## 1. Why this is different

OpenAI's tokenizers (BPE, WordPiece, Unigram) are published openly and can be replicated exactly in native code — which is what the C extension does. Claude 3+ and Gemini do **not** publish their tokenizers. There is no vocabulary file to download, no algorithm to re-implement locally.

Counting tokens for these models requires a **live API call** using an **API key**. The count is exact only as of the provider's current tokenizer; providers can update their tokenizers without notice.

Anthropic explicitly advises against using tiktoken or other BPE approximations for Claude, because Claude's tokenizer differs from OpenAI's. The remote companion documented here is the supported, official path: it calls Anthropic's `/v1/messages/count_tokens` endpoint, which returns the real token count.

---

## 2. Installation

The remote companion is a set of pure-PHP files located under `php/Tokenizers/Remote/`. It works **without the C extension installed** because it bootstraps a `\Tokenizers\TokenizerException` polyfill automatically.

**Requirements:**
- PHP 8.3 or 8.4
- `ext-curl` (for HTTP calls)
- `ext-json` (for JSON encoding/decoding)

No `anthropic-ai/sdk`, no Guzzle, no other HTTP library — raw curl only, zero extra dependencies.

**With Composer autoload** (recommended if you use Composer):

```php
<?php
require __DIR__ . '/vendor/autoload.php';

use Tokenizers\Remote\Anthropic;
use Tokenizers\Remote\Gemini;
use Tokenizers\TokenCounter;
```

**Without Composer** (manual requires):

```php
<?php
require_once __DIR__ . '/php/Tokenizers/TokenizerException.php'; // polyfill / base exception
require_once __DIR__ . '/php/Tokenizers/Remote/Http.php';        // Transport interface
require_once __DIR__ . '/php/Tokenizers/Remote/CurlTransport.php';
require_once __DIR__ . '/php/Tokenizers/Remote/Anthropic.php';
require_once __DIR__ . '/php/Tokenizers/Remote/Gemini.php';
require_once __DIR__ . '/php/Tokenizers/TokenCounter.php';

use Tokenizers\Remote\Anthropic;
use Tokenizers\Remote\Gemini;
use Tokenizers\TokenCounter;
```

---

## 3. API keys

| Provider | Environment variable(s) | Constructor override |
|----------|------------------------|----------------------|
| Anthropic | `ANTHROPIC_API_KEY` | `new Anthropic(apiKey: '...')` |
| Gemini | `GEMINI_API_KEY`, then `GOOGLE_API_KEY` | `new Gemini(apiKey: '...')` |

Keys are resolved at **call time**, not at construction time. Creating an `Anthropic` or `Gemini` object without a key does not throw — the exception is raised only when you actually call `countTokens()` and no key is found. This lets you instantiate the objects unconditionally and defer key validation to the call site.

The constructor `apiKey:` argument always takes precedence over environment variables.

```php
<?php
use Tokenizers\Remote\Anthropic;
use Tokenizers\Remote\Gemini;

// Key from environment
$anthropic = new Anthropic();              // reads ANTHROPIC_API_KEY at call time
$gemini    = new Gemini();                 // reads GEMINI_API_KEY or GOOGLE_API_KEY at call time

// Key passed explicitly (overrides env)
$anthropic = new Anthropic(apiKey: 'sk-ant-...');
$gemini    = new Gemini(apiKey: 'AIza...');
```

---

## 4. Counting Claude tokens

```php
<?php
use Tokenizers\Remote\Anthropic;

$client = new Anthropic(); // key from ANTHROPIC_API_KEY

// Simple text string — becomes a single user turn
$n = $client->countTokens('claude-opus-4-8', 'Hello, world!');
echo $n; // exact token count from the API

// Messages array + optional system prompt
$n = $client->countTokens(
    model: 'claude-opus-4-8',
    messages: [
        ['role' => 'user',      'content' => 'What is the capital of France?'],
        ['role' => 'assistant', 'content' => 'The capital of France is Paris.'],
        ['role' => 'user',      'content' => 'And Germany?'],
    ],
    system: 'You are a helpful geography assistant.',
);
echo $n;
```

**Under the hood:**

- Endpoint: `POST https://api.anthropic.com/v1/messages/count_tokens`
- Headers: `x-api-key: <key>`, `anthropic-version: 2023-06-01`, `content-type: application/json`
- Body: `{"model": "...", "messages": [...], "system": "..."}` — a plain string `$messages` argument is wrapped as `[{"role":"user","content":"<text>"}]` automatically
- Parses `input_tokens` from the JSON response
- A non-2xx HTTP status or malformed response body throws `\Tokenizers\TokenizerException`

---

## 5. Counting Gemini tokens

```php
<?php
use Tokenizers\Remote\Gemini;

$client = new Gemini(); // key from GEMINI_API_KEY or GOOGLE_API_KEY

// Short model name
$n = $client->countTokens('gemini-1.5-flash', 'Hello, world!');
echo $n;

// Full model name with leading "models/" prefix — both forms are accepted
$n = $client->countTokens('models/gemini-1.5-flash', 'Hello, world!');
echo $n; // same result; the leading "models/" is normalized, never double-prefixed
```

**Under the hood:**

- Endpoint: `POST https://generativelanguage.googleapis.com/v1beta/models/{model}:countTokens`
  (the `{model}` segment is always the bare name without `models/`, e.g. `gemini-1.5-flash`)
- Header: `x-goog-api-key: <key>`
- Body: `{"contents":[{"parts":[{"text":"<text>"}]}]}`
- Parses `totalTokens` from the JSON response
- A non-2xx HTTP status or malformed response body throws `\Tokenizers\TokenizerException`

---

## 6. Unified routing with `TokenCounter`

`TokenCounter` provides a single `count()` method that automatically routes to Anthropic, Gemini, or the local BPE encoder depending on the model name — no `if/else` logic in your application code.

```php
<?php
use Tokenizers\TokenCounter;

$tc = new TokenCounter(); // uses default Anthropic and Gemini clients (keys from env)

// Routes to local BPE (no network, no key needed)
$n = $tc->count('cl100k_base', 'Hello, world!');

// Routes to Anthropic (needs ANTHROPIC_API_KEY)
$n = $tc->count('claude-opus-4-8', 'Hello, world!');

// Routes to Gemini (needs GEMINI_API_KEY or GOOGLE_API_KEY)
$n = $tc->count('gemini-1.5-flash', 'Hello, world!');
```

**Routing rules** (applied by `TokenCounter::route($model)`, no network call):

| Model prefix | Provider returned |
|--------------|------------------|
| `claude*` or `anthropic*` | `'anthropic'` |
| `gemini*` or `models/gemini*` | `'gemini'` |
| Anything else | `'local'` |

```php
// Inspect routing without making any call
echo TokenCounter::route('claude-opus-4-8');   // 'anthropic'
echo TokenCounter::route('gemini-1.5-flash');  // 'gemini'
echo TokenCounter::route('cl100k_base');        // 'local'
```

**Forcing a provider** with the optional third argument:

```php
// Override automatic routing
$n = $tc->count('my-fine-tuned-model', $text, provider: 'anthropic');
```

Passing an unknown `$provider` value throws `\Tokenizers\TokenizerException("unknown provider '<p>' for model: <model>")`.

**Injecting custom backend instances** (e.g. to pass an API key or custom timeout):

```php
use Tokenizers\Remote\Anthropic;
use Tokenizers\Remote\Gemini;
use Tokenizers\TokenCounter;

$tc = new TokenCounter(
    anthropic: new Anthropic(apiKey: 'sk-ant-...', timeout: 10),
    gemini:    new Gemini(apiKey: 'AIza...'),
);
```

---

## 7. Testing offline

The `Transport` interface allows you to inject a fake HTTP transport for unit testing. This means you can test all routing, parsing, and error-handling logic without making real network calls.

```php
<?php
use Tokenizers\Remote\Anthropic;
use Tokenizers\Remote\Transport;

// Implement the interface with a simple fake
$fake = new class implements Transport {
    public function post(string $url, array $headers, string $body, int $timeout): array
    {
        // Return a fake API response
        return [
            'status' => 200,
            'body'   => json_encode(['input_tokens' => 42]),
        ];
    }
};

// Inject into the client
$client = new Anthropic(apiKey: 'test-key', transport: $fake);

$n = $client->countTokens('claude-opus-4-8', 'Hello');
assert($n === 42); // passes — no network call was made
```

The same `Transport` interface applies to `Gemini`. The default production transport is `CurlTransport`, which you never need to reference directly unless you are replacing it.

---

## 8. Honest boundaries

Before choosing this companion, be aware of the following constraints:

- **Network and key required.** There is no offline path for Claude or Gemini. Every call makes an HTTPS request to the provider's API.
- **Exact only as of now.** The count reflects the provider's tokenizer at the moment of the call. Providers can and do update their tokenizers without versioned endpoints.
- **Minimal dependencies.** The only PHP extension requirements are `ext-curl` and `ext-json`, both of which are typically bundled with PHP. No Composer packages, no SDK, no Guzzle.
- **If you already use `anthropic-ai/sdk`.** That SDK has its own token-counting method. You can use it directly rather than this companion — they both call the same Anthropic endpoint. This companion exists to provide a **zero-dependency, unified-routing** path for projects that do not want to pull in the full SDK.
- **Gemini model name normalization.** The companion handles both `gemini-1.5-flash` and `models/gemini-1.5-flash` transparently. Always pass whatever form the model name is in; the companion normalizes it before constructing the URL.

---

## See also

- [loading-models.md](loading-models.md) — local BPE, WordPiece, and Unigram tokenizers (no API key needed)
- [estimating-costs.md](estimating-costs.md) — convert token counts to cost estimates
- [../api-reference.md](../api-reference.md) — complete API reference for `Anthropic`, `Gemini`, `TokenCounter`, and `Transport`
- [../getting-started.md](../getting-started.md) — installation and initial setup
- [../status.md](../status.md) — version status, conformance, and known limitations
