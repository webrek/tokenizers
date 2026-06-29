--TEST--
Encoding::fromHuggingFace dispatches Unigram model.type
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Encoding;
require __DIR__ . '/../php/Tokenizers/Encoding.php';

$ug = Encoding::fromHuggingFace(__DIR__ . '/fixtures/mini_ug.json');
echo implode(',', $ug->encode('hello')), "\n"; // 0,1
echo $ug->vocabSize(), "\n";                   // 8
?>
--EXPECT--
0,1
8
