# tokenizers

> Una extensión nativa de PHP que cuenta, codifica y decodifica tokens de LLM —
> **byte-exacta** con los tokenizers de referencia, **rápida** y **sin toolchain de
> Rust**. Además, un companion en PHP puro que cuenta tokens de **Claude** y
> **Gemini** a través de sus APIs oficiales.

🌐 **English:** [Home](../index.md)

Piénsalo como una **báscula para texto de IA**: antes de enviar un prompt a un LLM,
lo pesas en tokens para saber cuánto va a **costar** y si **cabe** en la ventana de
contexto del modelo — todo desde PHP, exacto, y sin reconstruir un vocabulario de
26 MB en cada petición.

```php
use Tokenizers\Encoding;

$enc = Encoding::load('cl100k_base');                 // encoding clase GPT-4 / GPT-4o
echo $enc->countTokens('Hello, world! 🎉');           // 7
```

## Por dónde seguir

<div class="grid cards" markdown>

- :material-rocket-launch: **[Primeros pasos](getting-started.md)**
  Instalar, habilitar, verificar y tu primera tokenización.

- :material-book-open-variant: **[Cargar modelos](guides/loading-models.md)**
  BPE de OpenAI / HuggingFace, WordPiece, Unigram, opciones y la cache.

- :material-cash-multiple: **[Estimar costos de LLM](guides/estimating-costs.md)**
  Presupuestar el gasto, encajar en la ventana de contexto y medir uso por cliente.

- :material-cloud-outline: **[Proveedores remotos](guides/remote-providers.md)**
  Contar tokens de Claude y Gemini vía sus APIs oficiales.

- :material-code-braces: **[Referencia de la API](api-reference.md)**
  Cada clase, método y función con firmas exactas.

- :material-check-decagram: **[Estado y limitaciones](status.md)**
  Qué está verificado, resultados de conformance y límites honestos.

</div>

## Qué soporta

| Algoritmo | Modelos | Cómo se carga |
|---|---|---|
| **BPE** (tiktoken) | GPT-4, GPT-4o, o1, o3 | `Encoding::load('cl100k_base' / 'o200k_base')` |
| **BPE** (HuggingFace) | GPT-2, RoBERTa, Llama 3, Mistral, Qwen, DeepSeek | `Encoding::fromHuggingFace('tokenizer.json')` |
| **WordPiece** | Familia BERT | `Encoding::fromHuggingFace('tokenizer.json')` |
| **Unigram** | T5, ALBERT (SentencePiece) | `Encoding::fromHuggingFace('tokenizer.json')` |
| **Remoto** | Claude 3+, Gemini (sin tokenizer local) | `TokenCounter::count($model, $text)` |

Todos los algoritmos locales están verificados **byte-exactos** contra sus referencias
en Python. Consulta [Estado y limitaciones](status.md) para los resultados completos
de conformance y los matices honestos.
