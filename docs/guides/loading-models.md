# Loading and Using Local Tokenizers

This guide covers every local tokenizer the `tokenizers` extension supports: how to load it, what options are available, and how to encode, count, and decode tokens — all without a network call or API key.

For remote token counting (Claude, Gemini), see [remote-providers.md](remote-providers.md).

---

## 1. The three algorithms

All three algorithms are implemented in native C inside the `.so`/`.dll` and produce results that are byte-exact with their reference implementations:

| Algorithm | Class | Reference | Typical models |
|-----------|-------|-----------|----------------|
| **BPE** (byte-level, tiktoken-compatible) | `\Tokenizers\Bpe` | Python `tiktoken` | GPT-4, GPT-4o, Llama 3, Mistral, Qwen, DeepSeek |
| **WordPiece** | `\Tokenizers\WordPiece` | HuggingFace `BertTokenizerFast` | BERT and its family |
| **Unigram** (SentencePiece) | `\Tokenizers\Unigram` | HuggingFace `t5-small` | T5, ALBERT |

"Byte-exact" means the extension produces the identical token IDs as the Python reference on every verified test case — not an approximation. Conformance fixtures are committed to the repository and CI fails on any diff.

All three classes live in the `\Tokenizers\` namespace. The high-level factory `\Tokenizers\Encoding` auto-selects the right class for you based on the tokenizer file you provide.

---

## 2. OpenAI encodings

Use `Encoding::load()` to load one of the two OpenAI built-in encodings by name.

```php
<?php
use Tokenizers\Encoding;
use Tokenizers\TokenizerException;

// Load cl100k_base — used by GPT-4, GPT-4o (text), o1, o3
$enc = Encoding::load('cl100k_base');

// Load o200k_base — used by GPT-4o (multimodal), o1 mini, o1 pro
$enc = Encoding::load('o200k_base');

// Throws TokenizerException("unknown encoding: <name>") for anything else
```

**Which models use which encoding:**

| Encoding | Models |
|----------|--------|
| `cl100k_base` | GPT-4, GPT-4o text generation, o1, o3 |
| `o200k_base` | GPT-4o multimodal, o1 mini, o1 pro |

**On first use**, the vocab file is downloaded from OpenAI's public CDN, checksum-verified, and written to the local cache directory. Subsequent uses (in the same process and in future processes) load from the cache — no download. The vocab is never redistributed with the extension itself.

`cl100k_base` has a vocabulary of 100,277 tokens:

```php
echo $enc->vocabSize(); // 100277
```

**Encoding, counting, and decoding:**

```php
use Tokenizers\Encoding;

$enc = Encoding::load('cl100k_base');

// Count tokens without producing the full array
$count = $enc->countTokens('Hello, world!'); // fast, no allocation of the id array

// Encode to token IDs
$ids = $enc->encode('Hello world'); // [9906, 1917]

// Decode token IDs back to text
$text = $enc->decode($ids); // 'Hello world'

// Decode a single token ID
$piece = $enc->decodeSingle(9906); // 'Hello'
```

---

## 3. HuggingFace models (`tokenizer.json`)

`Encoding::fromHuggingFace($path)` loads any HuggingFace tokenizer from its `tokenizer.json` file and returns the appropriate typed object — `Bpe`, `WordPiece`, or `Unigram` — dispatched automatically by the `model.type` field inside the JSON.

You point the method at the `tokenizer.json` you already have on disk (downloaded separately from the HuggingFace Hub or bundled with your project).

### BPE — GPT-2, Llama 3, Mistral, Qwen, DeepSeek

```php
<?php
use Tokenizers\Encoding;
use Tokenizers\Bpe;

// Any HF model whose tokenizer.json has model.type == "BPE"
// e.g. GPT-2, Llama 3, Mistral (tekken), Qwen, DeepSeek
$bpe = Encoding::fromHuggingFace('/path/to/llama3/tokenizer.json');

assert($bpe instanceof Bpe);

$ids  = $bpe->encode('The quick brown fox');
$n    = $bpe->countTokens('The quick brown fox');
$text = $bpe->decode($ids);
```

### WordPiece — BERT

```php
<?php
use Tokenizers\Encoding;
use Tokenizers\WordPiece;

$wp = Encoding::fromHuggingFace('/path/to/bert-base-uncased/tokenizer.json');

assert($wp instanceof WordPiece);

$ids  = $wp->encode('unbelievable tokenization'); // [23653, 19204, 3989]
$n    = $wp->countTokens('unbelievable tokenization');
$text = $wp->decode($ids);
```

### Unigram — T5

```php
<?php
use Tokenizers\Encoding;
use Tokenizers\Unigram;

$ug = Encoding::fromHuggingFace('/path/to/t5-small/tokenizer.json');

assert($ug instanceof Unigram);

$ids  = $ug->encode('Hello world'); // [8774, 296]
$n    = $ug->countTokens('Hello world');
$text = $ug->decode($ids);
```

---

## 4. WordPiece options

If you have a plain `vocab.txt` file (one token per line, as BERT distributes), use `Encoding::wordPieceFromVocabFile()` instead of `fromHuggingFace()`:

```php
<?php
use Tokenizers\Encoding;

$wp = Encoding::wordPieceFromVocabFile('/path/to/vocab.txt', [
    'unkToken'               => '[UNK]',
    'continuingSubwordPrefix'=> '##',
    'maxInputCharsPerWord'   => 100,
    'lowercase'              => true,
    'stripAccents'           => true,
    'handleChineseChars'     => true,
]);
```

All `$opts` keys are optional. The full table with defaults:

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `unkToken` | `string` | `"[UNK]"` | Token emitted when a word cannot be segmented |
| `continuingSubwordPrefix` | `string` | `"##"` | Prefix prepended to non-initial sub-word pieces |
| `maxInputCharsPerWord` | `int` | `100` | Words longer than this map directly to `[UNK]` |
| `lowercase` | `bool` | `true` | Lowercase input before tokenizing |
| `stripAccents` | `bool` | `true` | Remove combining accent characters after lowercasing |
| `handleChineseChars` | `bool` | `true` | Pad CJK codepoints with spaces so they tokenize as individual characters |

**Normalization scope note:** The WordPiece normalizer in v0.1 covers Latin-1 and CJK spacing only — it does not perform full Unicode NFD decomposition. Non-Latin scripts that require decomposition (e.g. Arabic, Devanagari) are out of scope for v1. See [../status.md](../status.md) for details.

---

## 5. Unigram options

`Unigram::fromVocab()` is the low-level constructor (called internally by `fromHuggingFace`). When loading via `fromHuggingFace` you do not need to pass options — they are read from the JSON. The options are documented here for reference:

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `unkId` | `int` | ID of `<unk>` piece if present, else `0` | Token ID emitted when no piece covers the input |
| `addPrefixSpace` | `bool` | `true` | Prepend a leading Metaspace (`▁`, U+2581) to the input before segmenting |

**Normalization caveats:** The Unigram implementation handles Metaspace and is identity-on-ASCII. Inputs that require NFKC normalization and some whitespace edge cases (leading, trailing, or multiple consecutive spaces) can differ from the SentencePiece reference. See [../status.md](../status.md) for the full list of known deviations.

---

## 6. Special tokens (BPE)

By default, `Bpe::encode()` treats special tokens as errors: if the input text contains a string like `<|endoftext|>` and you have not explicitly allowed it, the call throws a `TokenizerException`.

```php
use Tokenizers\Encoding;
use Tokenizers\TokenizerException;

$enc = Encoding::load('cl100k_base');

// Default: disallows all special tokens — throws if any appear in $text
// $enc->encode('<|endoftext|> hello');   // TokenizerException

// Allow specific special tokens
$ids = $enc->encode('<|endoftext|> hello', allowedSpecial: ['<|endoftext|>']);

// Allow all special tokens
$ids = $enc->encode('<|endoftext|> hello', allowedSpecial: 'all');

// The second parameter is $allowedSpecial; the third is $disallowedSpecial
// (default: 'all', meaning all specials not in $allowedSpecial are disallowed)
$ids = $enc->encode($text, allowedSpecial: [], disallowedSpecial: 'all');
```

`countTokens()` does not accept special-token arguments — it operates on plain text only. Use `encode()` when you need special-token control.

---

## 7. Special tokens (BPE) — low-level constructors

For advanced use cases (building a custom BPE tokenizer from scratch, or loading a tiktoken `.tiktoken` file directly), two additional static constructors are available on `Bpe`:

```php
use Tokenizers\Bpe;

// Load from a .tiktoken-format file (base64 token → id, one per line)
$bpe = Bpe::fromTiktokenFile(
    path: '/path/to/vocab.tiktoken',
    pattern: '/(?i:...)/',          // regex split pattern
    specialTokens: ['<|endoftext|>' => 100257],
);

// Construct from in-memory arrays
$bpe = Bpe::fromVocab(
    tokenBytesToId: $tokenBytesToId, // array<string, int>: token bytes → id
    merges: $merges,                 // array<string>: merge rules in order
    pattern: '/(?i:...)/',
    specialTokens: [],
);
```

These constructors are what `Encoding::load()` and `Encoding::fromHuggingFace()` call internally. You only need them if you are loading a tokenizer format that `Encoding` does not yet support.

---

## 8. The process cache

Vocabulary data is loaded **once per worker process** into a process-global in-memory cache. Every subsequent `Encoding::load()` or `Encoding::fromHuggingFace()` call for the same file returns the cached tokenizer immediately, without re-parsing or re-allocating the vocab. Under ZTS (thread-safe PHP), the cache is protected by a TSRM mutex.

This is the primary memory win over pure-PHP alternatives, which typically rebuild ~26 MB of vocab data on every request.

**Inspecting the cache:**

```php
<?php
use Tokenizers\Encoding;

$enc1 = Encoding::load('cl100k_base'); // loads from disk / CDN, populates cache
$enc2 = Encoding::load('cl100k_base'); // served from cache

echo tokenizers_cache_count(); // 1

$enc3 = Encoding::load('o200k_base'); // second vocab loaded
echo tokenizers_cache_count(); // 2
```

**Cache directory resolution** (first that is set wins):

1. `$TOKENIZERS_CACHE_DIR/tokenizers`
2. `$XDG_CACHE_HOME/tokenizers`
3. `$HOME/.cache/tokenizers`
4. `sys_get_temp_dir()/tokenizers`

To override the location, set the `TOKENIZERS_CACHE_DIR` environment variable before any vocab is loaded:

```php
putenv('TOKENIZERS_CACHE_DIR=/var/cache/myapp');
// or in the shell: export TOKENIZERS_CACHE_DIR=/var/cache/myapp
```

You can also inspect the resolved path at runtime:

```php
use Tokenizers\Encoding;

echo Encoding::cacheDir(); // e.g. /home/user/.cache/tokenizers
```

Built-in OpenAI vocab files (`cl100k_base`, `o200k_base`) are downloaded from OpenAI's public CDN on first use, checksum-verified, and stored in the cache directory. They are never bundled with or redistributed by this extension.

---

## 9. The procedural API

For environments that prefer procedural calls over object methods, the extension also exposes a set of global functions. These functions accept a `Bpe` object as their first argument and otherwise mirror the OOP API.

```php
<?php
use Tokenizers\Encoding;

$bpe = Encoding::load('cl100k_base');
$text = 'Hola mundo! Tokenizing con una extensión PHP nativa.';

// Count tokens
$n    = tokenizers_count($bpe, $text);

// Encode to IDs (with optional allowed/disallowed special token arrays)
$ids  = tokenizers_encode($bpe, $text, allowedSpecial: [], disallowedSpecial: 'all');

// Decode IDs back to text
$out  = tokenizers_decode($bpe, $ids);

// Extension version
echo tokenizers_version(); // "0.1.0"
// Same as the constant:
echo \Tokenizers\VERSION;  // "0.1.0"
```

Note: procedural functions operate on `Bpe` only. For `WordPiece` and `Unigram`, use the OOP methods on those classes directly.

---

## See also

- [remote-providers.md](remote-providers.md) — count Claude and Gemini tokens via their APIs
- [estimating-costs.md](estimating-costs.md) — estimate LLM costs from token counts
- [../api-reference.md](../api-reference.md) — complete API reference for all classes and functions
- [../getting-started.md](../getting-started.md) — installation and first steps
- [../status.md](../status.md) — conformance results, normalization limitations, and roadmap
