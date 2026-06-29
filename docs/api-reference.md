# API Reference — `tokenizers` extension v0.1.0

> For guides and tutorials, see the [guides](guides/loading-models.md) and [Getting Started](getting-started.md).

---

## Table of Contents

1. [Constants](#1-constants)
2. [`\Tokenizers\Bpe`](#2-tokenizersbpe)
   - [Static factory methods](#static-factory-methods)
   - [Instance methods](#instance-methods)
3. [`\Tokenizers\WordPiece`](#3-tokenizerswordpiece)
   - [Static factory method](#static-factory-method-wordpiece)
   - [Instance methods](#instance-methods-wordpiece)
   - [`$opts` reference](#opts-reference-wordpiece)
4. [`\Tokenizers\Unigram`](#4-tokenizersunigram)
   - [Static factory method](#static-factory-method-unigram)
   - [Instance methods](#instance-methods-unigram)
   - [`$opts` reference](#opts-reference-unigram)
5. [`\Tokenizers\Encoding`](#5-tokenizersencoding)
   - [Cache directory resolution](#cache-directory-resolution)
6. [Procedural functions](#6-procedural-functions)
7. [Remote companion — `\Tokenizers\Remote`](#7-remote-companion--tokenizersremote)
   - [`Transport` interface](#transport-interface)
   - [`Anthropic`](#anthropic)
   - [`Gemini`](#gemini)
   - [Environment variable reference](#environment-variable-reference)
8. [`\Tokenizers\TokenCounter`](#8-tokenizerstokencounter)
9. [`\Tokenizers\TokenizerException`](#9-tokenizerstokenizerexception)
10. [See also](#see-also)

---

## 1. Constants

### `\Tokenizers\VERSION`

```php
const \Tokenizers\VERSION = "0.1.0";
```

The extension version string. Also available via the procedural helper `tokenizers_version()`.

```php
echo \Tokenizers\VERSION; // "0.1.0"
```

---

## 2. `\Tokenizers\Bpe`

Native C class. Implements byte-level BPE (byte-pair encoding), compatible with OpenAI's tiktoken library. Suitable for `cl100k_base` (GPT-4, o1, o3), `o200k_base` (GPT-4o multimodal, o1 mini/pro), and open-weight models loaded from a HuggingFace `tokenizer.json` (GPT-2, RoBERTa, Llama 3, Mistral, Qwen, DeepSeek).

Merge complexity is O(n log n) — heap-based, so adversarial inputs do not trigger quadratic blowup. The vocabulary is loaded once per worker process into a process-global cache (ZTS-safe via a TSRM mutex).

```php
final class Bpe { /* ... */ }
```

### Static factory methods

#### `Bpe::fromTiktokenFile()`

```php
public static function fromTiktokenFile(
    string $path,
    string $pattern,
    array  $specialTokens = []
): Bpe;
```

Loads a vocabulary from a tiktoken `.tiktoken` file on disk. `$path` is the absolute path to the file. `$pattern` is the splitting regex (e.g. the cl100k_base pattern). `$specialTokens` is a map of special-token string to integer id.

```php
$bpe = \Tokenizers\Bpe::fromTiktokenFile(
    '/path/to/cl100k_base.tiktoken',
    '/(?i:\'s|\'t|\'re|\'ve|\'m|\'ll|\'d)|[^\r\n\p{L}\p{N}]?\p{L}+|\p{N}{1,3}| ?[^\s\p{L}\p{N}]+[\r\n]*|\s*[\r\n]+|\s+(?!\S)|\s+/',
    ['<|endoftext|>' => 100257]
);
```

#### `Bpe::fromVocab()`

```php
public static function fromVocab(
    array  $tokenBytesToId,
    array  $merges,
    string $pattern,
    array  $specialTokens = []
): Bpe;
```

Constructs a `Bpe` instance directly from in-memory data. `$tokenBytesToId` maps base64-encoded byte sequences to integer ids. `$merges` is an ordered list of merge pairs. `$pattern` is the splitting regex. `$specialTokens` maps special-token strings to ids.

```php
$bpe = \Tokenizers\Bpe::fromVocab($tokenBytesToId, $merges, $pattern);
```

### Instance methods

#### `encode()`

```php
public function encode(
    string       $text,
    array|string $allowedSpecial    = [],
    array|string $disallowedSpecial = "all"
): array; // int[]
```

Encodes `$text` into an array of integer token ids.

**Special-token handling:** By default `$disallowedSpecial = "all"` means every special token in the vocabulary is treated as plain text (i.e. it will not be encoded as a single special-token id). Pass `"all"` to `$allowedSpecial` to allow all special tokens to be encoded as their ids, or pass an explicit list such as `['<|endoftext|>']` to allow only those tokens.

| Parameter | Type | Default | Description |
|---|---|---|---|
| `$text` | `string` | — | Input text to encode |
| `$allowedSpecial` | `array\|string` | `[]` | Tokens that may be encoded as special ids. Pass `"all"` or a list. |
| `$disallowedSpecial` | `array\|string` | `"all"` | Tokens that must NOT appear; causes an error if encountered. |

```php
$ids = $bpe->encode('Hello world');
// [9906, 1917]

// Allow the end-of-text marker to pass through as a special id:
$ids = $bpe->encode('<|endoftext|>text', allowedSpecial: ['<|endoftext|>']);

// Allow every special token:
$ids = $bpe->encode($text, allowedSpecial: 'all');
```

#### `countTokens()`

```php
public function countTokens(string $text): int;
```

Returns the number of tokens `$text` encodes to, without allocating the full id array. Faster than `count($this->encode($text))` for large inputs.

```php
$n = $bpe->countTokens('Hola mundo!'); // 4
```

#### `decode()`

```php
public function decode(array $ids): string;
```

Decodes an array of integer token ids back to a UTF-8 string.

```php
echo $bpe->decode([9906, 1917]); // "Hello world"
```

#### `decodeSingle()`

```php
public function decodeSingle(int $id): string;
```

Decodes a single token id to its byte representation. Useful for inspecting individual tokens.

```php
$bytes = $bpe->decodeSingle(9906); // "Hello"
```

#### `vocabSize()`

```php
public function vocabSize(): int;
```

Returns the total vocabulary size (base tokens + special tokens). For `cl100k_base` this is `100277`.

```php
echo $bpe->vocabSize(); // 100277 for cl100k_base
```

#### `name()`

```php
public function name(): ?string;
```

Returns the encoding name, or `null`. In v0.1 this always returns `null` — name tracking is not yet implemented.

```php
var_dump($bpe->name()); // NULL
```

---

## 3. `\Tokenizers\WordPiece`

Native C class. Implements greedy longest-match WordPiece tokenization (BERT family). Uses a `##` continuation prefix for subword pieces, falls back to `[UNK]` for out-of-vocabulary words.

**Normalization scope (v0.1):** Latin-1 and CJK-spacing only. Non-Latin scripts that require full Unicode NFD decomposition are out of scope for v0.1 and may produce different results from the reference.

```php
final class WordPiece { /* ... */ }
```

### Static factory method {#static-factory-method-wordpiece}

#### `WordPiece::fromVocab()`

```php
public static function fromVocab(
    array $tokenToId,
    array $opts = []
): WordPiece;
```

Builds a `WordPiece` tokenizer from a vocabulary map. `$tokenToId` maps token strings (e.g. `"hello"`, `"##ing"`) to their integer ids. See `$opts` reference below.

```php
$wp = \Tokenizers\WordPiece::fromVocab($vocab, [
    'lowercase'  => true,
    'unkToken'   => '[UNK]',
]);
```

To load directly from a BERT-style `vocab.txt` file, use `\Tokenizers\Encoding::wordPieceFromVocabFile()`.

### Instance methods {#instance-methods-wordpiece}

#### `encode()`

```php
public function encode(string $text): array; // int[]
```

Encodes `$text` and returns an array of integer token ids.

```php
$ids = $wp->encode('unbelievable tokenization'); // [23653, 19204, 3989]
```

#### `countTokens()`

```php
public function countTokens(string $text): int;
```

Returns the token count without materialising the full id array.

```php
$n = $wp->countTokens('Hello world');
```

#### `decode()`

```php
public function decode(array $ids): string;
```

Reconstructs text from a list of token ids. Strips the `##` continuation prefix when reassembling subword pieces.

```php
echo $wp->decode([23653, 19204, 3989]);
```

#### `vocabSize()`

```php
public function vocabSize(): int;
```

Returns the size of the vocabulary.

```php
echo $wp->vocabSize();
```

### `$opts` reference {#opts-reference-wordpiece}

All keys are optional. Unspecified keys use the listed defaults.

| Key | Type | Default | Description |
|---|---|---|---|
| `unkToken` | `string` | `"[UNK]"` | Token used for out-of-vocabulary words |
| `continuingSubwordPrefix` | `string` | `"##"` | Prefix prepended to continuation subword pieces |
| `maxInputCharsPerWord` | `int` | `100` | Words longer than this character limit are mapped to `[UNK]` |
| `lowercase` | `bool` | `true` | Lowercases input before tokenization |
| `stripAccents` | `bool` | `true` | Strips diacritic accents (Latin-1 scope) |
| `handleChineseChars` | `bool` | `true` | Pads CJK codepoints with spaces before tokenization |

---

## 4. `\Tokenizers\Unigram`

Native C class. Implements SentencePiece Unigram tokenization (T5, ALBERT). Uses a Metaspace (`▁`, U+2581) to encode leading spaces, and Viterbi best-path decoding over piece log-probability scores (f64).

**Normalization scope (v0.1):** Metaspace and identity-on-ASCII. Inputs requiring NFKC normalization, and some whitespace-edge cases (leading/trailing/multiple spaces), may differ from the reference tokenizer.

```php
final class Unigram { /* ... */ }
```

### Static factory method {#static-factory-method-unigram}

#### `Unigram::fromVocab()`

```php
public static function fromVocab(
    array $pieces,
    array $opts = []
): Unigram;
```

Constructs a `Unigram` tokenizer from a list of `(piece, score)` pairs. `$pieces` is an array of `[string, float]` entries as found in a SentencePiece `tokenizer.json`. See `$opts` reference below.

```php
$ug = \Tokenizers\Unigram::fromVocab($pieces, ['addPrefixSpace' => true]);
```

To load from a HuggingFace `tokenizer.json` automatically, use `\Tokenizers\Encoding::fromHuggingFace()`.

### Instance methods {#instance-methods-unigram}

#### `encode()`

```php
public function encode(string $text): array; // int[]
```

Encodes `$text` and returns an array of integer token ids via Viterbi best-path search.

```php
$ids = $ug->encode('Hello world'); // [8774, 296]
```

#### `countTokens()`

```php
public function countTokens(string $text): int;
```

Returns the token count without materialising the full id array.

```php
$n = $ug->countTokens('Hello world');
```

#### `decode()`

```php
public function decode(array $ids): string;
```

Reconstructs text from a list of token ids, replacing leading `▁` characters with spaces.

```php
echo $ug->decode([8774, 296]); // "Hello world"
```

#### `vocabSize()`

```php
public function vocabSize(): int;
```

Returns the size of the vocabulary.

```php
echo $ug->vocabSize();
```

### `$opts` reference {#opts-reference-unigram}

All keys are optional. Unspecified keys use the listed defaults.

| Key | Type | Default | Description |
|---|---|---|---|
| `unkId` | `int` | id of `<unk>` if present, else `0` | Token id to emit for unknown pieces |
| `addPrefixSpace` | `bool` | `true` | Prepend a Metaspace (`▁`) to the input before encoding |

---

## 5. `\Tokenizers\Encoding`

Pure-PHP shim (`php/Tokenizers/Encoding.php`). Provides high-level loaders that download, checksum-verify, and cache vocabulary files, then return the appropriate native tokenizer instance. Also dispatches HuggingFace tokenizer JSON files to the correct C class.

```php
final class Encoding { /* ... */ }
```

#### `Encoding::load()`

```php
public static function load(string $name): Bpe;
```

Loads a named built-in encoding. Downloads and checksum-verifies the vocabulary on first use; subsequent calls return from the process-global cache. Currently known encodings: `'cl100k_base'` and `'o200k_base'`.

Throws `\Tokenizers\TokenizerException("unknown encoding: <name>")` for unrecognised names.

```php
use Tokenizers\Encoding;

$enc = Encoding::load('cl100k_base');
echo $enc->countTokens('Hello world'); // 2
echo $enc->vocabSize();                // 100277
```

#### `Encoding::fromHuggingFace()`

```php
public static function fromHuggingFace(string $jsonPath): Bpe|WordPiece|Unigram;
```

Reads a HuggingFace `tokenizer.json` file and auto-dispatches by the `model.type` field:

| `model.type` value | Returned class |
|---|---|
| `"BPE"` | `\Tokenizers\Bpe` |
| `"WordPiece"` | `\Tokenizers\WordPiece` |
| `"Unigram"` | `\Tokenizers\Unigram` |

```php
$bpe  = Encoding::fromHuggingFace('path/to/llama3/tokenizer.json'); // Bpe
$wp   = Encoding::fromHuggingFace('path/to/bert/tokenizer.json');   // WordPiece
$ug   = Encoding::fromHuggingFace('path/to/t5/tokenizer.json');     // Unigram
```

#### `Encoding::wordPieceFromVocabFile()`

```php
public static function wordPieceFromVocabFile(
    string $path,
    array  $opts = []
): WordPiece;
```

Loads a plain BERT-style `vocab.txt` file (one token per line, line number = id) into a `WordPiece` tokenizer. `$opts` accepts the same keys as [`WordPiece::fromVocab()`](#opts-reference-wordpiece).

```php
$wp = Encoding::wordPieceFromVocabFile('/path/to/vocab.txt', ['lowercase' => true]);
```

#### `Encoding::cacheDir()`

```php
public static function cacheDir(): string;
```

Returns the resolved path to the directory where downloaded vocabulary files are stored. Useful for debugging cache location.

```php
echo Encoding::cacheDir();
// e.g. /home/user/.cache/tokenizers
```

#### `Encoding::download()`

```php
public static function download(string $url, ?string $sha256, string $dest): void;
```

Low-level utility. Downloads `$url` to `$dest`, optionally verifying the file against `$sha256`. Used internally by `Encoding::load()`; you generally do not need to call this directly.

```php
Encoding::download(
    'https://example.com/vocab.tiktoken',
    'abc123...',
    '/tmp/vocab.tiktoken'
);
```

### Cache directory resolution

`Encoding::load()` stores downloaded vocabulary files in the first directory that resolves from the following ordered list:

1. `$TOKENIZERS_CACHE_DIR/tokenizers`
2. `$XDG_CACHE_HOME/tokenizers`
3. `$HOME/.cache/tokenizers`
4. `sys_get_temp_dir()/tokenizers`

Built-in vocabulary files are downloaded from OpenAI's public CDN on first use, checksum-verified, and are never redistributed with the extension.

---

## 6. Procedural functions

Global-namespace procedural functions. All functions that operate on a tokenizer accept a `\Tokenizers\Bpe` instance only — `WordPiece` and `Unigram` are not accepted by these functions.

#### `tokenizers_version()`

```php
function tokenizers_version(): string;
```

Returns the extension version string `"0.1.0"`. Equivalent to `\Tokenizers\VERSION`.

```php
echo tokenizers_version(); // "0.1.0"
```

#### `tokenizers_cache_count()`

```php
function tokenizers_cache_count(): int;
```

Returns the number of tokenizer models currently held in the process-global vocabulary cache. Useful for diagnostics.

```php
echo tokenizers_cache_count(); // e.g. 1 after loading cl100k_base
```

#### `tokenizers_encode()`

```php
function tokenizers_encode(
    \Tokenizers\Bpe $t,
    string          $text,
    array           $allowedSpecial    = [],
    array|string    $disallowedSpecial = "all"
): array; // int[]
```

Procedural wrapper for `Bpe::encode()`. See [`encode()`](#encode) for `$allowedSpecial` and `$disallowedSpecial` semantics.

```php
$ids = tokenizers_encode($bpe, 'Hello world'); // [9906, 1917]
```

#### `tokenizers_decode()`

```php
function tokenizers_decode(\Tokenizers\Bpe $t, array $ids): string;
```

Procedural wrapper for `Bpe::decode()`.

```php
echo tokenizers_decode($bpe, [9906, 1917]); // "Hello world"
```

#### `tokenizers_count()`

```php
function tokenizers_count(\Tokenizers\Bpe $t, string $text): int;
```

Procedural wrapper for `Bpe::countTokens()`.

```php
$n = tokenizers_count($bpe, 'Hello world'); // 2
```

---

## 7. Remote companion — `\Tokenizers\Remote`

Pure-PHP classes that count tokens via the official provider APIs. They work **without** the C extension loaded (they bootstrap a `TokenizerException` polyfill via `require_once` of `php/Tokenizers/TokenizerException.php`). They require `ext-curl` and `ext-json`. They use raw curl — they do not depend on `anthropic-ai/sdk`, Guzzle, or any other HTTP library.

**Note:** Claude 3+ and Gemini models have no local tokenizer. Exact token counts for those models require a network call and a valid API key. Providers may change their tokenizer at any time.

### `Transport` interface

```php
interface Transport {
    public function post(
        string $url,
        array  $headers,
        string $body,
        int    $timeout
    ): array; // ['status' => int, 'body' => string]
}
```

The HTTP abstraction used by `Anthropic` and `Gemini`. The default implementation is `CurlTransport`. Inject a custom implementation for offline testing.

```php
class FakeTransport implements \Tokenizers\Remote\Transport {
    public function post(string $url, array $headers, string $body, int $timeout): array {
        return ['status' => 200, 'body' => '{"input_tokens":5}'];
    }
}
```

### `Anthropic`

```php
final class Anthropic {
    public function __construct(
        ?string    $apiKey    = null,
        ?Transport $transport = null,
        string     $version   = '2023-06-01',
        int        $timeout   = 30
    );

    public function countTokens(
        string       $model,
        string|array $messages,
        ?string      $system = null
    ): int;
}
```

Counts tokens for an Anthropic (Claude) model via the official API.

**`countTokens()` details:**

- **Endpoint:** `POST https://api.anthropic.com/v1/messages/count_tokens`
- **Headers:** `x-api-key: <key>`, `anthropic-version: 2023-06-01`, `content-type: application/json`
- **Body:** `{"model": <model>, "messages": [...], "system": <system?>}`
  - A plain `string` for `$messages` becomes a single `{"role":"user","content": <text>}` turn.
  - An `array` is sent as-is (for multi-turn conversations).
- **Response field parsed:** `input_tokens`
- **API key resolution:** `$apiKey` constructor argument, otherwise `ANTHROPIC_API_KEY` environment variable. A missing key throws `TokenizerException` at call time.
- **Errors:** Non-2xx HTTP status or a malformed/missing `input_tokens` field throws `TokenizerException`.

```php
use Tokenizers\Remote\Anthropic;

$anthropic = new Anthropic(); // reads ANTHROPIC_API_KEY from env
$n = $anthropic->countTokens('claude-opus-4-8', 'Hello, world!');

// Multi-turn:
$n = $anthropic->countTokens('claude-opus-4-8', [
    ['role' => 'user',      'content' => 'Hi'],
    ['role' => 'assistant', 'content' => 'Hello!'],
    ['role' => 'user',      'content' => 'How are you?'],
]);

// With a system prompt:
$n = $anthropic->countTokens('claude-opus-4-8', 'Hello', system: 'You are a helpful assistant.');
```

### `Gemini`

```php
final class Gemini {
    public function __construct(
        ?string    $apiKey    = null,
        ?Transport $transport = null,
        int        $timeout   = 30
    );

    public function countTokens(string $model, string $text): int;
}
```

Counts tokens for a Google Gemini model via the official API.

**`countTokens()` details:**

- **Endpoint:** `POST https://generativelanguage.googleapis.com/v1beta/models/{model}:countTokens`
  The `{model}` segment is normalised — both `"gemini-1.5-flash"` and `"models/gemini-1.5-flash"` are accepted; the leading `models/` is added or preserved exactly once, never double-prefixed.
- **Header:** `x-goog-api-key: <key>`
- **Body:** `{"contents":[{"parts":[{"text": <text>}]}]}`
- **Response field parsed:** `totalTokens`
- **API key resolution:** `$apiKey` constructor argument, then `GEMINI_API_KEY` env var, then `GOOGLE_API_KEY` env var. A missing key throws `TokenizerException` at call time.
- **Errors:** Non-2xx HTTP status or a malformed/missing `totalTokens` field throws `TokenizerException`.

```php
use Tokenizers\Remote\Gemini;

$gemini = new Gemini(); // reads GEMINI_API_KEY or GOOGLE_API_KEY from env
$n = $gemini->countTokens('gemini-1.5-flash', 'Hello, world!');
// Both forms are equivalent:
$n = $gemini->countTokens('models/gemini-1.5-flash', 'Hello, world!');
```

### Environment variable reference

| Provider | Environment variable(s) | Constructor override |
|---|---|---|
| Anthropic | `ANTHROPIC_API_KEY` | `new Anthropic(apiKey: '...')` |
| Gemini | `GEMINI_API_KEY` (checked first), then `GOOGLE_API_KEY` | `new Gemini(apiKey: '...')` |

---

## 8. `\Tokenizers\TokenCounter`

```php
final class TokenCounter {
    public function __construct(
        ?Anthropic $anthropic = null,
        ?Gemini    $gemini    = null
    );

    public static function route(string $model): string; // 'anthropic' | 'gemini' | 'local'

    public function count(
        string  $model,
        string  $text,
        ?string $provider = null
    ): int;
}
```

High-level facade that dispatches token counting to the right backend based on the model name. Defined in `php/Tokenizers/TokenCounter.php`.

**`route()` — routing rules (no network call):**

| Model prefix | Returns |
|---|---|
| `claude` or `anthropic` | `'anthropic'` |
| `gemini` or `models/gemini` | `'gemini'` |
| anything else | `'local'` |

**`count()` — dispatch logic:**

- `'anthropic'` → calls `Anthropic->countTokens($model, $text)`
- `'gemini'` → calls `Gemini->countTokens($model, $text)`
- `'local'` → calls `Encoding::load($model)->countTokens($text)`
- Passing an explicit `$provider` that is not one of the three recognised values throws `TokenizerException("unknown provider '<p>' for model: <model>")`.

```php
use Tokenizers\TokenCounter;

$tc = new TokenCounter();

// Local (no network, no key needed):
$n = $tc->count('cl100k_base', $text);

// Remote Anthropic (needs ANTHROPIC_API_KEY):
$n = $tc->count('claude-opus-4-8', $text);

// Remote Gemini (needs GEMINI_API_KEY or GOOGLE_API_KEY):
$n = $tc->count('gemini-1.5-flash', $text);

// Inject pre-configured clients:
$tc = new TokenCounter(
    anthropic: new \Tokenizers\Remote\Anthropic(apiKey: 'ant-...'),
    gemini:    new \Tokenizers\Remote\Gemini(apiKey: 'AIza...')
);

// Force a specific provider:
$n = $tc->count('my-model', $text, provider: 'local');

// Inspect routing without counting:
echo TokenCounter::route('claude-sonnet-4'); // 'anthropic'
echo TokenCounter::route('gemini-pro');      // 'gemini'
echo TokenCounter::route('cl100k_base');     // 'local'
```

---

## 9. `\Tokenizers\TokenizerException`

```php
class TokenizerException extends \RuntimeException {}
```

Thrown by all classes in this extension for error conditions. Extends the standard `\RuntimeException`, so it can be caught as either `\Tokenizers\TokenizerException` or `\RuntimeException`.

**Thrown in the following situations:**

- `Encoding::load()` — unknown encoding name: `"unknown encoding: <name>"`
- `Anthropic::countTokens()` / `Gemini::countTokens()` — missing API key (at call time)
- `Anthropic::countTokens()` / `Gemini::countTokens()` — non-2xx HTTP response
- `Anthropic::countTokens()` / `Gemini::countTokens()` — malformed or missing response field
- `TokenCounter::count()` — unknown explicit `$provider`: `"unknown provider '<p>' for model: <model>"`

```php
use Tokenizers\Encoding;
use Tokenizers\TokenizerException;

try {
    $enc = Encoding::load('p50k_base'); // not bundled in v0.1
} catch (TokenizerException $e) {
    echo $e->getMessage(); // "unknown encoding: p50k_base"
}
```

---

## See also

- [Getting Started](getting-started.md) — installation, enabling the extension, first tokenization
- [Status & Limitations](status.md) — conformance results, known limitations, roadmap
- [Guide: Estimating Costs](guides/estimating-costs.md) — budget LLM API costs before calling
- [Guide: Loading Models](guides/loading-models.md) — load OpenAI/HF BPE, WordPiece, Unigram, and the cache
- [Guide: Remote Providers](guides/remote-providers.md) — Claude/Gemini API companion, key setup, honest boundaries
