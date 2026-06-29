--TEST--
Bpe::fromVocab builds a working model from raw-byte vocab
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Bpe;
$b = Bpe::fromVocab(['a' => 0, 'b' => 1, 'ab' => 2], [], '\w+', []);
echo implode(',', $b->encode('ab')), "\n";   // 2
echo $b->decode([2]), "\n";                    // ab
echo $b->vocabSize(), "\n";                     // 3
?>
--EXPECT--
2
ab
3
