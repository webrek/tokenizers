--TEST--
Live smoke test: countTokens against real provider APIs (opt-in, skipped without keys)
--SKIPIF--
<?php
// Pure-PHP remote companion — no extension needed. Gate only on API keys so the
// test can run in extension-free CI that has keys, and SKIPS cleanly without them.
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

$text = 'Hello, world!';

// Each present provider must return a POSITIVE int. A non-positive count (or any
// thrown error) exits non-zero before "SMOKE OK", so the test turns red — the
// assertion is real, not a print behind a wildcard EXPECT.
if (getenv('ANTHROPIC_API_KEY')) {
    $n = (new Tokenizers\Remote\Anthropic())->countTokens('claude-opus-4-8', $text);
    if (!is_int($n) || $n <= 0) { echo "FAIL anthropic=$n\n"; exit(1); }
}
if (getenv('GEMINI_API_KEY') || getenv('GOOGLE_API_KEY')) {
    $n = (new Tokenizers\Remote\Gemini())->countTokens('gemini-1.5-flash', $text);
    if (!is_int($n) || $n <= 0) { echo "FAIL gemini=$n\n"; exit(1); }
}
echo "SMOKE OK\n";
?>
--EXPECT--
SMOKE OK
