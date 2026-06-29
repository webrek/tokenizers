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
?>
--EXPECT--
3,2
abc
2
5
bool(true)
