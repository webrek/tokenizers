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
counts, call the provider's `count_tokens` API directly. A Phase 3 companion PHP
library that wraps those APIs is planned; it will not require this extension.

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
use Tokenizers\Bpe;

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

## Vocabulary caching

Built-in encodings are downloaded from OpenAI's public CDN on first use and
checksum-verified. They are **never redistributed** with the extension. The cache
location is selected in this order:

1. `$TOKENIZERS_CACHE_DIR`
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

## Roadmap

- **Phase 1 (current — v0.1.0):** Byte-level BPE, `cl100k_base`, `o200k_base`,
  HuggingFace BPE `tokenizer.json`. O(n log n) merge. Tiktoken-conformant.
- **Phase 2:** WordPiece and Unigram algorithms → BERT, T5, and related models.
- **Phase 3:** Claude / Gemini API companion — a pure-PHP library that calls the
  provider `count_tokens` endpoints; does not require this extension.

## License

Apache-2.0. See [LICENSE](LICENSE).

Vocabulary files for built-in encodings are downloaded from OpenAI's public CDN
at runtime and are subject to OpenAI's terms of service. They are not bundled
with this extension.
