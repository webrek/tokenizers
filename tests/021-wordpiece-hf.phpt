--TEST--
Encoding::wordPieceFromVocabFile and fromHuggingFace WordPiece dispatch
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Encoding;
require __DIR__ . '/../php/Tokenizers/Encoding.php';

// Part 1: wordPieceFromVocabFile
$wp = Encoding::wordPieceFromVocabFile(__DIR__ . '/fixtures/mini_wp_vocab.txt');
echo implode(',', $wp->encode('unaffable')), "\n"; // 1,2,3
echo $wp->countTokens('zzz'), "\n";                 // 1

// Part 2: fromHuggingFace WordPiece dispatch
$wp2 = Encoding::fromHuggingFace(__DIR__ . '/fixtures/mini_wp.json');
echo implode(',', $wp2->encode('unaffable playing')), "\n"; // 1,2,3,4,5
?>
--EXPECT--
1,2,3
1
1,2,3,4,5
