# Carga y uso de tokenizers locales

> 🌐 English: [loading-models.md](../../guides/loading-models.md)

Esta guía cubre todos los tokenizers locales que soporta la extensión `tokenizers`: cómo cargarlos, qué opciones están disponibles y cómo codificar, contar y decodificar tokens — todo sin llamadas de red ni claves de API.

Para el conteo remoto de tokens (Claude, Gemini), consulta [remote-providers.md](remote-providers.md).

---

## 1. Los tres algoritmos

Los tres algoritmos están implementados en C nativo dentro del `.so`/`.dll` y producen resultados byte-exact con respecto a sus implementaciones de referencia:

| Algoritmo | Clase | Referencia | Modelos típicos |
|-----------|-------|-----------|----------------|
| **BPE** (byte-level, compatible con tiktoken) | `\Tokenizers\Bpe` | Python `tiktoken` | GPT-4, GPT-4o, Llama 3, Mistral, Qwen, DeepSeek |
| **WordPiece** | `\Tokenizers\WordPiece` | HuggingFace `BertTokenizerFast` | BERT y su familia |
| **Unigram** (SentencePiece) | `\Tokenizers\Unigram` | HuggingFace `t5-small` | T5, ALBERT |

"Byte-exact" significa que la extensión produce los mismos token IDs que la referencia Python en cada caso de prueba verificado — no es una aproximación. Los fixtures de conformidad están registrados en el repositorio y la CI falla ante cualquier diferencia.

Las tres clases viven en el namespace `\Tokenizers\`. La factory de alto nivel `\Tokenizers\Encoding` selecciona automáticamente la clase correcta según el archivo de tokenizer que se proporcione.

---

## 2. Codificaciones de OpenAI

Usa `Encoding::load()` para cargar una de las dos codificaciones integradas de OpenAI por nombre.

```php
<?php
use Tokenizers\Encoding;
use Tokenizers\TokenizerException;

// Carga cl100k_base — usado por GPT-4, GPT-4o (texto), o1, o3
$enc = Encoding::load('cl100k_base');

// Carga o200k_base — usado por GPT-4o (multimodal), o1 mini, o1 pro
$enc = Encoding::load('o200k_base');

// Lanza TokenizerException("unknown encoding: <name>") para cualquier otro valor
```

**Qué codificación usa cada modelo:**

| Codificación | Modelos |
|----------|--------|
| `cl100k_base` | GPT-4, GPT-4o generación de texto, o1, o3 |
| `o200k_base` | GPT-4o multimodal, o1 mini, o1 pro |

**En el primer uso**, el archivo de vocabulario se descarga desde la CDN pública de OpenAI, se verifica su checksum y se escribe en el directorio de caché local. Los usos posteriores (en el mismo proceso y en procesos futuros) cargan desde la caché — sin descarga. El vocabulario nunca se redistribuye con la propia extensión.

`cl100k_base` tiene un vocabulario de 100.277 tokens:

```php
echo $enc->vocabSize(); // 100277
```

**Codificación, conteo y decodificación:**

```php
use Tokenizers\Encoding;

$enc = Encoding::load('cl100k_base');

// Contar tokens sin producir el array completo
$count = $enc->countTokens('Hello, world!'); // rápido, sin asignación del array de ids

// Codificar a token IDs
$ids = $enc->encode('Hello world'); // [9906, 1917]

// Decodificar token IDs de vuelta a texto
$text = $enc->decode($ids); // 'Hello world'

// Decodificar un único token ID
$piece = $enc->decodeSingle(9906); // 'Hello'
```

---

## 3. Modelos de HuggingFace (`tokenizer.json`)

`Encoding::fromHuggingFace($path)` carga cualquier tokenizer de HuggingFace desde su archivo `tokenizer.json` y devuelve el objeto tipado apropiado — `Bpe`, `WordPiece` o `Unigram` — despachado automáticamente por el campo `model.type` dentro del JSON.

Se apunta el método al `tokenizer.json` que ya se tiene en disco (descargado por separado desde el HuggingFace Hub o incluido con el proyecto).

### BPE — GPT-2, Llama 3, Mistral, Qwen, DeepSeek

```php
<?php
use Tokenizers\Encoding;
use Tokenizers\Bpe;

// Cualquier modelo HF cuyo tokenizer.json tenga model.type == "BPE"
// p. ej. GPT-2, Llama 3, Mistral (tekken), Qwen, DeepSeek
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

## 4. Opciones de WordPiece

Si se dispone de un archivo `vocab.txt` plano (un token por línea, tal como lo distribuye BERT), usa `Encoding::wordPieceFromVocabFile()` en lugar de `fromHuggingFace()`:

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

Todas las claves de `$opts` son opcionales. La tabla completa con sus valores por defecto:

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `unkToken` | `string` | `"[UNK]"` | Token emitido cuando una palabra no puede segmentarse |
| `continuingSubwordPrefix` | `string` | `"##"` | Prefijo añadido a las piezas de subpalabra no iniciales |
| `maxInputCharsPerWord` | `int` | `100` | Las palabras más largas que este valor se mapean directamente a `[UNK]` |
| `lowercase` | `bool` | `true` | Convierte la entrada a minúsculas antes de tokenizar |
| `stripAccents` | `bool` | `true` | Elimina los caracteres de acento de combinación tras convertir a minúsculas |
| `handleChineseChars` | `bool` | `true` | Rodea los puntos de código CJK con espacios para que se tokenicen como caracteres individuales |

**Nota sobre el alcance de la normalización:** El normalizador WordPiece en v0.1 cubre solo Latin-1 y el espaciado CJK — no realiza descomposición NFD completa de Unicode. Los scripts no latinos que requieren descomposición (p. ej. árabe, devanagari) están fuera del alcance de v1. Consulta [../status.md](../status.md) para más detalles.

---

## 5. Opciones de Unigram

`Unigram::fromVocab()` es el constructor de bajo nivel (llamado internamente por `fromHuggingFace`). Al cargar mediante `fromHuggingFace` no es necesario pasar opciones — se leen del JSON. Las opciones están documentadas aquí como referencia:

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `unkId` | `int` | ID of `<unk>` piece if present, else `0` | Token ID emitido cuando ninguna pieza cubre la entrada |
| `addPrefixSpace` | `bool` | `true` | Antepone un Metaspace inicial (`▁`, U+2581) a la entrada antes de segmentar |

**Advertencias de normalización:** La implementación de Unigram gestiona Metaspace y es identity-on-ASCII. Las entradas que requieren normalización NFKC y algunos casos límite de espacios en blanco (espacios iniciales, finales o múltiples consecutivos) pueden diferir de la referencia SentencePiece. Consulta [../status.md](../status.md) para la lista completa de desviaciones conocidas.

---

## 6. Tokens especiales (BPE)

Por defecto, `Bpe::encode()` trata los tokens especiales como errores: si el texto de entrada contiene una cadena como `<|endoftext|>` y no se ha permitido explícitamente, la llamada lanza una `TokenizerException`.

```php
use Tokenizers\Encoding;
use Tokenizers\TokenizerException;

$enc = Encoding::load('cl100k_base');

// Por defecto: desactiva todos los tokens especiales — lanza si alguno aparece en $text
// $enc->encode('<|endoftext|> hello');   // TokenizerException

// Permite tokens especiales específicos
$ids = $enc->encode('<|endoftext|> hello', allowedSpecial: ['<|endoftext|>']);

// Permite todos los tokens especiales
$ids = $enc->encode('<|endoftext|> hello', allowedSpecial: 'all');

// El segundo parámetro es $allowedSpecial; el tercero es $disallowedSpecial
// (por defecto: 'all', es decir, todos los especiales no presentes en $allowedSpecial están desactivados)
$ids = $enc->encode($text, allowedSpecial: [], disallowedSpecial: 'all');
```

`countTokens()` no acepta argumentos de tokens especiales — opera solo sobre texto plano. Usa `encode()` cuando necesites control sobre tokens especiales.

---

## 7. Tokens especiales (BPE) — constructores de bajo nivel

Para casos de uso avanzados (construir un tokenizer BPE personalizado desde cero, o cargar directamente un archivo `.tiktoken`), hay dos constructores estáticos adicionales disponibles en `Bpe`:

```php
use Tokenizers\Bpe;

// Carga desde un archivo en formato .tiktoken (token base64 → id, uno por línea)
$bpe = Bpe::fromTiktokenFile(
    path: '/path/to/vocab.tiktoken',
    pattern: '/(?i:...)/',          // patrón regex de división
    specialTokens: ['<|endoftext|>' => 100257],
);

// Construye desde arrays en memoria
$bpe = Bpe::fromVocab(
    tokenBytesToId: $tokenBytesToId, // array<string, int>: bytes del token → id
    merges: $merges,                 // array<string>: reglas de fusión en orden
    pattern: '/(?i:...)/',
    specialTokens: [],
);
```

Estos constructores son los que `Encoding::load()` y `Encoding::fromHuggingFace()` invocan internamente. Solo son necesarios si se carga un formato de tokenizer que `Encoding` aún no soporta.

---

## 8. La caché de proceso

Los datos del vocabulario se cargan **una sola vez por proceso worker** en una caché en memoria global del proceso. Cada llamada posterior a `Encoding::load()` o `Encoding::fromHuggingFace()` para el mismo archivo devuelve el tokenizer en caché de inmediato, sin volver a analizar ni reasignar el vocabulario. Bajo ZTS (PHP thread-safe), la caché está protegida por un mutex TSRM.

Esta es la principal ventaja de memoria sobre las alternativas PHP puras, que típicamente reconstruyen ~26 MB de datos de vocabulario en cada solicitud.

**Inspeccionando la caché:**

```php
<?php
use Tokenizers\Encoding;

$enc1 = Encoding::load('cl100k_base'); // carga desde disco / CDN, llena la caché
$enc2 = Encoding::load('cl100k_base'); // servido desde la caché

echo tokenizers_cache_count(); // 1

$enc3 = Encoding::load('o200k_base'); // segundo vocabulario cargado
echo tokenizers_cache_count(); // 2
```

**Resolución del directorio de caché** (gana el primero que esté definido):

1. `$TOKENIZERS_CACHE_DIR/tokenizers`
2. `$XDG_CACHE_HOME/tokenizers`
3. `$HOME/.cache/tokenizers`
4. `sys_get_temp_dir()/tokenizers`

Para cambiar la ubicación, establece la variable de entorno `TOKENIZERS_CACHE_DIR` antes de cargar cualquier vocabulario:

```php
putenv('TOKENIZERS_CACHE_DIR=/var/cache/myapp');
// o en la shell: export TOKENIZERS_CACHE_DIR=/var/cache/myapp
```

También se puede inspeccionar la ruta resuelta en tiempo de ejecución:

```php
use Tokenizers\Encoding;

echo Encoding::cacheDir(); // p. ej. /home/user/.cache/tokenizers
```

Los archivos de vocabulario integrados de OpenAI (`cl100k_base`, `o200k_base`) se descargan desde la CDN pública de OpenAI en el primer uso, se verifica su checksum y se almacenan en el directorio de caché. Nunca se incluyen ni redistribuyen con esta extensión.

---

## 9. La API procedimental

Para entornos que prefieren llamadas procedimentales sobre métodos de objetos, la extensión también expone un conjunto de funciones globales. Estas funciones aceptan un objeto `Bpe` como primer argumento y de lo contrario reflejan la API OOP.

```php
<?php
use Tokenizers\Encoding;

$bpe = Encoding::load('cl100k_base');
$text = 'Hola mundo! Tokenizing con una extensión PHP nativa.';

// Contar tokens
$n    = tokenizers_count($bpe, $text);

// Codificar a IDs (con arrays opcionales de tokens especiales permitidos/desactivados)
$ids  = tokenizers_encode($bpe, $text, allowedSpecial: [], disallowedSpecial: 'all');

// Decodificar IDs de vuelta a texto
$out  = tokenizers_decode($bpe, $ids);

// Versión de la extensión
echo tokenizers_version(); // "0.1.0"
// Igual que la constante:
echo \Tokenizers\VERSION;  // "0.1.0"
```

Nota: las funciones procedimentales operan solo sobre `Bpe`. Para `WordPiece` y `Unigram`, usa los métodos OOP en esas clases directamente.

---

## Véase también

- [remote-providers.md](remote-providers.md) — cuenta tokens de Claude y Gemini mediante sus APIs
- [estimating-costs.md](estimating-costs.md) — estima los costos de LLM a partir de los conteos de tokens
- [../api-reference.md](../api-reference.md) — referencia completa de la API para todas las clases y funciones
- [../getting-started.md](../getting-started.md) — instalación y primeros pasos
- [../status.md](../status.md) — resultados de conformidad, limitaciones de normalización y hoja de ruta
