# Primeros pasos con `tokenizers`

> 🌐 English: [getting-started.md](../getting-started.md)

Una extensión nativa de PHP que cuenta, codifica y decodifica tokens de LLM — exactitud a nivel de byte con los tokenizadores de referencia — más un acompañante en PHP puro que cuenta tokens de Claude/Gemini a través de sus APIs oficiales.

---

## Requisitos

| Requisito | Detalles |
|---|---|
| PHP | 8.3 u 8.4, NTS o ZTS |
| Dep. de compilación | `libpcre2-dev` (Debian/Ubuntu) / `brew install pcre2` (macOS); `pcre2-config` debe estar en el `PATH` |
| Dep. en tiempo de ejecución | `libpcre2-8` |
| Extensión PHP | `ext-json` (incluida con PHP) |
| Solo para el acompañante remoto | `ext-curl` |

Sin toolchain de Rust. Sin `ffi.enable`. Solo una extensión PECL estándar en C.

---

## Instalación desde el código fuente (recomendada / verificada)

Este es el camino verificado y funcional. Úsalo a menos que tengas una razón específica para preferir PECL o PIE.

```bash
git clone https://github.com/webrek/tokenizers.git
cd tokenizers

phpize
./configure
make
make install
```

Luego habilita la extensión en tu `php.ini`:

```ini
extension=tokenizers
```

Para encontrar qué archivo `php.ini` está activo:

```bash
php --ini
```

Busca la línea "Loaded Configuration File". Añade `extension=tokenizers` ahí, o coloca un archivo como `tokenizers.ini` en el directorio `conf.d` que aparece bajo "Scan for additional .ini files in".

---

## Verificar la instalación

```bash
php -m | grep tokenizers
```

Deberías ver `tokenizers` en la salida. Para verificar la versión:

```bash
php -r 'echo extension_loaded("tokenizers") ? \Tokenizers\VERSION : "not loaded";'
```

Salida esperada: `0.1.0`

---

## Instalación mediante PECL

El paquete fuente se adjunta a cada release de GitHub. Hasta que la extensión
se publique en `pecl.php.net`, instálala directamente desde el tarball del
release:

```bash
pecl install https://github.com/webrek/tokenizers/releases/download/v0.1.0/tokenizers-0.1.0.tgz
```

Una vez publicada en el canal PECL, también funcionará la forma corta:

```bash
pecl install tokenizers
```

`pecl install` añade `extension=tokenizers` a tu `php.ini` automáticamente.

---

## Instalación mediante PIE

PIE instala por nombre de paquete de Composer, no por el nombre directo de la extensión:

```bash
pie install webrek/tokenizers
```

Para la versión en desarrollo/no publicada:

```bash
pie install webrek/tokenizers:*@dev
```

PIE lee el bloque `php-ext` en `composer.json` y ejecuta `phpize` / `configure` / `make` / `make install` por ti.

**Importante:** La instalación PIE de extremo a extremo aún no ha sido verificada en una máquina limpia — la herramienta `pie` no estaba disponible en el entorno de desarrollo. El manifiesto está listo, pero si encuentras problemas, vuelve al camino "Instalación desde el código fuente" descrito arriba, que es el método verificado.

---

## Tu primera tokenización

```php
<?php
require_once __DIR__ . '/php/Tokenizers/Encoding.php';

use Tokenizers\Encoding;

// Carga la codificación cl100k_base (usada por GPT-4, GPT-4o text, o1, o3).
// En el primer uso, el archivo de vocabulario se descarga desde la CDN de OpenAI,
// se verifica el checksum y se cachea para solicitudes futuras.
$enc = Encoding::load('cl100k_base');

// Cuenta tokens sin asignar el array de tokens.
$n = $enc->countTokens('Hello, world!');
echo "Token count: $n\n";

// Codifica a un array de IDs de token enteros.
$ids = $enc->encode('Hello world');
var_dump($ids); // array(2) { [0]=> int(9906) [1]=> int(1917) }

// Decodifica de vuelta a texto (el round-trip es exacto).
$text = $enc->decode($ids);
echo $text . "\n"; // Hello world
```

Codificaciones integradas: `cl100k_base` (clase GPT-4) y `o200k_base` (GPT-4o multimodal, o1 mini/pro). Para cargar cualquier otro modelo, usa `Encoding::fromHuggingFace()` — consulta [guides/loading-models.md](guides/loading-models.md).

El archivo de vocabulario se descarga una vez por máquina y se cachea. Consulta [Solución de problemas](#solución-de-problemas) si estás en un entorno con acceso restringido a la red.

---

## Uso sin la extensión C (solo remoto)

Las clases bajo `php/Tokenizers/Remote/` son PHP puro y funcionan sin el `.so` cargado. Solo requieren `ext-curl` y `ext-json`.

```php
<?php
require_once __DIR__ . '/php/Tokenizers/TokenizerException.php'; // polyfill
require_once __DIR__ . '/php/Tokenizers/Remote/Http.php';        // Interfaz de transporte
require_once __DIR__ . '/php/Tokenizers/Remote/CurlTransport.php';
require_once __DIR__ . '/php/Tokenizers/Remote/Anthropic.php';

use Tokenizers\Remote\Anthropic;

// Lee ANTHROPIC_API_KEY del entorno.
$n = (new Anthropic())->countTokens('claude-opus-4-8', 'Hello, world!');
echo "Token count: $n\n";
```

Para Gemini, reemplaza `Anthropic` con `Gemini` (requiere `GEMINI_API_KEY` o `GOOGLE_API_KEY`).

La fachada `TokenCounter` puede enrutar automáticamente por nombre de modelo:

```php
use Tokenizers\TokenCounter;
$tc = new TokenCounter();
$tc->count('cl100k_base', $text);      // local, sin red
$tc->count('claude-opus-4-8', $text);  // remoto Anthropic
$tc->count('gemini-1.5-flash', $text); // remoto Gemini
```

Consulta [guides/remote-providers.md](guides/remote-providers.md) para detalles completos de configuración, configuración de claves y limitaciones honestas.

---

## Solución de problemas

| Síntoma | Causa probable | Solución |
|---|---|---|
| `php -m` no lista `tokenizers` | `extension=tokenizers` no añadido, `php.ini` incorrecto, o ini de SAPI incorrecto (CLI vs FPM) | Ejecuta `php --ini` y confirma que editaste el archivo correcto; FPM/Apache usan un ini separado |
| La compilación falla con "pcre2 not found" o "pcre2-config: command not found" | `libpcre2-dev` no instalado, o `pcre2-config` no está en el `PATH` | macOS: `brew install pcre2`; Debian/Ubuntu: `apt-get install libpcre2-dev` |
| `TokenizerException: unknown encoding: <name>` | Solo `cl100k_base` y `o200k_base` están integradas | Usa `Encoding::fromHuggingFace($path)` con un `tokenizer.json` de HuggingFace para otros modelos |
| Error de red en el primer `Encoding::load()` | Descarga del vocabulario bloqueada por firewall o proxy | Define `TOKENIZERS_CACHE_DIR` como un directorio con permisos de escritura y coloca el archivo de vocabulario previamente; o ejecuta en un entorno con acceso a la red primero |

Orden de resolución del directorio de caché (primera coincidencia gana):
1. `$TOKENIZERS_CACHE_DIR/tokenizers`
2. `$XDG_CACHE_HOME/tokenizers`
3. `$HOME/.cache/tokenizers`
4. `sys_get_temp_dir()/tokenizers`

---

## Siguientes pasos

- [api-reference.md](api-reference.md) — API completa de todas las clases y funciones
- [guides/loading-models.md](guides/loading-models.md) — cargar modelos OpenAI, HuggingFace BPE, WordPiece y Unigram
- [guides/estimating-costs.md](guides/estimating-costs.md) — estimar y presupuestar costos de API de LLM antes de llamar
- [guides/remote-providers.md](guides/remote-providers.md) — acompañante Claude/Gemini: configuración, claves, uso y limitaciones
- [status.md](status.md) — estado de la versión, resultados de conformidad, limitaciones y hoja de ruta
