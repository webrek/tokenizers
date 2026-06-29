# Estimación y gestión del presupuesto de costes LLM

> 🌐 English: [estimating-costs.md](../../guides/estimating-costs.md)

Se paga por token. Eso significa que cada prompt que envías a un modelo tiene un
coste calculable antes de realizar la llamada a la API. Contar tokens localmente,
en PHP, antes de llamar al modelo permite:

- **Presupuestar con precisión.** Conocer el coste de un batch de solicitudes
  antes de comprometerse.
- **Aplicar límites de contexto.** Todo modelo tiene un context window máximo;
  un prompt que lo supera falla. Detectarlo localmente, no en el proveedor.
- **Enrutar de forma inteligente.** Un prompt de 200 tokens puede ser adecuado
  para un modelo costoso; uno de 10 000 tokens puede justificar un modelo más
  económico o una entrada más pequeña.

Esta guía recorre un escenario realista: una agencia de redacción con IA que
genera variantes de copy para múltiples clientes y funcionalidades, y necesita
hacer seguimiento del gasto por cliente antes de que llegue la factura mensual.

---

## 1. Por qué contar tokens antes de llamar

La mayoría de los proveedores LLM cobran por token, dividido entre entrada
(prompt) y salida (completion). Un prompt extenso —un system message largo, un
documento del usuario, un conjunto de ejemplos few-shot— puede costar
silenciosamente más que la propia salida del modelo.

Contar tokens localmente no cuesta nada y toma microsegundos. La alternativa
—enviar la solicitud y revisar la factura— cuesta dinero y tiempo. Adelanta los
cálculos.

Tres razones prácticas para contar antes de llamar:

1. **Presupuesto.** «Esta funcionalidad procesará 50 000 prompts hoy» se
   convierte en una estimación real en dólares en lugar de una suposición.
2. **Control del context window.** Si `countTokens($text) > $maxContextTokens`,
   puedes truncar, resumir o rechazar la entrada antes de que falle en el
   proveedor con un error críptico.
3. **Enrutamiento de modelos.** El recuento de tokens es una de las señales más
   baratas para enrutar: prompts cortos a un modelo rápido/económico, los largos
   a uno con un context window mayor.

---

## 2. Estimar el coste de un prompt

Carga un `Encoding`, cuenta los tokens de tu texto de prompt y multiplica por el
precio de entrada del modelo. Los precios varían según el modelo y cambian con el
tiempo — **confirma siempre el precio actual en la página oficial de precios del
proveedor.** Los números a continuación son meramente ilustrativos.

```php
<?php

require_once __DIR__ . '/vendor/autoload.php';

use Tokenizers\Encoding;

// Carga la codificación cl100k_base (usada por GPT-4, o1, o3 y modelos similares).
// El archivo de vocabulario se descarga en el primer uso y se cachea localmente;
// las llamadas posteriores son casi instantáneas.
$enc = Encoding::load('cl100k_base');

/**
 * Estima el coste de entrada de un texto.
 *
 * @param  \Tokenizers\Bpe $enc            Una instancia de Encoding cargada.
 * @param  string          $text           El texto del prompt a medir.
 * @param  float           $pricePerMillion Precio por millón de tokens de ENTRADA
 *                                          en USD — confirma con tu proveedor.
 * @return array{tokens: int, cost: float}
 */
function estimateCost(\Tokenizers\Bpe $enc, string $text, float $pricePerMillion): array
{
    $tokens = $enc->countTokens($text);
    $cost   = ($tokens / 1_000_000) * $pricePerMillion;

    return ['tokens' => $tokens, 'cost' => $cost];
}

// -------------------------------------------------------------------------
// Ejemplo: un system message + documento del usuario para un modelo de clase GPT-4.
// Reemplaza $inputPricePerMillion con la cifra actual de tu proveedor.
// -------------------------------------------------------------------------
$inputPricePerMillion = 5.00; // ILUSTRATIVO — verifica en la página de precios de tu proveedor

$systemPrompt = 'You are a professional copywriter. Write in a clear, engaging style.';
$userDocument = file_get_contents('/path/to/client/brief.txt');
$fullPrompt   = $systemPrompt . "\n\n" . $userDocument;

$estimate = estimateCost($enc, $fullPrompt, $inputPricePerMillion);

printf(
    "Prompt: %d tokens  →  coste de entrada estimado: $%.6f\n",
    $estimate['tokens'],
    $estimate['cost']
);
// ej. "Prompt: 1423 tokens  →  coste de entrada estimado: $0.007115"
```

**Fórmula:** `cost = (tokenCount / 1_000_000) × pricePerMillion`

Esto proporciona el coste estimado de *entrada*. Los tokens de salida (completion)
suelen tener un precio separado y a una tarifa diferente; es necesario estimar o
limitar la longitud de salida por separado.

> **Nota sobre `countTokens` frente a `encode`:** `countTokens` devuelve
> únicamente un entero; nunca asigna el array completo de IDs de tokens. Para la
> estimación de costes, usar siempre `countTokens` — es notablemente más rápido
> en textos largos.

---

## 3. Presupuestar un batch

La agencia necesita generar 100 variantes de copy a partir de un conjunto de
briefs. Antes de enviar una sola solicitud a la API, suma los recuentos de tokens
de todas las entradas.

```php
<?php

require_once __DIR__ . '/vendor/autoload.php';

use Tokenizers\Encoding;

$enc = Encoding::load('cl100k_base');

// Precios ILUSTRATIVOS — reemplaza con las cifras actuales de tu proveedor.
$inputPricePerMillion  = 5.00;
$outputPricePerMillion = 15.00;
$estimatedOutputTokensPerVariant = 300; // longitud de completion esperada

// Simula 100 textos de brief (en la práctica, cárgalos desde una base de datos o archivos).
$briefs = array_map(
    fn(int $i) => "Write a compelling product description for item #{$i}. "
                . "Focus on benefits, tone: professional, length: ~150 words.",
    range(1, 100)
);

$totalInputTokens = 0;

foreach ($briefs as $brief) {
    // countTokens es preferible a encode() aquí — no se construye ningún array de IDs.
    $totalInputTokens += $enc->countTokens($brief);
}

$totalOutputTokens = count($briefs) * $estimatedOutputTokensPerVariant;

$inputCost  = ($totalInputTokens  / 1_000_000) * $inputPricePerMillion;
$outputCost = ($totalOutputTokens / 1_000_000) * $outputPricePerMillion;
$totalCost  = $inputCost + $outputCost;

printf("Resumen del batch (100 variantes)\n");
printf("  Total tokens de entrada  : %d\n",   $totalInputTokens);
printf("  Est. tokens de salida    : %d\n",   $totalOutputTokens);
printf("  Est. coste de entrada    : $%.4f\n", $inputCost);
printf("  Est. coste de salida     : $%.4f\n", $outputCost);
printf("  Est. coste total         : $%.4f\n", $totalCost);
```

Esto se ejecuta en milisegundos, completamente de forma local. Puedes añadir un
control que aborte o alerte cuando `$totalCost` supere un umbral de presupuesto
por trabajo antes de que se realice la primera llamada a la API.

---

## 4. Mantenerse dentro del context window

Todo modelo especifica un número máximo de tokens que acepta. Superarlo devuelve
un error del proveedor. Dos necesidades comunes: comprobar si un prompt cabe y
truncarlo si no es así.

### Comprobación: ¿cabe este prompt?

```php
<?php

use Tokenizers\Encoding;

$enc = Encoding::load('cl100k_base');

/**
 * Devuelve true si $text cabe dentro de $maxTokens.
 * Úsalo como comprobación previa rápida antes de enviar al modelo.
 */
function fitsInBudget(\Tokenizers\Bpe $enc, string $text, int $maxTokens): bool
{
    return $enc->countTokens($text) <= $maxTokens;
}

$contextWindow = 8_192; // límite de ejemplo — consulta las especificaciones de tu modelo
$document      = file_get_contents('/path/to/document.txt');

if (!fitsInBudget($enc, $document, $contextWindow)) {
    // truncar, dividir en fragmentos o resumir antes de continuar
    echo "Documento demasiado largo — se requiere truncación.\n";
}
```

### Truncar a N tokens

Cuando se necesita acortar un texto para que quepa, se puede codificar, recortar
el array de tokens y luego decodificar de nuevo a una cadena.

```php
<?php

use Tokenizers\Encoding;

$enc = Encoding::load('cl100k_base');

/**
 * Trunca $text a un máximo de $maxTokens tokens y devuelve la cadena decodificada.
 *
 * AVISO IMPORTANTE: el BPE a nivel de bytes puede codificar un solo carácter como
 * múltiples IDs de token, y los límites de carácter no siempre se alinean con los
 * límites de token. Recortar el array de IDs en una posición arbitraria y decodificar
 * puede producir UTF-8 corrupto en el punto de corte — típicamente uno o unos pocos
 * bytes desfasados. Para la mayoría de los casos de uso (mantenerse dentro de un
 * context window) esto es aceptable. Si necesitas un corte UTF-8 limpio, elimina
 * además cualquier carácter de reemplazo final (\u{FFFD}) o usa mb_convert_encoding()
 * para forzar el resultado.
 *
 * @return string El texto decodificado y truncado por tokens. Puede tener un límite impreciso.
 */
function truncateToTokens(\Tokenizers\Bpe $enc, string $text, int $maxTokens): string
{
    $ids = $enc->encode($text);

    if (count($ids) <= $maxTokens) {
        return $text; // ya cabe; omite el viaje de ida y vuelta de decodificación
    }

    $truncatedIds = array_slice($ids, 0, $maxTokens);

    return $enc->decode($truncatedIds);
}

$maxTokens = 4_096;
$rawText   = file_get_contents('/path/to/long-document.txt');
$fitted    = truncateToTokens($enc, $rawText, $maxTokens);

printf("Original: %d tokens → truncado: %d tokens\n",
    $enc->countTokens($rawText),
    $enc->countTokens($fitted)
);
```

> **Cuándo no usar la truncación:** Si el documento tiene una estructura
> significativa (secciones, JSON, código), recortar por tokens en lugar de por
> límites lógicos puede corromper el contenido semánticamente. En esos casos, es
> preferible dividir el texto en fragmentos en delimitadores lógicos y procesar
> los fragmentos individualmente.

---

## 5. Seguimiento del gasto por cliente/funcionalidad

Para una agencia, la imputación de costes por cliente y por funcionalidad es
esencial para la facturación y para detectar qué funcionalidades están consumiendo
presupuesto.

```php
<?php

require_once __DIR__ . '/vendor/autoload.php';

use Tokenizers\Encoding;

$enc = Encoding::load('cl100k_base');

// Precios ILUSTRATIVOS — verifica las cifras actuales con tu proveedor.
const INPUT_PRICE_PER_MILLION  = 5.00;
const OUTPUT_PRICE_PER_MILLION = 15.00;

/**
 * Un acumulador sencillo del gasto en tokens por etiquetas (clientes, funcionalidades, etc.).
 * Es código de aplicación puro — no forma parte de la biblioteca.
 */
class SpendTracker
{
    /** @var array<string, array{input_tokens: int, output_tokens: int}> */
    private array $buckets = [];

    public function record(string $label, int $inputTokens, int $outputTokens = 0): void
    {
        if (!isset($this->buckets[$label])) {
            $this->buckets[$label] = ['input_tokens' => 0, 'output_tokens' => 0];
        }

        $this->buckets[$label]['input_tokens']  += $inputTokens;
        $this->buckets[$label]['output_tokens'] += $outputTokens;
    }

    /**
     * Devuelve un array de informe indexado por etiqueta.
     * Pasa $inputPpm y $outputPpm como tus precios actuales por millón de tokens.
     *
     * @return array<string, array{input_tokens: int, output_tokens: int, est_cost: float}>
     */
    public function report(float $inputPpm, float $outputPpm): array
    {
        $report = [];

        foreach ($this->buckets as $label => $counts) {
            $cost = ($counts['input_tokens']  / 1_000_000) * $inputPpm
                  + ($counts['output_tokens'] / 1_000_000) * $outputPpm;

            $report[$label] = [
                'input_tokens'  => $counts['input_tokens'],
                'output_tokens' => $counts['output_tokens'],
                'est_cost'      => round($cost, 6),
            ];
        }

        return $report;
    }
}

// -------------------------------------------------------------------------
// Simula el procesamiento de solicitudes entre clientes y funcionalidades.
// -------------------------------------------------------------------------
$tracker = new SpendTracker();

$jobs = [
    ['label' => 'client:acme/feature:product-descriptions', 'prompt' => 'Write a product description for a coffee maker.'],
    ['label' => 'client:acme/feature:email-subject-lines',  'prompt' => 'Generate 5 email subject lines for a sale event.'],
    ['label' => 'client:globex/feature:product-descriptions','prompt' => 'Describe the Globex Series 7 industrial pump.'],
    ['label' => 'client:globex/feature:social-posts',        'prompt' => 'Write a LinkedIn post about our Q2 results.'],
];

// Se asumen 200 tokens de salida por solicitud (reemplaza con el completionUsage real).
$estimatedOutputTokens = 200;

foreach ($jobs as $job) {
    $inputTokens = $enc->countTokens($job['prompt']);
    $tracker->record($job['label'], $inputTokens, $estimatedOutputTokens);
}

// Imprimir informe.
$report = $tracker->report(INPUT_PRICE_PER_MILLION, OUTPUT_PRICE_PER_MILLION);

echo "\n=== Informe de gasto ===\n";
foreach ($report as $label => $data) {
    printf(
        "%-50s  in: %4d  out: %4d  est: $%.6f\n",
        $label,
        $data['input_tokens'],
        $data['output_tokens'],
        $data['est_cost']
    );
}
```

En producción, reemplaza el valor fijo `$estimatedOutputTokens` por el valor real
de `usage.completion_tokens` (o equivalente) devuelto en la respuesta de la API,
de modo que el tracker capture números reales tras cada llamada.

---

## 6. Contar tokens para Claude y Gemini

Los modelos Claude 3+ y Gemini no disponen de un tokenizador local de acceso
público. Para contar sus tokens de forma exacta es necesario realizar una llamada
de red al endpoint dedicado de recuento de tokens del proveedor. La clase
`TokenCounter` gestiona el enrutamiento automáticamente: los modelos `claude-*`
van a Anthropic, los `gemini-*` van a Google, y todo lo demás se cuenta
localmente mediante BPE.

```php
<?php

require_once __DIR__ . '/vendor/autoload.php';

use Tokenizers\TokenCounter;

// TokenCounter lee las claves de API del entorno por defecto:
//   ANTHROPIC_API_KEY  — para modelos claude-*
//   GEMINI_API_KEY (o GOOGLE_API_KEY)  — para modelos gemini-*
//
// Pasa las credenciales explícitamente cuando lo necesites:
//   new TokenCounter(
//       new \Tokenizers\Remote\Anthropic(apiKey: 'sk-ant-...'),
//       new \Tokenizers\Remote\Gemini(apiKey: 'AIza...')
//   );

$counter = new TokenCounter();

$text = 'Summarise the following legal document in three bullet points.';

// BPE local — sin llamada de red, sin clave de API necesaria.
$localCount = $counter->count('cl100k_base', $text);
printf("cl100k_base (local):   %d tokens\n", $localCount);

// Remoto — realiza una llamada de red al endpoint de recuento de tokens de Anthropic.
$claudeCount = $counter->count('claude-opus-4-8', $text);
printf("claude-opus-4-8 (API): %d tokens\n", $claudeCount);

// Remoto — realiza una llamada de red al endpoint de recuento de tokens de Google.
$geminiCount = $counter->count('gemini-1.5-flash', $text);
printf("gemini-1.5-flash (API): %d tokens\n", $geminiCount);
```

También puedes comprobar qué backend utilizará un modelo sin realizar ninguna
llamada de red:

```php
<?php

use Tokenizers\TokenCounter;

echo TokenCounter::route('claude-opus-4-8');  // 'anthropic'
echo TokenCounter::route('gemini-1.5-flash'); // 'gemini'
echo TokenCounter::route('cl100k_base');      // 'local'
```

### Consideraciones de rendimiento para el recuento remoto

Cada llamada a `count()` para un modelo remoto realiza una solicitud HTTP real.
Para batches grandes:

- **Muestrea, no lo cuentes todo.** Cuenta un subconjunto representativo de tus
  prompts y extrapola. Si los prompts son estructuralmente similares (mismo system
  message + texto de usuario variable), la varianza suele ser baja.
- **Cachea los resultados.** Si el mismo texto de prompt se usa repetidamente
  (por ejemplo, un system message con plantilla), cachea su recuento de tokens en
  lugar de volver a consultarlo.
- **Usa `countTokens` para la parte local.** Si el prompt se compone de un system
  message específico de Claude más un documento grande tokenizable localmente,
  puedes contar el documento con `cl100k_base` como aproximación para la parte
  que es estructuralmente similar entre modelos BPE.

Para instrucciones completas de configuración, configuración de variables de
entorno y pruebas sin conexión con un transport simulado, consulta
[remote-providers.md](remote-providers.md).

---

## Véase también

- [loading-models.md](loading-models.md) — cómo cargar encodings de OpenAI y
  HuggingFace, la caché y la factoría `Encoding`.
- [remote-providers.md](remote-providers.md) — configuración complementaria de
  Claude y Gemini, configuración de claves de API, pruebas sin conexión.
- [../api-reference.md](../api-reference.md) — referencia completa de la API para
  `Bpe`, `Encoding`, `TokenCounter`, `Remote\Anthropic` y `Remote\Gemini`.
- [../getting-started.md](../getting-started.md) — instalación y primera
  tokenización.
