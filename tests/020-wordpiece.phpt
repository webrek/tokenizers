--TEST--
WordPiece::fromVocab encode/count
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\WordPiece;
$v = ['un'=>0,'##aff'=>1,'##able'=>2,'[UNK]'=>3,'play'=>4,'##ing'=>5];
$wp = WordPiece::fromVocab($v);
echo implode(',', $wp->encode('unaffable playing')), "\n"; // 0,1,2,4,5
echo $wp->countTokens('zzz'), "\n";                         // 1  ([UNK])
echo $wp->vocabSize(), "\n";                                 // 6
?>
--EXPECT--
0,1,2,4,5
1
6
