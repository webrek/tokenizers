--TEST--
Unigram::fromVocab encode/countTokens/decode/vocabSize
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Unigram;

/* Tiny vocab matching Task-1 unigram fixture:
   index = id, each element = [piece, score] */
$pieces = [
    ["\xE2\x96\x81he", -1.0],  /* id 0 */
    ["llo",            -1.0],  /* id 1 */
    ["\xE2\x96\x81",  -3.0],  /* id 2 */
    ["h",              -2.0],  /* id 3 */
    ["e",              -2.0],  /* id 4 */
    ["l",              -2.0],  /* id 5 */
    ["o",              -2.0],  /* id 6 */
    ["<unk>",         -10.0],  /* id 7 */
];
$ug = Unigram::fromVocab($pieces, ['unkId' => 7]);

echo implode(',', $ug->encode('hello')), "\n";  // 0,1
echo $ug->countTokens('hello'), "\n";           // 2
echo $ug->decode([0, 1]), "\n";                 // hello
echo $ug->vocabSize(), "\n";                    // 8
?>
--EXPECT--
0,1
2
hello
8
