--TEST--
Live smoke test: countTokens against real provider APIs (opt-in, skipped without keys)
--SKIPIF--
<?php
if (!extension_loaded('tokenizers')) echo 'skip extension not loaded';
elseif (!getenv('ANTHROPIC_API_KEY') && !getenv('GEMINI_API_KEY') && !getenv('GOOGLE_API_KEY')) {
    echo 'skip ANTHROPIC_API_KEY and GEMINI_API_KEY not set (opt-in live test)';
}
?>
--FILE--
<?php
require __DIR__ . '/../php/Tokenizers/Remote/Http.php';
require __DIR__ . '/../php/Tokenizers/Remote/CurlTransport.php';
require __DIR__ . '/../php/Tokenizers/Remote/Anthropic.php';
require __DIR__ . '/../php/Tokenizers/Remote/Gemini.php';

$text = 'Hello, world!';

if (getenv('ANTHROPIC_API_KEY')) {
    $n = (new Tokenizers\Remote\Anthropic())->countTokens('claude-opus-4-8', $text);
    echo 'anthropic: ', ($n > 0 ? 'ok' : 'fail'), "\n";
}

if (getenv('GEMINI_API_KEY') || getenv('GOOGLE_API_KEY')) {
    $n = (new Tokenizers\Remote\Gemini())->countTokens('gemini-1.5-flash', $text);
    echo 'gemini: ', ($n > 0 ? 'ok' : 'fail'), "\n";
}
?>
--EXPECTF--
%a
