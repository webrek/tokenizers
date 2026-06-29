--TEST--
Live smoke test: countTokens against real provider APIs (opt-in, skipped without keys)
--SKIPIF--
<?php
// The remote companion is standalone (it bootstraps its own TokenizerException
// polyfill when the extension is absent), so we gate only on API keys: SKIP
// cleanly in CI without them, hit the network only when opted in.
if (!getenv('ANTHROPIC_API_KEY') && !getenv('GEMINI_API_KEY') && !getenv('GOOGLE_API_KEY')) {
    echo 'skip no API keys set (opt-in live test)';
}
?>
--FILE--
<?php
require __DIR__ . '/../php/Tokenizers/Remote/Http.php';
require __DIR__ . '/../php/Tokenizers/Remote/CurlTransport.php';
require __DIR__ . '/../php/Tokenizers/Remote/Anthropic.php';
require __DIR__ . '/../php/Tokenizers/Remote/Gemini.php';
use Tokenizers\TokenizerException;

$text = 'Hello, world!';

// Real assertion: a present, AUTHORIZED key must yield a POSITIVE int. A bad count
// or an unexpected error exits non-zero before "SMOKE OK" (the test turns red).
// An auth/permission failure (401/403) means the key isn't enabled for that API —
// an environment/setup issue, not a tokenizer bug — so we skip that provider rather
// than fail. (A malformed-request 400, 5xx, or wrong count still fails hard.)
$check = function (callable $call, string $name): void {
    try {
        $n = $call();
    } catch (TokenizerException $e) {
        $m = $e->getMessage();
        if (str_contains($m, 'HTTP 401') || str_contains($m, 'HTTP 403')) {
            echo "$name: skipped (key not authorized for this API)\n";
            return;
        }
        echo "FAIL $name: $m\n"; exit(1);
    }
    if (!is_int($n) || $n <= 0) { echo "FAIL $name: non-positive count ($n)\n"; exit(1); }
    echo "$name: ok ($n)\n";
};

if (getenv('ANTHROPIC_API_KEY')) {
    $check(fn() => (new Tokenizers\Remote\Anthropic())->countTokens('claude-opus-4-8', $text), 'anthropic');
}
if (getenv('GEMINI_API_KEY') || getenv('GOOGLE_API_KEY')) {
    $check(fn() => (new Tokenizers\Remote\Gemini())->countTokens('gemini-1.5-flash', $text), 'gemini');
}
echo "SMOKE OK\n";
?>
--EXPECTF--
%aSMOKE OK
