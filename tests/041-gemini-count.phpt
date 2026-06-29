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

// a leading "models/" must not double-prefix
$t2 = new FakeTransport(200, '{"totalTokens":3}');
(new Gemini(apiKey:'gk', transport:$t2))->countTokens('models/gemini-1.5-flash', 'x');
$u = $t2->calls[0]['url'];
echo (str_contains($u, 'models/gemini-1.5-flash:countTokens') && !str_contains($u, 'models/models/')) ? "norm-ok\n" : "norm-bad\n";

// missing key (both env vars cleared) -> throws before any transport call
putenv('GEMINI_API_KEY'); putenv('GOOGLE_API_KEY');
try { (new Gemini(transport:new FakeTransport(200,'{"totalTokens":1}')))->countTokens('gemini-1.5-flash','x'); echo "no throw\n"; }
catch (\Tokenizers\TokenizerException $e) { echo "nokey-throws\n"; }
?>
--EXPECT--
7
url-ok
body-ok
has-key
http-throws
norm-ok
nokey-throws
