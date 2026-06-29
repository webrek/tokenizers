--TEST--
Encoding::fromHuggingFace loads a BPE tokenizer.json
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Encoding;
require __DIR__ . '/../php/Tokenizers/Encoding.php';
$b = Encoding::fromHuggingFace(__DIR__ . '/fixtures/mini_hf.json');
echo implode(',', $b->encode('ab')), "\n";  // 2
?>
--EXPECT--
2
