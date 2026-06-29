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
