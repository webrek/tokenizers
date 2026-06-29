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
echo TokenCounter::route('anthropic-claude'), "\n";     // anthropic
echo TokenCounter::route('models/gemini-pro'), "\n";    // gemini
echo TokenCounter::route('CLAUDE-OPUS'), "\n";          // anthropic (lowercased)

// remote counts via injected fakes
use Tokenizers\Remote\{Anthropic, Gemini, FakeTransport};
$tc = new TokenCounter(
    new Anthropic(apiKey:'k', transport:new FakeTransport(200,'{"input_tokens":11}')),
    new Gemini(apiKey:'k', transport:new FakeTransport(200,'{"totalTokens":5}')),
);
echo $tc->count('claude-opus-4-8', 'hi'), "\n";   // 11
echo $tc->count('gemini-1.5-flash', 'hi'), "\n";  // 5
echo $tc->count('whatever', 'hi', 'anthropic'), "\n"; // explicit override -> 11

// explicit unknown provider -> throws with provider in the message
try { $tc->count('gpt-4', 'hi', 'openai'); echo "no throw\n"; }
catch (\Tokenizers\TokenizerException $e) { echo (str_contains($e->getMessage(), 'openai') ? "unknown-throws\n" : "throws-no-provider\n"); }
?>
--EXPECT--
anthropic
gemini
local
anthropic
gemini
anthropic
11
5
11
unknown-throws
