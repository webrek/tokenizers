--TEST--
Pathological long single token does not hang (O(n log n))
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Bpe;
// vocab of single byte 'a' plus 'aa' forces repeated merges; 200k chars must finish fast
$vocab = ['a' => 0, 'aa' => 1];
$b = Bpe::fromVocab($vocab, [], '\w+', []);
$t = str_repeat('a', 200000);
$start = microtime(true);
$n = $b->countTokens($t);
$elapsed = microtime(true) - $start;
var_dump($n === 100000);          // 200k 'a' -> 100k 'aa'
var_dump($elapsed < 2.0);          // must not be quadratic
?>
--EXPECT--
bool(true)
bool(true)
