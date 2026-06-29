# tokenizers

Native PHP extension (C) for byte-level BPE tokenization. Tiktoken-compatible
(`cl100k_base`, `o200k_base`) and supports HuggingFace `tokenizer.json` BPE
models. Apache-2.0.

## Why a C extension?

| Property | Pure-PHP tiktoken port | This extension |
|---|---|---|
| Memory per worker | ~26 MB vocab rebuild on each request | Loaded **once** per worker process |
| Worst-case complexity | O(n²) per pre-token piece | **O(n log n)** (heap-based merge) |
| Installation | Pure PHP, no native deps | Single `.so`; no Rust toolchain, no `ffi.enable` |
| Small-prompt throughput | **Faster** (no FFI overhead) | Slower — use pure-PHP for <100 tokens |

**The wins are memory, worst-case latency, and installability — not throughput on
small inputs.** If your prompt fits in a tweet, a pure-PHP implementation will be
faster. This extension exists for workloads where (a) you care about the 26 MB
per-process overhead or (b) adversarial inputs could trigger quadratic behavior.

## Supported models

### Locally and exactly

| Family | Models | Encoding |
|---|---|---|
| OpenAI | GPT-4, GPT-4o (text), o1, o3 | `cl100k_base` |
| OpenAI | GPT-4o (multimodal), o1 mini/pro | `o200k_base` |
| Open-weight | GPT-2, RoBERTa | HuggingFace BPE `tokenizer.json` |
| Open-weight | Llama 3, Mistral (tekken), Qwen, DeepSeek | HuggingFace BPE `tokenizer.json` |

Conformance is byte-exact with the Python `tiktoken` reference. Any diff against
the committed fixture set fails CI.

### Not supported locally (no public tokenizer)

**Claude 3 and later** and **Gemini** do not publish their tokenizers. For token
counts, call the provider's `count_tokens` API directly. The Phase 3 companion
PHP classes (`Tokenizers\Remote\Anthropic`, `Tokenizers\Remote\Gemini`,
`Tokenizers\TokenCounter`) wrap those APIs and do not require this extension.
See the "Remote providers (Claude / Gemini)" section below.

## Requirements

- PHP 8.3 or 8.4 (NTS or ZTS)
- **`libpcre2-8`** at runtime
- **`libpcre2-dev`** (or `pcre2-config` on PATH) to build

On macOS with Homebrew: `brew install pcre2`. On Debian/Ubuntu:
`apt-get install libpcre2-dev`.

## Installation

### Via PECL

```bash
pecl install tokenizers
```

### Via PIE

```bash
pie install tokenizers
```

### From source

```bash
phpize
./configure
make
make install
```

Then enable the extension in your `php.ini`:

```ini
extension=tokenizers
```

## Usage

### OO API — known OpenAI encodings

```php
use Tokenizers\Encoding;

// Downloads and checksum-verifies the vocab on first use; cached for subsequent calls.
$enc = Encoding::load('cl100k_base');   // or 'o200k_base'

$n   = $enc->countTokens($prompt);     // cheapest: count without building the id array
$ids = $enc->encode($prompt);          // => int[]
$str = $enc->decode($ids);             // => string (same bytes as $prompt for plain text)

echo $enc->vocabSize();   // 100277 for cl100k_base
echo $enc->name();        // null (name tracking not implemented in v0.1)
```

### OO API — HuggingFace BPE models

```php
use Tokenizers\Encoding;

$enc  = Encoding::fromHuggingFace('/path/to/tokenizer.json');
$ids  = $enc->encode('Hello, world!');
$text = $enc->decode($ids);
```

### Special tokens

```php
use Tokenizers\{Encoding, Bpe};

$enc = Encoding::load('cl100k_base');

// By default all special tokens are disallowed in the input string
// (raises TokenizerException if they appear). To allow specific ones:
$ids = $enc->encode('<|endoftext|> hello', allowedSpecial: ['<|endoftext|>']);

// To allow every special token:
$ids = $enc->encode('<|endoftext|> hello', allowedSpecial: 'all');
```

### Procedural API

```php
use Tokenizers\{Bpe, Encoding};

$bpe  = Encoding::load('cl100k_base');    // returns a Bpe instance

$ids  = tokenizers_encode($bpe, $text);
$n    = tokenizers_count($bpe, $text);
$str  = tokenizers_decode($bpe, $ids);

echo tokenizers_version();       // "0.1.0"
echo tokenizers_cache_count();   // number of models loaded in process cache
```

### Low-level constructors (advanced)

```php
use Tokenizers\Bpe;

// Load directly from a .tiktoken file you already have on disk
$bpe = Bpe::fromTiktokenFile('/path/to/cl100k_base.tiktoken', $pattern, $specialTokens);

// Build from a vocab array and merge list (used internally by Encoding::fromHuggingFace)
$bpe = Bpe::fromVocab($tokenBytesToId, $merges, $pattern, $specialTokens);
```

### Error handling

```php
use Tokenizers\{Encoding, TokenizerException};

try {
    $enc = Encoding::load('gpt2');   // not a built-in encoding
} catch (TokenizerException $e) {
    echo $e->getMessage();           // "unknown encoding: gpt2"
}
```

`\Tokenizers\TokenizerException` extends `\RuntimeException`.

## Remote providers (Claude / Gemini)

Claude 3 and later, and Gemini, do not publish their tokenizers. There is no
local vocabulary file to load — token counts for those models require a live
call to the provider's API and an API key. Anthropic explicitly advises against
using tiktoken or any other local approximation for Claude; this companion is
the supported path.

The three classes live in `php/Tokenizers/Remote/` and are plain PHP — they do
not require building the C extension, but they do require `ext-curl` and
`ext-json`. They intentionally do **not** pull in `anthropic-ai/sdk` or any
HTTP library (Guzzle, etc.); raw curl only.

### Installation (remote classes only)

The remote classes are autoloaded if you use Composer, or you can require them
manually:

```php
require '/path/to/php/Tokenizers/Remote/Http.php';          // Transport interface
require '/path/to/php/Tokenizers/Remote/CurlTransport.php';
require '/path/to/php/Tokenizers/Remote/Anthropic.php';
require '/path/to/php/Tokenizers/Remote/Gemini.php';
// For the unified facade:
require '/path/to/php/Tokenizers/TokenCounter.php';
```

### Environment variables

| Provider | Env var(s) | Constructor override |
|---|---|---|
| Anthropic | `ANTHROPIC_API_KEY` | `new Anthropic(apiKey: '...')` |
| Gemini | `GEMINI_API_KEY` then `GOOGLE_API_KEY` | `new Gemini(apiKey: '...')` |

The constructor argument takes priority over the environment; the key is read
from the environment at construction time if the argument is omitted or `null`.
A missing key throws `\Tokenizers\TokenizerException` at call time, not at
construction time.

### Usage

**Anthropic** — count tokens for one user turn:

```php
use Tokenizers\Remote\Anthropic;

$n = (new Anthropic(apiKey: 'sk-ant-...'))->countTokens('claude-opus-4-8', 'Hello, world!');
// or read key from ANTHROPIC_API_KEY:
$n = (new Anthropic())->countTokens('claude-opus-4-8', 'Hello, world!');
echo $n; // => positive int (exact as of the model's current tokenizer)
```

Pass a full messages array and an optional system prompt:

```php
$n = (new Anthropic())->countTokens(
    'claude-opus-4-8',
    [['role' => 'user', 'content' => 'Hello'], ['role' => 'assistant', 'content' => 'Hi!']],
    system: 'You are a helpful assistant.',
);
```

**Gemini** — count tokens for a text prompt:

```php
use Tokenizers\Remote\Gemini;

$n = (new Gemini())->countTokens('gemini-1.5-flash', 'Hello, world!');
// GEMINI_API_KEY (or GOOGLE_API_KEY) must be set, or pass apiKey: '...'
echo $n; // => positive int
```

**TokenCounter** — unified facade that routes to the right backend by model name:

```php
use Tokenizers\TokenCounter;

$tc = new TokenCounter();                            // lazy-initialises backends from env vars
echo $tc->count('claude-opus-4-8', $text);          // => remote Anthropic call
echo $tc->count('gemini-1.5-flash', $text);         // => remote Gemini call
echo $tc->count('cl100k_base', $text);              // => local BPE (no network, no key needed)
```

`TokenCounter::route(string $model): string` returns `'anthropic'`, `'gemini'`,
or `'local'` without making any network call — useful for branching logic.

### API reference: remote classes

#### `\Tokenizers\Remote\Anthropic`

```
__construct(
    ?string   $apiKey    = null,    // falls back to ANTHROPIC_API_KEY env var
    ?Transport $transport = null,   // defaults to CurlTransport; injectable for testing
    string    $version   = '2023-06-01',
    int       $timeout   = 30,
): void

countTokens(string $model, string|array $messages, ?string $system = null): int
```

`$messages` can be a plain string (treated as one user turn) or a full
`[['role' => ..., 'content' => ...], ...]` array. Throws
`\Tokenizers\TokenizerException` on missing key, non-2xx response, or malformed
response body.

#### `\Tokenizers\Remote\Gemini`

```
__construct(
    ?string    $apiKey    = null,   // falls back to GEMINI_API_KEY, then GOOGLE_API_KEY
    ?Transport $transport = null,
    int        $timeout   = 30,
): void

countTokens(string $model, string $text): int
```

The `$model` argument accepts both `'gemini-1.5-flash'` and
`'models/gemini-1.5-flash'` — the leading `models/` prefix is normalised
automatically.

#### `\Tokenizers\TokenCounter`

```
__construct(?Anthropic $anthropic = null, ?Gemini $gemini = null): void

static route(string $model): string   // 'anthropic' | 'gemini' | 'local'

count(string $model, string $text, ?string $provider = null): int
```

Pass `$provider` explicitly to override the auto-routing (e.g. `'anthropic'`,
`'gemini'`, or `'local'`). Throws `\Tokenizers\TokenizerException` for unknown
providers.

### Honest boundaries

- **Network and key required.** There is no offline path for Claude or Gemini
  token counts.
- **Exact only as of the provider's current tokenizer.** Anthropic and Google
  can update their internal tokenizers; this library always reflects whatever
  the live API returns.
- **Dependency.** `ext-curl` (built-in on most PHP installations) and
  `ext-json`. No Composer packages, no vendored HTTP library.

### If you already use `anthropic-ai/sdk`

Callers who already pull in `anthropic-ai/sdk` can call
`$client->messages->countTokens(...)` directly — there is no need to also use
this companion. The companion exists for projects that want a zero-dependency
path (raw curl only) or that need unified routing across Anthropic, Gemini, and
local BPE models via `TokenCounter`.

## Vocabulary caching

Built-in encodings are downloaded from OpenAI's public CDN on first use and
checksum-verified. They are **never redistributed** with the extension. The cache
location is selected in this order:

1. `$TOKENIZERS_CACHE_DIR/tokenizers`
2. `$XDG_CACHE_HOME/tokenizers`
3. `$HOME/.cache/tokenizers`
4. `sys_get_temp_dir()/tokenizers`

Once a process loads a model, it stays in a process-global cache and is reused
for every subsequent request in that worker — this is the primary memory win.

## API reference

### `\Tokenizers\Encoding` (PHP shim)

| Method | Description |
|---|---|
| `Encoding::load(string $name): Bpe` | Download/cache a known encoding (`cl100k_base`, `o200k_base`) and return a `Bpe` instance |
| `Encoding::fromHuggingFace(string $jsonPath): Bpe` | Parse a HuggingFace `tokenizer.json` BPE file and return a `Bpe` instance |
| `Encoding::cacheDir(): string` | Return the resolved cache directory path |

### `\Tokenizers\Bpe` (C class)

| Method | Description |
|---|---|
| `Bpe::fromTiktokenFile(string $path, string $pattern, array $specialTokens = []): Bpe` | Load from a `.tiktoken` vocab file |
| `Bpe::fromVocab(array $tokenBytesToId, array $merges, string $pattern, array $specialTokens = []): Bpe` | Build from raw vocab + merge list |
| `encode(string $text, array\|string $allowedSpecial = [], array\|string $disallowedSpecial = "all"): array` | Encode text to token IDs |
| `countTokens(string $text): int` | Count tokens without allocating the ID array |
| `decode(array $ids): string` | Decode token IDs to text |
| `decodeSingle(int $id): string` | Decode a single token ID |
| `vocabSize(): int` | Return the vocabulary size |
| `name(): ?string` | Return the encoding name, or `null` if not set |

### Procedural functions

| Function | Description |
|---|---|
| `tokenizers_encode(Bpe $t, string $text, array $allowedSpecial = [], array\|string $disallowedSpecial = "all"): array` | Encode text to token IDs |
| `tokenizers_decode(Bpe $t, array $ids): string` | Decode token IDs to text |
| `tokenizers_count(Bpe $t, string $text): int` | Count tokens |
| `tokenizers_cache_count(): int` | Number of models currently held in the process-global cache |
| `tokenizers_version(): string` | Extension version string |

The constant `\Tokenizers\VERSION` (string) holds the same version value.

## Roadmap

- **Phase 1 (current — v0.1.0):** Byte-level BPE, `cl100k_base`, `o200k_base`,
  HuggingFace BPE `tokenizer.json`. O(n log n) merge. Tiktoken-conformant.
- **Phase 2:** WordPiece and Unigram algorithms → BERT, T5, and related models.
- **Phase 3 (done):** Claude / Gemini API companion — a pure-PHP library that
  calls the provider `count_tokens` endpoints; does not require this extension.
  See "Remote providers" section above.

## License

Apache-2.0. See [LICENSE](LICENSE).

Vocabulary files for built-in encodings are downloaded from OpenAI's public CDN
at runtime and are subject to OpenAI's terms of service. They are not bundled
with this extension.
