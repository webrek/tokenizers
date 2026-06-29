# Proveedores Remotos: Conteo de Tokens para Claude y Gemini

> 🌐 English: [remote-providers.md](../../guides/remote-providers.md)

Esta guía cubre el complemento de PHP puro que cuenta tokens para los modelos Claude y Gemini llamando a sus APIs oficiales. Si buscas tokenización local sin conexión, consulta [loading-models.md](loading-models.md).

---

## 1. Por qué esto es diferente

Los tokenizadores de OpenAI (BPE, WordPiece, Unigram) se publican abiertamente y pueden replicarse exactamente en código nativo — que es lo que hace la extensión C. Claude 3+ y Gemini **no** publican sus tokenizadores. No hay ningún archivo de vocabulario que descargar, ni ningún algoritmo que reimplementar localmente.

Contar tokens para estos modelos requiere una **llamada a la API en vivo** usando una **API key**. El conteo es exacto solo según el tokenizador actual del proveedor; los proveedores pueden actualizar sus tokenizadores sin previo aviso.

Anthropic desaconseja explícitamente el uso de tiktoken u otras aproximaciones BPE para Claude, porque el tokenizador de Claude difiere del de OpenAI. El complemento remoto documentado aquí es la vía oficial y soportada: llama al endpoint `/v1/messages/count_tokens` de Anthropic, que devuelve el conteo real de tokens.

---

## 2. Instalación

El complemento remoto es un conjunto de archivos de PHP puro ubicados en `php/Tokenizers/Remote/`. Funciona **sin la extensión C instalada** porque inicializa automáticamente un polyfill de `\Tokenizers\TokenizerException`.

**Requisitos:**
- PHP 8.3 u 8.4
- `ext-curl` (para llamadas HTTP)
- `ext-json` (para codificación/decodificación JSON)

Sin `anthropic-ai/sdk`, sin Guzzle, sin ninguna otra librería HTTP — solo curl puro, cero dependencias adicionales.

**Con autoload de Composer** (recomendado si usas Composer):

```php
<?php
require __DIR__ . '/vendor/autoload.php';

use Tokenizers\Remote\Anthropic;
use Tokenizers\Remote\Gemini;
use Tokenizers\TokenCounter;
```

**Sin Composer** (requires manuales):

```php
<?php
require_once __DIR__ . '/php/Tokenizers/TokenizerException.php'; // polyfill / excepción base
require_once __DIR__ . '/php/Tokenizers/Remote/Http.php';        // Transport interface
require_once __DIR__ . '/php/Tokenizers/Remote/CurlTransport.php';
require_once __DIR__ . '/php/Tokenizers/Remote/Anthropic.php';
require_once __DIR__ . '/php/Tokenizers/Remote/Gemini.php';
require_once __DIR__ . '/php/Tokenizers/TokenCounter.php';

use Tokenizers\Remote\Anthropic;
use Tokenizers\Remote\Gemini;
use Tokenizers\TokenCounter;
```

---

## 3. API keys

| Proveedor | Variable(s) de entorno | Override del constructor |
|-----------|------------------------|--------------------------|
| Anthropic | `ANTHROPIC_API_KEY` | `new Anthropic(apiKey: '...')` |
| Gemini | `GEMINI_API_KEY`, luego `GOOGLE_API_KEY` | `new Gemini(apiKey: '...')` |

Las keys se resuelven en el **momento de la llamada**, no en el momento de la construcción. Crear un objeto `Anthropic` o `Gemini` sin una key no lanza excepción — la excepción se lanza solo cuando realmente se llama a `countTokens()` y no se encuentra ninguna key. Esto permite instanciar los objetos incondicionalmente y diferir la validación de la key al lugar de la llamada.

El argumento `apiKey:` del constructor siempre tiene precedencia sobre las variables de entorno.

```php
<?php
use Tokenizers\Remote\Anthropic;
use Tokenizers\Remote\Gemini;

// Key desde el entorno
$anthropic = new Anthropic();              // lee ANTHROPIC_API_KEY en el momento de la llamada
$gemini    = new Gemini();                 // lee GEMINI_API_KEY o GOOGLE_API_KEY en el momento de la llamada

// Key pasada explícitamente (sobreescribe la variable de entorno)
$anthropic = new Anthropic(apiKey: 'sk-ant-...');
$gemini    = new Gemini(apiKey: 'AIza...');
```

---

## 4. Conteo de tokens de Claude

```php
<?php
use Tokenizers\Remote\Anthropic;

$client = new Anthropic(); // key desde ANTHROPIC_API_KEY

// Cadena de texto simple — se convierte en un turno de usuario único
$n = $client->countTokens('claude-opus-4-8', 'Hello, world!');
echo $n; // conteo exacto de tokens desde la API

// Array de mensajes + system prompt opcional
$n = $client->countTokens(
    model: 'claude-opus-4-8',
    messages: [
        ['role' => 'user',      'content' => 'What is the capital of France?'],
        ['role' => 'assistant', 'content' => 'The capital of France is Paris.'],
        ['role' => 'user',      'content' => 'And Germany?'],
    ],
    system: 'You are a helpful geography assistant.',
);
echo $n;
```

**Bajo el capó:**

- Endpoint: `POST https://api.anthropic.com/v1/messages/count_tokens`
- Headers: `x-api-key: <key>`, `anthropic-version: 2023-06-01`, `content-type: application/json`
- Body: `{"model": "...", "messages": [...], "system": "..."}` — un argumento `$messages` de tipo cadena se envuelve automáticamente como `[{"role":"user","content":"<text>"}]`
- Parsea `input_tokens` de la respuesta JSON
- Un status HTTP distinto de 2xx o un body de respuesta malformado lanza `\Tokenizers\TokenizerException`

---

## 5. Conteo de tokens de Gemini

```php
<?php
use Tokenizers\Remote\Gemini;

$client = new Gemini(); // key desde GEMINI_API_KEY o GOOGLE_API_KEY

// Nombre corto del modelo
$n = $client->countTokens('gemini-1.5-flash', 'Hello, world!');
echo $n;

// Nombre completo del modelo con prefijo "models/" — ambas formas son aceptadas
$n = $client->countTokens('models/gemini-1.5-flash', 'Hello, world!');
echo $n; // mismo resultado; el prefijo "models/" se normaliza, nunca se duplica
```

**Bajo el capó:**

- Endpoint: `POST https://generativelanguage.googleapis.com/v1beta/models/{model}:countTokens`
  (el segmento `{model}` es siempre el nombre sin `models/`, p. ej. `gemini-1.5-flash`)
- Header: `x-goog-api-key: <key>`
- Body: `{"contents":[{"parts":[{"text":"<text>"}]}]}`
- Parsea `totalTokens` de la respuesta JSON
- Un status HTTP distinto de 2xx o un body de respuesta malformado lanza `\Tokenizers\TokenizerException`

---

## 6. Enrutamiento unificado con `TokenCounter`

`TokenCounter` proporciona un único método `count()` que enruta automáticamente a Anthropic, Gemini, o al encoder BPE local según el nombre del modelo — sin lógica `if/else` en el código de tu aplicación.

```php
<?php
use Tokenizers\TokenCounter;

$tc = new TokenCounter(); // usa los clientes Anthropic y Gemini por defecto (keys desde el entorno)

// Enruta al BPE local (sin red, sin key)
$n = $tc->count('cl100k_base', 'Hello, world!');

// Enruta a Anthropic (requiere ANTHROPIC_API_KEY)
$n = $tc->count('claude-opus-4-8', 'Hello, world!');

// Enruta a Gemini (requiere GEMINI_API_KEY o GOOGLE_API_KEY)
$n = $tc->count('gemini-1.5-flash', 'Hello, world!');
```

**Reglas de enrutamiento** (aplicadas por `TokenCounter::route($model)`, sin llamada a la red):

| Prefijo del modelo | Proveedor retornado |
|--------------------|---------------------|
| `claude*` o `anthropic*` | `'anthropic'` |
| `gemini*` o `models/gemini*` | `'gemini'` |
| Cualquier otro | `'local'` |

```php
// Inspeccionar el enrutamiento sin realizar ninguna llamada
echo TokenCounter::route('claude-opus-4-8');   // 'anthropic'
echo TokenCounter::route('gemini-1.5-flash');  // 'gemini'
echo TokenCounter::route('cl100k_base');        // 'local'
```

**Forzar un proveedor** con el tercer argumento opcional:

```php
// Sobreescribir el enrutamiento automático
$n = $tc->count('my-fine-tuned-model', $text, provider: 'anthropic');
```

Pasar un valor `$provider` desconocido lanza `\Tokenizers\TokenizerException("unknown provider '<p>' for model: <model>")`.

**Inyectar instancias de backend personalizadas** (p. ej. para pasar una API key o un timeout personalizado):

```php
use Tokenizers\Remote\Anthropic;
use Tokenizers\Remote\Gemini;
use Tokenizers\TokenCounter;

$tc = new TokenCounter(
    anthropic: new Anthropic(apiKey: 'sk-ant-...', timeout: 10),
    gemini:    new Gemini(apiKey: 'AIza...'),
);
```

---

## 7. Pruebas sin conexión

La interfaz `Transport` permite inyectar un transport HTTP falso para pruebas unitarias. Esto significa que puedes probar toda la lógica de enrutamiento, parseo y manejo de errores sin realizar llamadas de red reales.

```php
<?php
use Tokenizers\Remote\Anthropic;
use Tokenizers\Remote\Transport;

// Implementar la interfaz con un fake simple
$fake = new class implements Transport {
    public function post(string $url, array $headers, string $body, int $timeout): array
    {
        // Devolver una respuesta de API falsa
        return [
            'status' => 200,
            'body'   => json_encode(['input_tokens' => 42]),
        ];
    }
};

// Inyectar en el cliente
$client = new Anthropic(apiKey: 'test-key', transport: $fake);

$n = $client->countTokens('claude-opus-4-8', 'Hello');
assert($n === 42); // pasa — no se realizó ninguna llamada de red
```

La misma interfaz `Transport` se aplica a `Gemini`. El transport de producción por defecto es `CurlTransport`, al que nunca necesitas hacer referencia directamente a menos que lo estés reemplazando.

---

## 8. Limitaciones honestas

Antes de elegir este complemento, ten en cuenta las siguientes restricciones:

- **Red y key requeridas.** No existe una vía sin conexión para Claude o Gemini. Cada llamada realiza una petición HTTPS a la API del proveedor.
- **Exacto solo en este momento.** El conteo refleja el tokenizador del proveedor en el momento de la llamada. Los proveedores pueden y actualizan sus tokenizadores sin versionar los endpoints.
- **Dependencias mínimas.** Los únicos requisitos de extensión PHP son `ext-curl` y `ext-json`, ambos típicamente incluidos con PHP. Sin paquetes de Composer, sin SDK, sin Guzzle.
- **Si ya usas `anthropic-ai/sdk`.** Ese SDK tiene su propio método de conteo de tokens. Puedes usarlo directamente en lugar de este complemento — ambos llaman al mismo endpoint de Anthropic. Este complemento existe para proporcionar una vía **sin dependencias y con enrutamiento unificado** para proyectos que no quieren incorporar el SDK completo.
- **Normalización del nombre de modelo de Gemini.** El complemento maneja tanto `gemini-1.5-flash` como `models/gemini-1.5-flash` de forma transparente. Siempre pasa el nombre del modelo en la forma en que se encuentre; el complemento lo normaliza antes de construir la URL.

---

## Véase también

- [loading-models.md](loading-models.md) — tokenizadores BPE, WordPiece y Unigram locales (sin API key)
- [estimating-costs.md](estimating-costs.md) — convierte conteos de tokens en estimaciones de coste
- [../api-reference.md](../api-reference.md) — referencia completa de la API para `Anthropic`, `Gemini`, `TokenCounter` y `Transport`
- [../getting-started.md](../getting-started.md) — instalación y configuración inicial
- [../status.md](../status.md) — estado de la versión, conformidad y limitaciones conocidas
