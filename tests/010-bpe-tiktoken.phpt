--TEST--
Bpe::fromTiktokenFile encode/decode/count round-trip
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Bpe;
$bpe = Bpe::fromTiktokenFile(__DIR__ . '/fixtures/mini.tiktoken', '\w+| +', []);
$ids = $bpe->encode('abc');
echo implode(',', $ids), "\n";          // expect 3,2  (ab + c)
echo $bpe->decode($ids), "\n";          // expect abc
echo $bpe->countTokens('abc'), "\n";    // expect 2
echo $bpe->vocabSize(), "\n";           // expect 5
// caching: loading the same file again does not add a cache entry
$n1 = tokenizers_cache_count();
$bpe2 = Bpe::fromTiktokenFile(__DIR__ . '/fixtures/mini.tiktoken', '\w+| +', []);
var_dump(tokenizers_cache_count() === $n1);
// FIX2: same id, different special NAME -> distinct cache entries
$base = tokenizers_cache_count();
Tokenizers\Bpe::fromTiktokenFile(__DIR__ . '/fixtures/mini.tiktoken', '\w+| +', ['<eos>' => 100]);
$m = tokenizers_cache_count();
Tokenizers\Bpe::fromTiktokenFile(__DIR__ . '/fixtures/mini.tiktoken', '\w+| +', ['<bos>' => 100]);
$n = tokenizers_cache_count();
var_dump($m === $base + 1 && $n === $m + 1);
// FIX3: object is uncloneable
try { clone $bpe; echo "cloned\n"; } catch (\Error $e) { echo "uncloneable\n"; }
?>
--EXPECT--
3,2
abc
2
5
bool(true)
bool(true)
uncloneable
