# tokenizers

🌐 **Español:** [README.es.md](README.es.md)

> A native PHP extension that counts, encodes, and decodes LLM tokens — **byte-exact** with the reference tokenizers, **fast**, and with **no Rust toolchain**. Plus a pure-PHP companion that counts **Claude** and **Gemini** tokens through their official APIs.

[![CI](https://github.com/webrek/tokenizers/actions/workflows/ci.yml/badge.svg)](https://github.com/webrek/tokenizers/actions/workflows/ci.yml)
[![docs](https://img.shields.io/badge/docs-webrek.github.io-blue)](https://webrek.github.io/tokenizers/)
![version](https://img.shields.io/badge/version-0.1.0-blue)
![php](https://img.shields.io/badge/php-8.3%20%7C%208.4-777bb4)
![thread safety](https://img.shields.io/badge/ZTS-supported-success)
![license](https://img.shields.io/badge/license-Apache--2.0-green)

Think of it as a **scale for AI text**: before you send a prompt to an LLM, weigh it
in tokens so you know what it will **cost** and whether it **fits** the model's
context window — all from PHP, exactly, without rebuilding a 26 MB vocabulary on
every request.

```php
use Tokenizers\Encoding;

$enc = Encoding::load('cl100k_base');                 // GPT-4 / GPT-4o-class encoding
echo $enc->countTokens('Hello, world! 🎉');           // 7
```

---

## Why this extension?

| Property | Pure-PHP tiktoken port | **This extension** |
|---|---|---|
| Memory per worker | ~26 MB vocab rebuilt **every request** | Loaded **once** per worker process |
| Worst-case latency | O(n²) per pre-token piece | **O(n log n)** heap-based merge |
| Install | Pure PHP | Single `.so` — **no Rust**, no `ffi.enable` |
| Accuracy | Approximate | **Byte-exact** vs the reference (tiktoken / BERT / T5) |
| Models | OpenAI only | OpenAI **+ BERT + T5 + Llama/Mistral/Qwen… + Claude/Gemini via API** |

The wins are **memory, worst-case latency, accuracy, and installability** — not raw
throughput on tiny inputs. For prompts that fit in a tweet, a pure-PHP port can be
faster (no extension-call overhead). This extension is for workloads where the
26 MB-per-worker overhead, adversarial inputs, or byte-exactness actually matter.

## Supported models

### Locally, byte-exact

| Algorithm | Models | How to load |
|---|---|---|
| **BPE** (tiktoken) | GPT-4, GPT-4o (text), o1, o3 | `Encoding::load('cl100k_base')` |
| **BPE** (tiktoken) | GPT-4o (multimodal), o1 mini/pro | `Encoding::load('o200k_base')` |
| **BPE** (HuggingFace) | GPT-2, RoBERTa, Llama 3, Mistral (tekken), Qwen, DeepSeek | `Encoding::fromHuggingFace('tokenizer.json')` |
| **WordPiece** | BERT family (bert-base-uncased, …) | `Encoding::fromHuggingFace('tokenizer.json')` |
| **Unigram** | T5, ALBERT (SentencePiece) | `Encoding::fromHuggingFace('tokenizer.json')` |

Conformance is verified byte-for-byte against Python `tiktoken`, HuggingFace
`BertTokenizerFast`, and `t5-small`. Any diff against the committed fixtures fails
CI. See [Status & conformance](docs/status.md).

### Remotely (no public tokenizer)

**Claude 3+** and **Gemini** do not publish their tokenizers — there is no local
vocabulary to load. The pure-PHP companion (`Tokenizers\Remote\Anthropic`,
`Tokenizers\Remote\Gemini`, `Tokenizers\TokenCounter`) counts their tokens through
the providers' official `count_tokens` endpoints. It works **without** building the
C extension. See the [Remote providers guide](docs/guides/remote-providers.md).

## Install

```bash
phpize && ./configure && make && make install
```

Then enable it in your `php.ini`:

```ini
extension=tokenizers
```

Verify:

```bash
php -m | grep tokenizers          # → tokenizers
```

`pecl install tokenizers` and `pie install webrek/tokenizers` are also supported.
Full instructions, requirements (`libpcre2`), and troubleshooting are in
**[Getting Started](docs/getting-started.md)**.

## Quick start

```php
use Tokenizers\Encoding;

// OpenAI encoding (vocab downloads + caches on first use)
$enc = Encoding::load('cl100k_base');
$n   = $enc->countTokens($prompt);     // count without allocating the id array
$ids = $enc->encode($prompt);          // int[]
$str = $enc->decode($ids);             // round-trips for plain text

// HuggingFace model — returns Bpe | WordPiece | Unigram by model type
$bert = Encoding::fromHuggingFace('/path/to/bert/tokenizer.json');
$t5   = Encoding::fromHuggingFace('/path/to/t5/tokenizer.json');

// One facade for local + remote, routed by model name
use Tokenizers\TokenCounter;
$tc = new TokenCounter();
$tc->count('cl100k_base',     $text);  // local BPE, no key
$tc->count('claude-opus-4-8', $text);  // remote Anthropic (needs ANTHROPIC_API_KEY)
$tc->count('gemini-1.5-flash',$text);  // remote Gemini   (needs GEMINI_API_KEY)
```

## Documentation

| Guide | What it covers |
|---|---|
| **[Getting Started](docs/getting-started.md)** | Install, enable, verify, first tokenization, troubleshooting |
| **[Loading models](docs/guides/loading-models.md)** | OpenAI / HuggingFace BPE, WordPiece, Unigram, options, the cache |
| **[Estimating LLM costs](docs/guides/estimating-costs.md)** | Budget spend, fit context windows, track usage per client |
| **[Remote providers (Claude / Gemini)](docs/guides/remote-providers.md)** | Counting tokens via the provider APIs |
| **[API reference](docs/api-reference.md)** | Every class, method, and function |
| **[Status & limitations](docs/status.md)** | What's verified, conformance results, honest limits, roadmap |

## Project status

`v0.1.0`, early but functional. All three planned phases are complete and merged:

- **BPE** (cl100k_base, o200k_base, HuggingFace BPE) — byte-exact, O(n log n) merge.
- **WordPiece** (BERT) and **Unigram** (T5/SentencePiece) — byte-exact.
- **Claude / Gemini API companion** — pure PHP, standalone.

Honest caveats live in [Status & limitations](docs/status.md) (normalization scope,
PIE install not yet verified end-to-end, remote counting needs a network call + key).

## License

Apache-2.0 — see [LICENSE](LICENSE). Vocabulary files for built-in encodings are
downloaded from OpenAI's public CDN at runtime, checksum-verified, and **not**
redistributed with the extension.
