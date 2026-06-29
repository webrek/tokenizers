# tokenizers

> A native PHP extension that counts, encodes, and decodes LLM tokens — **byte-exact**
> with the reference tokenizers, **fast**, and with **no Rust toolchain**. Plus a
> pure-PHP companion that counts **Claude** and **Gemini** tokens through their
> official APIs.

🌐 **Español:** [Inicio](es/index.md)

Think of it as a **scale for AI text**: before you send a prompt to an LLM, weigh it
in tokens so you know what it will **cost** and whether it **fits** the model's
context window — all from PHP, exactly, without rebuilding a 26 MB vocabulary on
every request.

```php
use Tokenizers\Encoding;

$enc = Encoding::load('cl100k_base');                 // GPT-4 / GPT-4o-class encoding
echo $enc->countTokens('Hello, world! 🎉');           // 7
```

## Where to go next

<div class="grid cards" markdown>

- :material-rocket-launch: **[Getting Started](getting-started.md)**
  Install, enable, verify, and run your first tokenization.

- :material-book-open-variant: **[Loading models](guides/loading-models.md)**
  OpenAI / HuggingFace BPE, WordPiece, Unigram, options, and the cache.

- :material-cash-multiple: **[Estimating LLM costs](guides/estimating-costs.md)**
  Budget spend, fit context windows, and track usage per client.

- :material-cloud-outline: **[Remote providers](guides/remote-providers.md)**
  Count Claude and Gemini tokens via their official APIs.

- :material-code-braces: **[API Reference](api-reference.md)**
  Every class, method, and function with exact signatures.

- :material-check-decagram: **[Status & Limitations](status.md)**
  What's verified, conformance results, and honest limits.

</div>

## What it supports

| Algorithm | Models | How to load |
|---|---|---|
| **BPE** (tiktoken) | GPT-4, GPT-4o, o1, o3 | `Encoding::load('cl100k_base' / 'o200k_base')` |
| **BPE** (HuggingFace) | GPT-2, RoBERTa, Llama 3, Mistral, Qwen, DeepSeek | `Encoding::fromHuggingFace('tokenizer.json')` |
| **WordPiece** | BERT family | `Encoding::fromHuggingFace('tokenizer.json')` |
| **Unigram** | T5, ALBERT (SentencePiece) | `Encoding::fromHuggingFace('tokenizer.json')` |
| **Remote** | Claude 3+, Gemini (no local tokenizer) | `TokenCounter::count($model, $text)` |

All local algorithms are verified **byte-exact** against their Python references.
See [Status & Limitations](status.md) for the full conformance results and honest caveats.
