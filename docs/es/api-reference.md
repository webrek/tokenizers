# Referencia de API — extensión `tokenizers` v0.1.0

> 🌐 English: [api-reference.md](../api-reference.md)

> Para guías y tutoriales, consulta las [guías](guides/loading-models.md) y el documento [Primeros pasos](getting-started.md).

---

## Tabla de contenidos

1. [Constantes](#1-constantes)
2. [`\Tokenizers\Bpe`](#2-tokenizersbpe)
   - [Métodos de factoría estáticos](#métodos-de-factoría-estáticos)
   - [Métodos de instancia](#métodos-de-instancia)
3. [`\Tokenizers\WordPiece`](#3-tokenizerswordpiece)
   - [Método de factoría estático](#static-factory-method-wordpiece)
   - [Métodos de instancia](#instance-methods-wordpiece)
   - [Referencia de `$opts`](#opts-reference-wordpiece)
4. [`\Tokenizers\Unigram`](#4-tokenizersunigram)
   - [Método de factoría estático](#static-factory-method-unigram)
   - [Métodos de instancia](#instance-methods-unigram)
   - [Referencia de `$opts`](#opts-reference-unigram)
5. [`\Tokenizers\Encoding`](#5-tokenizersencoding)
   - [Resolución del directorio de caché](#resolución-del-directorio-de-caché)
6. [Funciones procedurales](#6-funciones-procedurales)
7. [Companion remoto — `\Tokenizers\Remote`](#7-companion-remoto--tokenizersremote)
   - [Interfaz `Transport`](#interfaz-transport)
   - [`Anthropic`](#anthropic)
   - [`Gemini`](#gemini)
   - [Referencia de variables de entorno](#referencia-de-variables-de-entorno)
8. [`\Tokenizers\TokenCounter`](#8-tokenizerstokencounter)
9. [`\Tokenizers\TokenizerException`](#9-tokenizerstokenizerexception)
10. [Véase también](#véase-también)

---

## 1. Constantes

### `\Tokenizers\VERSION`

```php
const \Tokenizers\VERSION = "0.1.0";
```

La cadena de versión de la extensión. También disponible mediante la función procedural `tokenizers_version()`.

```php
echo \Tokenizers\VERSION; // "0.1.0"
```

---

## 2. `\Tokenizers\Bpe`

Clase nativa en C. Implementa BPE a nivel de bytes (byte-pair encoding), compatible con la biblioteca tiktoken de OpenAI. Apta para `cl100k_base` (GPT-4, o1, o3), `o200k_base` (GPT-4o multimodal, o1 mini/pro) y modelos de pesos abiertos cargados desde un `tokenizer.json` de HuggingFace (GPT-2, RoBERTa, Llama 3, Mistral, Qwen, DeepSeek).

La complejidad de las fusiones es O(n log n) — basada en heap, por lo que entradas adversariales no provocan un crecimiento cuadrático. El vocabulario se carga una sola vez por proceso worker en una caché global del proceso (segura para ZTS mediante un mutex TSRM).

```php
final class Bpe { /* ... */ }
```

### Métodos de factoría estáticos

#### `Bpe::fromTiktokenFile()`

```php
public static function fromTiktokenFile(
    string $path,
    string $pattern,
    array  $specialTokens = []
): Bpe;
```

Carga un vocabulario desde un archivo `.tiktoken` en disco. `$path` es la ruta absoluta al archivo. `$pattern` es la expresión regular de división (p. ej., el patrón de cl100k_base). `$specialTokens` es un mapa de string de token especial a id entero.

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

Construye una instancia de `Bpe` directamente desde datos en memoria. `$tokenBytesToId` mapea secuencias de bytes codificadas en base64 a ids enteros. `$merges` es una lista ordenada de pares de fusión. `$pattern` es la expresión regular de división. `$specialTokens` mapea strings de tokens especiales a ids.

```php
$bpe = \Tokenizers\Bpe::fromVocab($tokenBytesToId, $merges, $pattern);
```

### Métodos de instancia

#### `encode()`

```php
public function encode(
    string       $text,
    array|string $allowedSpecial    = [],
    array|string $disallowedSpecial = "all"
): array; // int[]
```

Codifica `$text` en un array de ids de token enteros.

**Manejo de tokens especiales:** Por defecto, `$disallowedSpecial = "all"` significa que cada token especial del vocabulario se trata como texto plano (es decir, no se codificará como un id de token especial único). Pasa `"all"` a `$allowedSpecial` para permitir que todos los tokens especiales se codifiquen como sus ids, o pasa una lista explícita como `['<|endoftext|>']` para permitir solo esos tokens.

| Parámetro | Tipo | Predeterminado | Descripción |
|---|---|---|---|
| `$text` | `string` | — | Texto de entrada a codificar |
| `$allowedSpecial` | `array\|string` | `[]` | Tokens que pueden codificarse como ids especiales. Pasa `"all"` o una lista. |
| `$disallowedSpecial` | `array\|string` | `"all"` | Tokens que NO deben aparecer; genera un error si se encuentran. |

```php
$ids = $bpe->encode('Hello world');
// [9906, 1917]

// Permite que el marcador de fin de texto pase como id especial:
$ids = $bpe->encode('<|endoftext|>text', allowedSpecial: ['<|endoftext|>']);

// Permite todos los tokens especiales:
$ids = $bpe->encode($text, allowedSpecial: 'all');
```

#### `countTokens()`

```php
public function countTokens(string $text): int;
```

Devuelve el número de tokens a los que se codifica `$text`, sin asignar el array completo de ids. Más rápido que `count($this->encode($text))` para entradas largas.

```php
$n = $bpe->countTokens('Hola mundo!'); // 4
```

#### `decode()`

```php
public function decode(array $ids): string;
```

Decodifica un array de ids de token enteros a una cadena UTF-8.

```php
echo $bpe->decode([9906, 1917]); // "Hello world"
```

#### `decodeSingle()`

```php
public function decodeSingle(int $id): string;
```

Decodifica un único id de token a su representación en bytes. Útil para inspeccionar tokens individuales.

```php
$bytes = $bpe->decodeSingle(9906); // "Hello"
```

#### `vocabSize()`

```php
public function vocabSize(): int;
```

Devuelve el tamaño total del vocabulario (tokens base + tokens especiales). Para `cl100k_base` es `100277`.

```php
echo $bpe->vocabSize(); // 100277 para cl100k_base
```

#### `name()`

```php
public function name(): ?string;
```

Devuelve el nombre de la codificación, o `null`. En v0.1 siempre devuelve `null` — el seguimiento de nombres no está implementado aún.

```php
var_dump($bpe->name()); // NULL
```

---

## 3. `\Tokenizers\WordPiece`

Clase nativa en C. Implementa la tokenización WordPiece por coincidencia greedy más larga (familia BERT). Utiliza el prefijo de continuación `##` para subpalabras y recurre a `[UNK]` para palabras fuera de vocabulario.

**Alcance de normalización (v0.1):** Solo Latin-1 y espaciado CJK. Los scripts no latinos que requieren descomposición NFD Unicode completa están fuera del alcance de v0.1 y pueden producir resultados distintos a los de la referencia.

```php
final class WordPiece { /* ... */ }
```

### Método de factoría estático {#static-factory-method-wordpiece}

#### `WordPiece::fromVocab()`

```php
public static function fromVocab(
    array $tokenToId,
    array $opts = []
): WordPiece;
```

Construye un tokenizer `WordPiece` desde un mapa de vocabulario. `$tokenToId` mapea strings de tokens (p. ej., `"hello"`, `"##ing"`) a sus ids enteros. Consulta la referencia de `$opts` más abajo.

```php
$wp = \Tokenizers\WordPiece::fromVocab($vocab, [
    'lowercase'  => true,
    'unkToken'   => '[UNK]',
]);
```

Para cargar directamente desde un archivo `vocab.txt` de estilo BERT, usa `\Tokenizers\Encoding::wordPieceFromVocabFile()`.

### Métodos de instancia {#instance-methods-wordpiece}

#### `encode()`

```php
public function encode(string $text): array; // int[]
```

Codifica `$text` y devuelve un array de ids de token enteros.

```php
$ids = $wp->encode('unbelievable tokenization'); // [23653, 19204, 3989]
```

#### `countTokens()`

```php
public function countTokens(string $text): int;
```

Devuelve el recuento de tokens sin materializar el array completo de ids.

```php
$n = $wp->countTokens('Hello world');
```

#### `decode()`

```php
public function decode(array $ids): string;
```

Reconstruye el texto a partir de una lista de ids de token. Elimina el prefijo de continuación `##` al rearmar las subpalabras.

```php
echo $wp->decode([23653, 19204, 3989]);
```

#### `vocabSize()`

```php
public function vocabSize(): int;
```

Devuelve el tamaño del vocabulario.

```php
echo $wp->vocabSize();
```

### Referencia de `$opts` {#opts-reference-wordpiece}

Todas las claves son opcionales. Las claves no especificadas usan los valores predeterminados indicados.

| Clave | Tipo | Predeterminado | Descripción |
|---|---|---|---|
| `unkToken` | `string` | `"[UNK]"` | Token usado para palabras fuera de vocabulario |
| `continuingSubwordPrefix` | `string` | `"##"` | Prefijo antepuesto a las subpalabras de continuación |
| `maxInputCharsPerWord` | `int` | `100` | Palabras más largas que este límite de caracteres se mapean a `[UNK]` |
| `lowercase` | `bool` | `true` | Convierte la entrada a minúsculas antes de la tokenización |
| `stripAccents` | `bool` | `true` | Elimina acentos diacríticos (alcance Latin-1) |
| `handleChineseChars` | `bool` | `true` | Rodea los codepoints CJK con espacios antes de la tokenización |

---

## 4. `\Tokenizers\Unigram`

Clase nativa en C. Implementa la tokenización Unigram de SentencePiece (T5, ALBERT). Utiliza un Metaspace (`▁`, U+2581) para codificar espacios iniciales y la decodificación Viterbi de mejor camino sobre puntuaciones de log-probabilidad de pieza (f64).

**Alcance de normalización (v0.1):** Metaspace e identidad en ASCII. Las entradas que requieren normalización NFKC, y algunos casos límite de espacios en blanco (espacios iniciales/finales/múltiples), pueden diferir del tokenizer de referencia.

```php
final class Unigram { /* ... */ }
```

### Método de factoría estático {#static-factory-method-unigram}

#### `Unigram::fromVocab()`

```php
public static function fromVocab(
    array $pieces,
    array $opts = []
): Unigram;
```

Construye un tokenizer `Unigram` a partir de una lista de pares `(piece, score)`. `$pieces` es un array de entradas `[string, float]` tal como se encuentran en un `tokenizer.json` de SentencePiece. Consulta la referencia de `$opts` más abajo.

```php
$ug = \Tokenizers\Unigram::fromVocab($pieces, ['addPrefixSpace' => true]);
```

Para cargar desde un `tokenizer.json` de HuggingFace automáticamente, usa `\Tokenizers\Encoding::fromHuggingFace()`.

### Métodos de instancia {#instance-methods-unigram}

#### `encode()`

```php
public function encode(string $text): array; // int[]
```

Codifica `$text` y devuelve un array de ids de token enteros mediante búsqueda Viterbi de mejor camino.

```php
$ids = $ug->encode('Hello world'); // [8774, 296]
```

#### `countTokens()`

```php
public function countTokens(string $text): int;
```

Devuelve el recuento de tokens sin materializar el array completo de ids.

```php
$n = $ug->countTokens('Hello world');
```

#### `decode()`

```php
public function decode(array $ids): string;
```

Reconstruye el texto a partir de una lista de ids de token, reemplazando los caracteres `▁` iniciales por espacios.

```php
echo $ug->decode([8774, 296]); // "Hello world"
```

#### `vocabSize()`

```php
public function vocabSize(): int;
```

Devuelve el tamaño del vocabulario.

```php
echo $ug->vocabSize();
```

### Referencia de `$opts` {#opts-reference-unigram}

Todas las claves son opcionales. Las claves no especificadas usan los valores predeterminados indicados.

| Clave | Tipo | Predeterminado | Descripción |
|---|---|---|---|
| `unkId` | `int` | id de `<unk>` si está presente, si no `0` | Id de token a emitir para piezas desconocidas |
| `addPrefixSpace` | `bool` | `true` | Antepone un Metaspace (`▁`) a la entrada antes de codificar |

---

## 5. `\Tokenizers\Encoding`

Shim en PHP puro (`php/Tokenizers/Encoding.php`). Proporciona cargadores de alto nivel que descargan, verifican el checksum y cachean los archivos de vocabulario, y devuelven la instancia de tokenizer nativo apropiada. También despacha los archivos JSON de tokenizer de HuggingFace a la clase C correcta.

```php
final class Encoding { /* ... */ }
```

#### `Encoding::load()`

```php
public static function load(string $name): Bpe;
```

Carga una codificación integrada por nombre. Descarga y verifica el checksum del vocabulario en el primer uso; las llamadas posteriores devuelven desde la caché global del proceso. Codificaciones conocidas actualmente: `'cl100k_base'` y `'o200k_base'`.

Lanza `\Tokenizers\TokenizerException("unknown encoding: <name>")` para nombres no reconocidos.

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

Lee un archivo `tokenizer.json` de HuggingFace y lo despacha automáticamente según el campo `model.type`:

| Valor de `model.type` | Clase devuelta |
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

Carga un archivo `vocab.txt` de estilo BERT (un token por línea, número de línea = id) en un tokenizer `WordPiece`. `$opts` acepta las mismas claves que [`WordPiece::fromVocab()`](#opts-reference-wordpiece).

```php
$wp = Encoding::wordPieceFromVocabFile('/path/to/vocab.txt', ['lowercase' => true]);
```

#### `Encoding::cacheDir()`

```php
public static function cacheDir(): string;
```

Devuelve la ruta resuelta al directorio donde se almacenan los archivos de vocabulario descargados. Útil para depurar la ubicación de la caché.

```php
echo Encoding::cacheDir();
// p. ej. /home/user/.cache/tokenizers
```

#### `Encoding::download()`

```php
public static function download(string $url, ?string $sha256, string $dest): void;
```

Utilidad de bajo nivel. Descarga `$url` en `$dest`, verificando opcionalmente el archivo contra `$sha256`. Se usa internamente por `Encoding::load()`; en general, no necesitas llamarla directamente.

```php
Encoding::download(
    'https://example.com/vocab.tiktoken',
    'abc123...',
    '/tmp/vocab.tiktoken'
);
```

### Resolución del directorio de caché

`Encoding::load()` almacena los archivos de vocabulario descargados en el primer directorio que se resuelva de la siguiente lista ordenada:

1. `$TOKENIZERS_CACHE_DIR/tokenizers`
2. `$XDG_CACHE_HOME/tokenizers`
3. `$HOME/.cache/tokenizers`
4. `sys_get_temp_dir()/tokenizers`

Los archivos de vocabulario integrados se descargan desde el CDN público de OpenAI en el primer uso, se verifica su checksum y nunca se redistribuyen con la extensión.

---

## 6. Funciones procedurales

Funciones procedurales en el espacio de nombres global. Todas las funciones que operan sobre un tokenizer aceptan únicamente una instancia de `\Tokenizers\Bpe` — `WordPiece` y `Unigram` no son aceptadas por estas funciones.

#### `tokenizers_version()`

```php
function tokenizers_version(): string;
```

Devuelve la cadena de versión de la extensión `"0.1.0"`. Equivalente a `\Tokenizers\VERSION`.

```php
echo tokenizers_version(); // "0.1.0"
```

#### `tokenizers_cache_count()`

```php
function tokenizers_cache_count(): int;
```

Devuelve el número de modelos de tokenizer actualmente almacenados en la caché de vocabulario global del proceso. Útil para diagnóstico.

```php
echo tokenizers_cache_count(); // p. ej. 1 tras cargar cl100k_base
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

Envoltura procedural de `Bpe::encode()`. Consulta [`encode()`](#encode) para la semántica de `$allowedSpecial` y `$disallowedSpecial`.

```php
$ids = tokenizers_encode($bpe, 'Hello world'); // [9906, 1917]
```

#### `tokenizers_decode()`

```php
function tokenizers_decode(\Tokenizers\Bpe $t, array $ids): string;
```

Envoltura procedural de `Bpe::decode()`.

```php
echo tokenizers_decode($bpe, [9906, 1917]); // "Hello world"
```

#### `tokenizers_count()`

```php
function tokenizers_count(\Tokenizers\Bpe $t, string $text): int;
```

Envoltura procedural de `Bpe::countTokens()`.

```php
$n = tokenizers_count($bpe, 'Hello world'); // 2
```

---

## 7. Companion remoto — `\Tokenizers\Remote`

Clases en PHP puro que cuentan tokens mediante las APIs oficiales del proveedor. Funcionan **sin** la extensión C cargada (arrancan un polyfill de `TokenizerException` mediante `require_once` de `php/Tokenizers/TokenizerException.php`). Requieren `ext-curl` y `ext-json`. Usan curl directamente — no dependen de `anthropic-ai/sdk`, Guzzle ni de ninguna otra librería HTTP.

**Nota:** Los modelos Claude 3+ y Gemini no tienen tokenizer local. Los recuentos exactos de tokens para esos modelos requieren una llamada de red y una API key válida. Los proveedores pueden cambiar su tokenizer en cualquier momento.

### Interfaz `Transport`

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

La abstracción HTTP utilizada por `Anthropic` y `Gemini`. La implementación predeterminada es `CurlTransport`. Inyecta una implementación personalizada para pruebas sin conexión.

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

Cuenta tokens para un modelo Anthropic (Claude) mediante la API oficial.

**Detalles de `countTokens()`:**

- **Endpoint:** `POST https://api.anthropic.com/v1/messages/count_tokens`
- **Cabeceras:** `x-api-key: <key>`, `anthropic-version: 2023-06-01`, `content-type: application/json`
- **Cuerpo:** `{"model": <model>, "messages": [...], "system": <system?>}`
  - Un `string` simple en `$messages` se convierte en un turno único `{"role":"user","content": <text>}`.
  - Un `array` se envía tal cual (para conversaciones de múltiples turnos).
- **Campo de respuesta analizado:** `input_tokens`
- **Resolución de API key:** argumento `$apiKey` del constructor; si no, variable de entorno `ANTHROPIC_API_KEY`. Una key ausente lanza `TokenizerException` en el momento de la llamada.
- **Errores:** Un estado HTTP no 2xx o un campo `input_tokens` malformado o ausente lanza `TokenizerException`.

```php
use Tokenizers\Remote\Anthropic;

$anthropic = new Anthropic(); // lee ANTHROPIC_API_KEY del entorno
$n = $anthropic->countTokens('claude-opus-4-8', 'Hello, world!');

// Múltiples turnos:
$n = $anthropic->countTokens('claude-opus-4-8', [
    ['role' => 'user',      'content' => 'Hi'],
    ['role' => 'assistant', 'content' => 'Hello!'],
    ['role' => 'user',      'content' => 'How are you?'],
]);

// Con un system prompt:
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

Cuenta tokens para un modelo Google Gemini mediante la API oficial.

**Detalles de `countTokens()`:**

- **Endpoint:** `POST https://generativelanguage.googleapis.com/v1beta/models/{model}:countTokens`
  El segmento `{model}` se normaliza — se aceptan tanto `"gemini-1.5-flash"` como `"models/gemini-1.5-flash"`; el prefijo `models/` se añade o preserva exactamente una vez, nunca con doble prefijo.
- **Cabecera:** `x-goog-api-key: <key>`
- **Cuerpo:** `{"contents":[{"parts":[{"text": <text>}]}]}`
- **Campo de respuesta analizado:** `totalTokens`
- **Resolución de API key:** argumento `$apiKey` del constructor; luego variable de entorno `GEMINI_API_KEY`; luego `GOOGLE_API_KEY`. Una key ausente lanza `TokenizerException` en el momento de la llamada.
- **Errores:** Un estado HTTP no 2xx o un campo `totalTokens` malformado o ausente lanza `TokenizerException`.

```php
use Tokenizers\Remote\Gemini;

$gemini = new Gemini(); // lee GEMINI_API_KEY o GOOGLE_API_KEY del entorno
$n = $gemini->countTokens('gemini-1.5-flash', 'Hello, world!');
// Ambas formas son equivalentes:
$n = $gemini->countTokens('models/gemini-1.5-flash', 'Hello, world!');
```

### Referencia de variables de entorno

| Proveedor | Variable(s) de entorno | Sobrescritura en constructor |
|---|---|---|
| Anthropic | `ANTHROPIC_API_KEY` | `new Anthropic(apiKey: '...')` |
| Gemini | `GEMINI_API_KEY` (se comprueba primero), luego `GOOGLE_API_KEY` | `new Gemini(apiKey: '...')` |

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

Fachada de alto nivel que despacha el recuento de tokens al backend correcto según el nombre del modelo. Definida en `php/Tokenizers/TokenCounter.php`.

**`route()` — reglas de enrutamiento (sin llamada de red):**

| Prefijo de modelo | Devuelve |
|---|---|
| `claude` o `anthropic` | `'anthropic'` |
| `gemini` o `models/gemini` | `'gemini'` |
| cualquier otro valor | `'local'` |

**`count()` — lógica de despacho:**

- `'anthropic'` → llama a `Anthropic->countTokens($model, $text)`
- `'gemini'` → llama a `Gemini->countTokens($model, $text)`
- `'local'` → llama a `Encoding::load($model)->countTokens($text)`
- Pasar un `$provider` explícito que no sea uno de los tres valores reconocidos lanza `TokenizerException("unknown provider '<p>' for model: <model>")`.

```php
use Tokenizers\TokenCounter;

$tc = new TokenCounter();

// Local (sin red, sin key):
$n = $tc->count('cl100k_base', $text);

// Anthropic remoto (requiere ANTHROPIC_API_KEY):
$n = $tc->count('claude-opus-4-8', $text);

// Gemini remoto (requiere GEMINI_API_KEY o GOOGLE_API_KEY):
$n = $tc->count('gemini-1.5-flash', $text);

// Inyecta clientes preconfigurados:
$tc = new TokenCounter(
    anthropic: new \Tokenizers\Remote\Anthropic(apiKey: 'ant-...'),
    gemini:    new \Tokenizers\Remote\Gemini(apiKey: 'AIza...')
);

// Fuerza un proveedor específico:
$n = $tc->count('my-model', $text, provider: 'local');

// Inspecciona el enrutamiento sin contar:
echo TokenCounter::route('claude-sonnet-4'); // 'anthropic'
echo TokenCounter::route('gemini-pro');      // 'gemini'
echo TokenCounter::route('cl100k_base');     // 'local'
```

---

## 9. `\Tokenizers\TokenizerException`

```php
class TokenizerException extends \RuntimeException {}
```

Lanzada por todas las clases de esta extensión ante condiciones de error. Extiende el `\RuntimeException` estándar, por lo que puede capturarse como `\Tokenizers\TokenizerException` o como `\RuntimeException`.

**Se lanza en las siguientes situaciones:**

- `Encoding::load()` — nombre de codificación desconocido: `"unknown encoding: <name>"`
- `Anthropic::countTokens()` / `Gemini::countTokens()` — API key ausente (en el momento de la llamada)
- `Anthropic::countTokens()` / `Gemini::countTokens()` — respuesta HTTP no 2xx
- `Anthropic::countTokens()` / `Gemini::countTokens()` — campo de respuesta malformado o ausente
- `TokenCounter::count()` — `$provider` explícito desconocido: `"unknown provider '<p>' for model: <model>"`

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

## Véase también

- [Primeros pasos](getting-started.md) — instalación, activación de la extensión, primera tokenización
- [Estado y limitaciones](status.md) — resultados de conformidad, limitaciones conocidas, hoja de ruta
- [Guía: Estimación de costes](guides/estimating-costs.md) — presupuestar los costes de la API LLM antes de llamar
- [Guía: Carga de modelos](guides/loading-models.md) — cargar BPE de OpenAI/HF, WordPiece, Unigram y la caché
- [Guía: Proveedores remotos](guides/remote-providers.md) — companion de API Claude/Gemini, configuración de keys, límites reales
