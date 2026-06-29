--TEST--
Procedural helpers operate on a Bpe instance
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Bpe;
$b = Bpe::fromTiktokenFile(__DIR__ . '/fixtures/mini.tiktoken', '\w+| +', []);
$ids = tokenizers_encode($b, 'abc');
echo implode(',', $ids), "\n";        // 3,2
echo tokenizers_count($b, 'abc'), "\n"; // 2
echo tokenizers_decode($b, $ids), "\n"; // abc
?>
--EXPECT--
3,2
2
abc
