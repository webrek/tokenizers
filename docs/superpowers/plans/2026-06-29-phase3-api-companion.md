# Phase 3 — Remote-API Companion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use `- [ ]`.

**Goal:** A PHP-only companion that counts tokens for **Claude** (Anthropic `count_tokens`) and **Gemini** (`countTokens`) via their APIs, plus a `\Tokenizers\TokenCounter` facade that routes a model to the local extension or the right remote provider.

**Architecture:** Clients depend on an injectable `Transport` interface (testable offline). `CurlTransport` is the production impl (ext-curl). No C changes; ships in `php/Tokenizers/`. No new hard deps beyond ext-curl + ext-json.

**Tech Stack:** PHP 8.3+, ext-curl, ext-json. Tests are `.phpt` with a fake `Transport` (no network).

## Global Constraints
- PHP 8.3+. No C changes. No new hard dependency beyond `ext-curl` (+ existing `ext-json`). Do NOT add the `anthropic-ai/sdk` or any HTTP library (Guzzle, etc.) — raw curl only (spec decision D1).
- Errors throw `\Tokenizers\TokenizerException` (already defined by the extension).
- All clients depend on the `Transport` interface, never on curl directly — this is what makes them testable offline.
- API keys: Anthropic `ANTHROPIC_API_KEY`; Gemini `GEMINI_API_KEY` then `GOOGLE_API_KEY`. Constructor arg overrides env. Missing key → throw at call time.
- Wire formats are authoritative as written in the spec; the Gemini task MUST WebFetch the current Google doc to confirm field names before finalizing.
- No build artifacts tracked in git. `.phpt` tests run against the existing `modules/tokenizers.so` (for `TokenizerException` + the local routing path).

## File Structure
```
php/Tokenizers/Remote/Http.php          — interface Transport
php/Tokenizers/Remote/CurlTransport.php — Transport via ext-curl
php/Tokenizers/Remote/Anthropic.php     — countTokens() -> /v1/messages/count_tokens
php/Tokenizers/Remote/Gemini.php        — countTokens() -> models/{m}:countTokens
php/Tokenizers/TokenCounter.php         — unified facade (routing)
tests/_fake_transport.php               — test helper: closure/canned Transport
tests/040-anthropic-count.phpt, 041-gemini-count.phpt, 042-tokencounter-route.phpt, 043-live-smoke.phpt
```

---

## Task 1: Transport + CurlTransport + Anthropic client

**Files:** Create `php/Tokenizers/Remote/Http.php`, `php/Tokenizers/Remote/CurlTransport.php`, `php/Tokenizers/Remote/Anthropic.php`, `tests/_fake_transport.php`, `tests/040-anthropic-count.phpt`.

**Interfaces:**
- Produces: `\Tokenizers\Remote\Transport` (interface), `\Tokenizers\Remote\CurlTransport`, `\Tokenizers\Remote\Anthropic`.

- [ ] **Step 1: Write the failing test + fake transport**

`tests/_fake_transport.php`:
```php
<?php
namespace Tokenizers\Remote;
final class FakeTransport implements Transport {
    public array $calls = [];
    public function __construct(private int $status, private string $respBody) {}
    public function post(string $url, array $headers, string $body, int $timeout): array {
        $this->calls[] = ['url'=>$url, 'headers'=>$headers, 'body'=>$body, 'timeout'=>$timeout];
        return ['status'=>$this->status, 'body'=>$this->respBody];
    }
}
```

`tests/040-anthropic-count.phpt`:
```
--TEST--
Anthropic::countTokens builds the right request and parses input_tokens
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Remote\{Anthropic, FakeTransport};
require __DIR__ . '/../php/Tokenizers/Remote/Http.php';
require __DIR__ . '/../php/Tokenizers/Remote/CurlTransport.php';
require __DIR__ . '/../php/Tokenizers/Remote/Anthropic.php';
require __DIR__ . '/_fake_transport.php';

$t = new FakeTransport(200, '{"input_tokens":42}');
$a = new Anthropic(apiKey: 'sk-test', transport: $t);
echo $a->countTokens('claude-opus-4-8', 'hello world'), "\n";   // 42

$c = $t->calls[0];
echo $c['url'], "\n";
echo in_array('x-api-key: sk-test', $c['headers'], true) ? "has-key\n" : "no-key\n";
echo (str_contains($c['body'], '"model":"claude-opus-4-8"') && str_contains($c['body'], '"hello world"')) ? "body-ok\n" : "body-bad\n";

// system param included when given
$a->countTokens('claude-opus-4-8', 'hi', 'be terse');
echo str_contains($t->calls[1]['body'], '"system":"be terse"') ? "sys-ok\n" : "sys-bad\n";

// HTTP error -> throws
try { (new Anthropic(apiKey:'k', transport:new FakeTransport(400,'{"error":"bad"}')))->countTokens('m','x'); echo "no throw\n"; }
catch (\Tokenizers\TokenizerException $e) { echo "http-throws\n"; }

// missing key -> throws (clear env for the test)
putenv('ANTHROPIC_API_KEY');
try { (new Anthropic(transport:new FakeTransport(200,'{"input_tokens":1}')))->countTokens('m','x'); echo "no throw\n"; }
catch (\Tokenizers\TokenizerException $e) { echo "nokey-throws\n"; }
?>
--EXPECT--
42
https://api.anthropic.com/v1/messages/count_tokens
has-key
body-ok
sys-ok
http-throws
nokey-throws
```

- [ ] **Step 2: Run it — RED** (`php -d extension=modules/tokenizers.so tests/040-anthropic-count.phpt`) → class not found.

- [ ] **Step 3: Implement the three files**

`php/Tokenizers/Remote/Http.php`:
```php
<?php
namespace Tokenizers\Remote;
interface Transport {
    /** @return array{status:int, body:string} */
    public function post(string $url, array $headers, string $body, int $timeout): array;
}
```

`php/Tokenizers/Remote/CurlTransport.php`:
```php
<?php
namespace Tokenizers\Remote;
use Tokenizers\TokenizerException;
final class CurlTransport implements Transport {
    public function post(string $url, array $headers, string $body, int $timeout): array {
        if (!\function_exists('curl_init')) throw new TokenizerException('ext-curl is required for remote token counting');
        $ch = \curl_init($url);
        \curl_setopt_array($ch, [
            \CURLOPT_POST => true,
            \CURLOPT_POSTFIELDS => $body,
            \CURLOPT_HTTPHEADER => $headers,
            \CURLOPT_RETURNTRANSFER => true,
            \CURLOPT_TIMEOUT => $timeout,
            \CURLOPT_CONNECTTIMEOUT => $timeout,
        ]);
        $resp = \curl_exec($ch);
        if ($resp === false) { $e = \curl_error($ch); \curl_close($ch); throw new TokenizerException("HTTP transport error: $e"); }
        $status = (int)\curl_getinfo($ch, \CURLINFO_HTTP_CODE);
        \curl_close($ch);
        return ['status'=>$status, 'body'=>(string)$resp];
    }
}
```

`php/Tokenizers/Remote/Anthropic.php`:
```php
<?php
namespace Tokenizers\Remote;
use Tokenizers\TokenizerException;
final class Anthropic {
    private string $version; private int $timeout; private ?string $apiKey; private Transport $transport;
    public function __construct(?string $apiKey = null, ?Transport $transport = null, string $version = '2023-06-01', int $timeout = 30) {
        $this->apiKey = $apiKey ?? (getenv('ANTHROPIC_API_KEY') ?: null);
        $this->transport = $transport ?? new CurlTransport();
        $this->version = $version; $this->timeout = $timeout;
    }
    /** @param string|array $messages plain string (one user turn) or a full messages array */
    public function countTokens(string $model, string|array $messages, ?string $system = null): int {
        if (!$this->apiKey) throw new TokenizerException('ANTHROPIC_API_KEY not set');
        $msgs = \is_string($messages) ? [['role'=>'user','content'=>$messages]] : $messages;
        $payload = ['model'=>$model, 'messages'=>$msgs];
        if ($system !== null) $payload['system'] = $system;
        $body = \json_encode($payload, \JSON_UNESCAPED_SLASHES | \JSON_UNESCAPED_UNICODE);
        $resp = $this->transport->post(
            'https://api.anthropic.com/v1/messages/count_tokens',
            ['x-api-key: '.$this->apiKey, 'anthropic-version: '.$this->version, 'content-type: application/json'],
            $body, $this->timeout
        );
        if ($resp['status'] < 200 || $resp['status'] >= 300) throw new TokenizerException("Anthropic count_tokens HTTP {$resp['status']}: {$resp['body']}");
        $j = \json_decode($resp['body'], true);
        if (!\is_array($j) || !isset($j['input_tokens'])) throw new TokenizerException('Anthropic count_tokens: missing input_tokens');
        return (int)$j['input_tokens'];
    }
}
```

- [ ] **Step 4: GREEN** — run the phpt; matches `--EXPECT--`.
- [ ] **Step 5: Commit** (explicit paths).

---

## Task 2: Gemini client

**Files:** Create `php/Tokenizers/Remote/Gemini.php`, `tests/041-gemini-count.phpt`.

**Pre-req:** WebFetch the current Google doc `https://ai.google.dev/api/tokens` (or `https://ai.google.dev/gemini-api/docs/tokens`) and confirm: the `:countTokens` path, that auth may be the `x-goog-api-key: <key>` header (preferred over `?key=` to keep the key out of URLs/logs), the request body `{"contents":[{"parts":[{"text":...}]}]}`, and the response field `totalTokens`. Pin exact names before implementing.

- [ ] **Step 1: Failing test**

`tests/041-gemini-count.phpt`:
```
--TEST--
Gemini::countTokens builds the right request and parses totalTokens
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Remote\{Gemini, FakeTransport};
require __DIR__ . '/../php/Tokenizers/Remote/Http.php';
require __DIR__ . '/../php/Tokenizers/Remote/CurlTransport.php';
require __DIR__ . '/../php/Tokenizers/Remote/Gemini.php';
require __DIR__ . '/_fake_transport.php';

$t = new FakeTransport(200, '{"totalTokens":7}');
$g = new Gemini(apiKey: 'gk-test', transport: $t);
echo $g->countTokens('gemini-1.5-flash', 'hello'), "\n";  // 7

$c = $t->calls[0];
echo str_contains($c['url'], 'models/gemini-1.5-flash:countTokens') ? "url-ok\n" : "url-bad\n";
echo str_contains($c['body'], '"text":"hello"') ? "body-ok\n" : "body-bad\n";
// key present (header or query — whichever the WebFetch confirmed)
$hasKey = in_array('x-goog-api-key: gk-test', $c['headers'], true) || str_contains($c['url'], 'gk-test');
echo $hasKey ? "has-key\n" : "no-key\n";

try { (new Gemini(apiKey:'k', transport:new FakeTransport(403,'{"error":{}}')))->countTokens('gemini-1.5-flash','x'); echo "no throw\n"; }
catch (\Tokenizers\TokenizerException $e) { echo "http-throws\n"; }
?>
--EXPECT--
7
url-ok
body-ok
has-key
http-throws
```

- [ ] **Step 2: RED.**
- [ ] **Step 3: Implement `Gemini.php`** — accept `model` with or without a leading `models/`; build `https://generativelanguage.googleapis.com/v1beta/models/{model}:countTokens`; send the API key via the `x-goog-api-key` header (confirmed by the WebFetch); body `{"contents":[{"parts":[{"text":$text}]}]}`; parse `totalTokens`; non-2xx and missing-key throw `TokenizerException`. Key resolution: `GEMINI_API_KEY` then `GOOGLE_API_KEY`.
- [ ] **Step 4: GREEN.**
- [ ] **Step 5: Commit.** Note in the report which auth mechanism (header vs query) the WebFetch confirmed.

---

## Task 3: `\Tokenizers\TokenCounter` facade

**Files:** Create `php/Tokenizers/TokenCounter.php`, `tests/042-tokencounter-route.phpt`.

**Interfaces:** Consumes the two remote clients + the extension's `Encoding::load` for the local path.

- [ ] **Step 1: Failing test** — assert routing only (no network):
```
--TEST--
TokenCounter routes models to the right backend
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\TokenCounter;
require __DIR__ . '/../php/Tokenizers/Remote/Http.php';
require __DIR__ . '/../php/Tokenizers/Remote/CurlTransport.php';
require __DIR__ . '/../php/Tokenizers/Remote/Anthropic.php';
require __DIR__ . '/../php/Tokenizers/Remote/Gemini.php';
require __DIR__ . '/../php/Tokenizers/TokenCounter.php';
require __DIR__ . '/_fake_transport.php';

echo TokenCounter::route('claude-opus-4-8'), "\n";     // anthropic
echo TokenCounter::route('gemini-1.5-pro'), "\n";       // gemini
echo TokenCounter::route('cl100k_base'), "\n";          // local

// remote counts via injected fakes
use Tokenizers\Remote\{Anthropic, Gemini, FakeTransport};
$tc = new TokenCounter(
    new Anthropic(apiKey:'k', transport:new FakeTransport(200,'{"input_tokens":11}')),
    new Gemini(apiKey:'k', transport:new FakeTransport(200,'{"totalTokens":5}')),
);
echo $tc->count('claude-opus-4-8', 'hi'), "\n";   // 11
echo $tc->count('gemini-1.5-flash', 'hi'), "\n";  // 5
echo $tc->count('whatever', 'hi', 'anthropic'), "\n"; // explicit override -> 11
?>
--EXPECT--
anthropic
gemini
local
11
5
11
```

- [ ] **Step 2: RED → Step 3: implement `TokenCounter`** (static `route()` + instance `count()` with the `local|anthropic|gemini` match; local path calls `Encoding::load($model)->countTokens($text)`; injected clients used when present, else constructed lazily). → **Step 4: GREEN → Step 5: Commit.**

---

## Task 4: Docs + opt-in live smoke test

**Files:** Modify `README.md` (add a "Remote providers (Claude / Gemini)" section). Create `tests/043-live-smoke.phpt`.

- [ ] **Step 1:** Write `tests/043-live-smoke.phpt` with a `--SKIPIF--` that skips unless `ANTHROPIC_API_KEY` (and/or `GEMINI_API_KEY`) is set; when set, it does one real `countTokens` per available provider and asserts the result is a positive int. This is opt-in; CI without keys skips it.
- [ ] **Step 2:** README section: what it is (Claude/Gemini have no local tokenizer — counts require the provider API + a key), the three classes + `TokenCounter` with a runnable example, the env vars, the honest boundary (network + key required; exact only as of the provider's current tokenizer), and the dependency note (ext-curl; no SDK). Cross-reference: callers already using `anthropic-ai/sdk` can call its `messages->countTokens()` directly.
- [ ] **Step 3:** Run the full `.phpt` suite (040/041/042 pass; 043 skips without keys) + confirm Phase 1/2 tests unaffected. Commit.

---

## Plan Self-Review
- Spec coverage: Transport+Anthropic (T1), Gemini (T2), TokenCounter (T3), docs+live-smoke (T4) cover the Phase 3 goals. ✓
- No placeholders: clients have full code; Gemini's exact auth/field names are pinned by a mandated WebFetch in T2.
- Type consistency: `Transport::post` signature identical across CurlTransport, FakeTransport, and the clients' calls. ✓
- Risk: provider drift — mitigated by the transport seam + the T2 WebFetch; no offline parity check is possible (documented).

## Execution Handoff
Subagent-driven (autonomous goal). After T4, run a final whole-branch review, then merge `phase3-api-companion` → `main`. That completes Phase 3 (and the goal "dale con los dos").
