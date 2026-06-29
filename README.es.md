# tokenizers

🌐 **English:** [README.md](README.md)

> Una extensión nativa de PHP (en C) que cuenta, codifica y decodifica tokens de LLM — **byte-exacta** con los tokenizers de referencia, **rápida** y **sin toolchain de Rust**. Más un companion en PHP puro que cuenta tokens de **Claude** y **Gemini** a través de sus APIs oficiales.

[![CI](https://github.com/webrek/tokenizers/actions/workflows/ci.yml/badge.svg)](https://github.com/webrek/tokenizers/actions/workflows/ci.yml)
[![docs](https://img.shields.io/badge/docs-webrek.github.io-blue)](https://webrek.github.io/tokenizers/)
![version](https://img.shields.io/badge/version-0.1.0-blue)
![php](https://img.shields.io/badge/php-8.3%20%7C%208.4-777bb4)
![thread safety](https://img.shields.io/badge/ZTS-supported-success)
![license](https://img.shields.io/badge/license-Apache--2.0-green)

Piénsalo como una **báscula para texto de IA**: antes de enviar un prompt a un LLM,
lo pesas en tokens para saber cuánto va a **costar** y si **cabe** en la ventana de
contexto del modelo — todo desde PHP, exacto, y sin reconstruir un vocabulario de
26 MB en cada petición.

```php
use Tokenizers\Encoding;

$enc = Encoding::load('cl100k_base');                 // encoding clase GPT-4 / GPT-4o
echo $enc->countTokens('Hello, world! 🎉');           // 7
```

---

## ¿Por qué esta extensión?

| Propiedad | Port de tiktoken en PHP puro | **Esta extensión** |
|---|---|---|
| Memoria por worker | ~26 MB de vocab reconstruidos **en cada petición** | Cargado **una sola vez** por proceso worker |
| Latencia peor caso | O(n²) por pieza de pre-token | Merge **O(n log n)** basado en heap |
| Instalación | PHP puro | Un solo `.so` — **sin Rust**, sin `ffi.enable` |
| Exactitud | Aproximada | **Byte-exacta** vs la referencia (tiktoken / BERT / T5) |
| Modelos | Solo OpenAI | OpenAI **+ BERT + T5 + Llama/Mistral/Qwen… + Claude/Gemini vía API** |

Las ventajas son **memoria, latencia peor caso, exactitud e instalabilidad** — no el
throughput bruto en entradas diminutas. Para prompts que caben en un tweet, un port
en PHP puro puede ser más rápido (sin overhead de llamada a la extensión). Esta
extensión es para cargas donde el overhead de 26 MB por worker, las entradas
adversarias, o la exactitud byte-a-byte sí importan.

## Modelos soportados

### Localmente, byte-exacto

| Algoritmo | Modelos | Cómo se carga |
|---|---|---|
| **BPE** (tiktoken) | GPT-4, GPT-4o (texto), o1, o3 | `Encoding::load('cl100k_base')` |
| **BPE** (tiktoken) | GPT-4o (multimodal), o1 mini/pro | `Encoding::load('o200k_base')` |
| **BPE** (HuggingFace) | GPT-2, RoBERTa, Llama 3, Mistral (tekken), Qwen, DeepSeek | `Encoding::fromHuggingFace('tokenizer.json')` |
| **WordPiece** | Familia BERT (bert-base-uncased, …) | `Encoding::fromHuggingFace('tokenizer.json')` |
| **Unigram** | T5, ALBERT (SentencePiece) | `Encoding::fromHuggingFace('tokenizer.json')` |

La conformance está verificada byte por byte contra el `tiktoken` de Python, el
`BertTokenizerFast` de HuggingFace y `t5-small`. Cualquier diferencia contra los
fixtures commiteados rompe CI. Ver [Estado y conformance](docs/es/status.md).

### Remotamente (sin tokenizer público)

**Claude 3+** y **Gemini** no publican sus tokenizers — no hay vocabulario local que
cargar. El companion en PHP puro (`Tokenizers\Remote\Anthropic`,
`Tokenizers\Remote\Gemini`, `Tokenizers\TokenCounter`) cuenta sus tokens mediante los
endpoints oficiales `count_tokens` de los proveedores. Funciona **sin** compilar la
extensión en C. Ver la [guía de proveedores remotos](docs/es/guides/remote-providers.md).

## Instalación

```bash
phpize && ./configure && make && make install
```

Luego habilítala en tu `php.ini`:

```ini
extension=tokenizers
```

Verifica:

```bash
php -m | grep tokenizers          # → tokenizers
```

`pecl install tokenizers` y `pie install webrek/tokenizers` también están soportados.
Las instrucciones completas, los requisitos (`libpcre2`) y el troubleshooting están en
**[Primeros pasos](docs/es/getting-started.md)**.

## Inicio rápido

```php
use Tokenizers\Encoding;

// Encoding de OpenAI (el vocab se descarga + cachea en el primer uso)
$enc = Encoding::load('cl100k_base');
$n   = $enc->countTokens($prompt);     // cuenta sin asignar el array de ids
$ids = $enc->encode($prompt);          // int[]
$str = $enc->decode($ids);             // round-trip exacto para texto plano

// Modelo HuggingFace — devuelve Bpe | WordPiece | Unigram según el tipo de modelo
$bert = Encoding::fromHuggingFace('/path/to/bert/tokenizer.json');
$t5   = Encoding::fromHuggingFace('/path/to/t5/tokenizer.json');

// Un solo facade para local + remoto, ruteado por nombre de modelo
use Tokenizers\TokenCounter;
$tc = new TokenCounter();
$tc->count('cl100k_base',     $text);  // BPE local, sin clave
$tc->count('claude-opus-4-8', $text);  // Anthropic remoto (necesita ANTHROPIC_API_KEY)
$tc->count('gemini-1.5-flash',$text);  // Gemini remoto   (necesita GEMINI_API_KEY)
```

## Documentación

| Guía | Qué cubre |
|---|---|
| **[Primeros pasos](docs/es/getting-started.md)** | Instalar, habilitar, verificar, primera tokenización, troubleshooting |
| **[Cargar modelos](docs/es/guides/loading-models.md)** | BPE de OpenAI / HuggingFace, WordPiece, Unigram, opciones, la cache |
| **[Estimar costos de LLM](docs/es/guides/estimating-costs.md)** | Presupuestar gasto, encajar en la ventana de contexto, medir uso por cliente |
| **[Proveedores remotos (Claude / Gemini)](docs/es/guides/remote-providers.md)** | Contar tokens vía las APIs de los proveedores |
| **[Referencia de la API](docs/es/api-reference.md)** | Cada clase, método y función |
| **[Estado y limitaciones](docs/es/status.md)** | Qué está verificado, resultados de conformance, límites honestos, roadmap |

## Estado del proyecto

`v0.1.0`, temprano pero funcional. Las tres fases planeadas están completas y mergeadas:

- **BPE** (cl100k_base, o200k_base, BPE de HuggingFace) — byte-exacto, merge O(n log n).
- **WordPiece** (BERT) y **Unigram** (T5/SentencePiece) — byte-exacto.
- **Companion de API Claude / Gemini** — PHP puro, standalone.

Los matices honestos están en [Estado y limitaciones](docs/es/status.md) (alcance de
normalización, instalación por PIE aún no verificada de extremo a extremo, el conteo
remoto necesita una llamada de red + clave).

## Licencia

Apache-2.0 — ver [LICENSE](LICENSE). Los archivos de vocabulario de los encodings
integrados se descargan del CDN público de OpenAI en tiempo de ejecución, se verifican
por checksum y **no** se redistribuyen con la extensión.
