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
