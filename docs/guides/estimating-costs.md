# Estimating and Budgeting LLM Costs

You pay per token. That means every prompt you send to a model has a calculable
cost — before you ever make the API call. Counting tokens locally, in PHP,
before you call the model lets you:

- **Budget accurately.** Know what a batch of requests will cost before
  committing.
- **Enforce context limits.** Every model has a maximum context window; a prompt
  that exceeds it fails. Catch that locally, not at the provider.
- **Route intelligently.** A 200-token prompt may be fine for an expensive model;
  a 10,000-token prompt may warrant a cheaper model or a smaller input.

This guide walks through a realistic scenario: an AI writing agency that
generates copy variants for multiple clients and features, and needs to track
spend per client before the month's bill arrives.

---

## 1. Why count tokens before you call

Most LLM providers charge on a per-token basis split between input (prompt) and
output (completion). A large prompt — a long system message, a user document, a
few-shot example set — can quietly cost more than the model output itself.

Counting tokens locally costs nothing and takes microseconds. The alternative —
sending the request, reading the bill — costs money and time. Front-load the
math.

Three practical reasons to count before calling:

1. **Budgeting.** "This feature will process 50,000 prompts today" becomes a
   real dollar estimate rather than a guess.
2. **Context-window enforcement.** If `countTokens($text) > $maxContextTokens`,
   you can truncate, summarise, or reject the input before it fails at the
   provider with a cryptic error.
3. **Model routing.** Token count is one of the cheapest signals for routing:
   short prompts to a fast/cheap model, long ones to a model with a bigger
   window.

---

## 2. Estimate the cost of a prompt

Load an `Encoding`, count the tokens in your prompt text, and multiply by the
model's input price. Prices vary by model and change over time — **always
confirm the current price in the provider's official pricing page.** The numbers
below are illustrative only.

```php
<?php

require_once __DIR__ . '/vendor/autoload.php';

use Tokenizers\Encoding;

// Load the cl100k_base encoding (used by GPT-4, o1, o3, and similar models).
// The vocab file is downloaded on first use and cached locally; subsequent
// calls are nearly instant.
$enc = Encoding::load('cl100k_base');

/**
 * Estimate the input cost of a single piece of text.
 *
 * @param  \Tokenizers\Bpe $enc            A loaded Encoding instance.
 * @param  string          $text           The prompt text to measure.
 * @param  float           $pricePerMillion Price per one million INPUT tokens
 *                                          in USD — confirm with your provider.
 * @return array{tokens: int, cost: float}
 */
function estimateCost(\Tokenizers\Bpe $enc, string $text, float $pricePerMillion): array
{
    $tokens = $enc->countTokens($text);
    $cost   = ($tokens / 1_000_000) * $pricePerMillion;

    return ['tokens' => $tokens, 'cost' => $cost];
}

// -------------------------------------------------------------------------
// Example: a system message + user document for a GPT-4-class model.
// Replace $inputPricePerMillion with the current figure from your provider.
// -------------------------------------------------------------------------
$inputPricePerMillion = 5.00; // ILLUSTRATIVE — verify at your provider's pricing page

$systemPrompt = 'You are a professional copywriter. Write in a clear, engaging style.';
$userDocument = file_get_contents('/path/to/client/brief.txt');
$fullPrompt   = $systemPrompt . "\n\n" . $userDocument;

$estimate = estimateCost($enc, $fullPrompt, $inputPricePerMillion);

printf(
    "Prompt: %d tokens  →  estimated input cost: $%.6f\n",
    $estimate['tokens'],
    $estimate['cost']
);
// e.g. "Prompt: 1423 tokens  →  estimated input cost: $0.007115"
```

**Formula:** `cost = (tokenCount / 1_000_000) × pricePerMillion`

This gives you the estimated *input* cost. Output (completion) tokens are
usually priced separately and at a different rate; you need to estimate or cap
output length separately.

> **Note on `countTokens` vs `encode`:** `countTokens` returns only an integer;
> it never allocates the full token-id array. For cost estimation, always prefer
> `countTokens` — it is measurably faster on long texts.

---

## 3. Budget a batch

The agency needs to generate 100 copy variants from a set of briefs. Before
sending a single API request, sum the token counts across all inputs.

```php
<?php

require_once __DIR__ . '/vendor/autoload.php';

use Tokenizers\Encoding;

$enc = Encoding::load('cl100k_base');

// ILLUSTRATIVE prices — replace with current figures from your provider.
$inputPricePerMillion  = 5.00;
$outputPricePerMillion = 15.00;
$estimatedOutputTokensPerVariant = 300; // your expected completion length

// Simulate 100 brief texts (in practice, load these from a database or files).
$briefs = array_map(
    fn(int $i) => "Write a compelling product description for item #{$i}. "
                . "Focus on benefits, tone: professional, length: ~150 words.",
    range(1, 100)
);

$totalInputTokens = 0;

foreach ($briefs as $brief) {
    // countTokens is preferred over encode() here — no id array is built.
    $totalInputTokens += $enc->countTokens($brief);
}

$totalOutputTokens = count($briefs) * $estimatedOutputTokensPerVariant;

$inputCost  = ($totalInputTokens  / 1_000_000) * $inputPricePerMillion;
$outputCost = ($totalOutputTokens / 1_000_000) * $outputPricePerMillion;
$totalCost  = $inputCost + $outputCost;

printf("Batch summary (100 variants)\n");
printf("  Total input tokens   : %d\n",   $totalInputTokens);
printf("  Est. output tokens   : %d\n",   $totalOutputTokens);
printf("  Est. input cost      : $%.4f\n", $inputCost);
printf("  Est. output cost     : $%.4f\n", $outputCost);
printf("  Est. total cost      : $%.4f\n", $totalCost);
```

This runs in milliseconds, entirely locally. You can add a guard that aborts or
alerts when `$totalCost` exceeds a per-job budget threshold before the first
API call is made.

---

## 4. Stay within the context window

Every model specifies a maximum number of tokens it will accept. Exceeding it
returns an error from the provider. Two common needs: checking whether a prompt
fits, and truncating it if it does not.

### Check: does this prompt fit?

```php
<?php

use Tokenizers\Encoding;

$enc = Encoding::load('cl100k_base');

/**
 * Returns true if $text fits within $maxTokens.
 * Use this as a fast pre-flight check before sending to the model.
 */
function fitsInBudget(\Tokenizers\Bpe $enc, string $text, int $maxTokens): bool
{
    return $enc->countTokens($text) <= $maxTokens;
}

$contextWindow = 8_192; // example limit — check your model's spec
$document      = file_get_contents('/path/to/document.txt');

if (!fitsInBudget($enc, $document, $contextWindow)) {
    // truncate, chunk, or summarise before proceeding
    echo "Document too long — truncation required.\n";
}
```

### Truncate to N tokens

When you need to shorten a text to fit, you can encode, slice the token array,
then decode back to a string.

```php
<?php

use Tokenizers\Encoding;

$enc = Encoding::load('cl100k_base');

/**
 * Truncate $text to at most $maxTokens tokens and return the decoded string.
 *
 * IMPORTANT CAVEAT: byte-level BPE can encode a single character as multiple
 * token IDs, and the character boundaries do not always align with token
 * boundaries. Slicing the ID array at an arbitrary position and decoding can
 * produce garbled UTF-8 at the cut point — typically one or a few bytes off.
 * For most use cases (staying within a context window) this is acceptable.
 * If you need a clean UTF-8 cut, additionally strip any trailing replacement
 * characters (\u{FFFD}) or use mb_convert_encoding() to coerce the result.
 *
 * @return string The decoded, token-truncated text. May have a rough boundary.
 */
function truncateToTokens(\Tokenizers\Bpe $enc, string $text, int $maxTokens): string
{
    $ids = $enc->encode($text);

    if (count($ids) <= $maxTokens) {
        return $text; // already fits; skip the decode round-trip
    }

    $truncatedIds = array_slice($ids, 0, $maxTokens);

    return $enc->decode($truncatedIds);
}

$maxTokens = 4_096;
$rawText   = file_get_contents('/path/to/long-document.txt');
$fitted    = truncateToTokens($enc, $rawText, $maxTokens);

printf("Original: %d tokens → truncated: %d tokens\n",
    $enc->countTokens($rawText),
    $enc->countTokens($fitted)
);
```

> **When not to use truncation:** If the document has meaningful structure
> (sections, JSON, code), slicing by tokens rather than logical boundaries can
> corrupt the content semantically. In those cases, prefer chunking at logical
> delimiters and processing chunks individually.

---

## 5. Track spend per client/feature

For an agency, per-client and per-feature cost attribution is essential for
invoicing and for spotting which features are burning budget.

```php
<?php

require_once __DIR__ . '/vendor/autoload.php';

use Tokenizers\Encoding;

$enc = Encoding::load('cl100k_base');

// ILLUSTRATIVE prices — verify current figures with your provider.
const INPUT_PRICE_PER_MILLION  = 5.00;
const OUTPUT_PRICE_PER_MILLION = 15.00;

/**
 * A simple accumulator for token spend across labels (clients, features, etc.).
 * This is plain application code — it does not ship with the library.
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
     * Returns a report array keyed by label.
     * Pass $inputPpm and $outputPpm as your current per-million-token prices.
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
// Simulate processing requests across clients and features.
// -------------------------------------------------------------------------
$tracker = new SpendTracker();

$jobs = [
    ['label' => 'client:acme/feature:product-descriptions', 'prompt' => 'Write a product description for a coffee maker.'],
    ['label' => 'client:acme/feature:email-subject-lines',  'prompt' => 'Generate 5 email subject lines for a sale event.'],
    ['label' => 'client:globex/feature:product-descriptions','prompt' => 'Describe the Globex Series 7 industrial pump.'],
    ['label' => 'client:globex/feature:social-posts',        'prompt' => 'Write a LinkedIn post about our Q2 results.'],
];

// Assume output tokens = 200 per request (replace with actual completionUsage).
$estimatedOutputTokens = 200;

foreach ($jobs as $job) {
    $inputTokens = $enc->countTokens($job['prompt']);
    $tracker->record($job['label'], $inputTokens, $estimatedOutputTokens);
}

// Print report.
$report = $tracker->report(INPUT_PRICE_PER_MILLION, OUTPUT_PRICE_PER_MILLION);

echo "\n=== Spend Report ===\n";
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

In production, replace the hardcoded `$estimatedOutputTokens` with the actual
`usage.completion_tokens` (or equivalent) returned in the API response so that
your tracker captures real numbers after each call.

---

## 6. Counting for Claude and Gemini

Claude 3+ and Gemini models do not have a publicly available local tokenizer.
To count their tokens exactly you must make a network call to the provider's
dedicated token-counting endpoint. The `TokenCounter` class handles routing
automatically: `claude-*` models go to Anthropic, `gemini-*` models go to
Google, and anything else is counted locally via BPE.

```php
<?php

require_once __DIR__ . '/vendor/autoload.php';

use Tokenizers\TokenCounter;

// TokenCounter reads API keys from the environment by default:
//   ANTHROPIC_API_KEY  — for claude-* models
//   GEMINI_API_KEY (or GOOGLE_API_KEY)  — for gemini-* models
//
// Pass credentials explicitly when you need to:
//   new TokenCounter(
//       new \Tokenizers\Remote\Anthropic(apiKey: 'sk-ant-...'),
//       new \Tokenizers\Remote\Gemini(apiKey: 'AIza...')
//   );

$counter = new TokenCounter();

$text = 'Summarise the following legal document in three bullet points.';

// Local BPE — no network call, no API key needed.
$localCount = $counter->count('cl100k_base', $text);
printf("cl100k_base (local):   %d tokens\n", $localCount);

// Remote — makes a network call to the Anthropic token-count endpoint.
$claudeCount = $counter->count('claude-opus-4-8', $text);
printf("claude-opus-4-8 (API): %d tokens\n", $claudeCount);

// Remote — makes a network call to the Google token-count endpoint.
$geminiCount = $counter->count('gemini-1.5-flash', $text);
printf("gemini-1.5-flash (API): %d tokens\n", $geminiCount);
```

You can also check which backend a model will use without making any network
call:

```php
<?php

use Tokenizers\TokenCounter;

echo TokenCounter::route('claude-opus-4-8');  // 'anthropic'
echo TokenCounter::route('gemini-1.5-flash'); // 'gemini'
echo TokenCounter::route('cl100k_base');      // 'local'
```

### Performance consideration for remote counting

Each call to `count()` for a remote model makes a real HTTP request. For large
batches:

- **Sample, don't count everything.** Count a representative subset of your
  prompts and extrapolate. If your prompts are structurally similar (same system
  message + variable user text), the variance is usually low.
- **Cache results.** If the same prompt text is used repeatedly (e.g., a
  templated system message), cache its token count rather than re-fetching it.
- **Prefer `countTokens` for the local portion.** If the prompt is assembled
  from a Claude-specific system message plus a large locally-tokenisable
  document, you can count the document with `cl100k_base` as an approximation
  for the part that is structurally similar across BPE models.

For full setup instructions, environment variable configuration, and offline
testing with a fake transport, see [remote-providers.md](remote-providers.md).

---

## See also

- [loading-models.md](loading-models.md) — how to load OpenAI and HuggingFace
  encodings, the cache, and the `Encoding` factory.
- [remote-providers.md](remote-providers.md) — Claude and Gemini companion
  setup, API key configuration, offline testing.
- [../api-reference.md](../api-reference.md) — complete API reference for
  `Bpe`, `Encoding`, `TokenCounter`, `Remote\Anthropic`, and `Remote\Gemini`.
- [../getting-started.md](../getting-started.md) — installation and first
  tokenization.
