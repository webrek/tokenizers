--TEST--
Byte-exact Unigram tokenization vs transformers T5TokenizerFast (t5-small)
--SKIPIF--
<?php
if (!extension_loaded('tokenizers')) { echo 'skip'; exit; }
$f = __DIR__ . '/reference/fixtures/t5_tokenizer.json';
if (!is_file($f)) { echo 'skip t5_tokenizer.json fixture missing'; exit; }
$f2 = __DIR__ . '/reference/fixtures/unigram_conformance.json';
if (!is_file($f2)) { echo 'skip unigram_conformance.json fixture missing'; exit; }
?>
--FILE--
<?php
use Tokenizers\Encoding;
require __DIR__ . '/../php/Tokenizers/Encoding.php';

$ug   = Encoding::fromHuggingFace(__DIR__ . '/reference/fixtures/t5_tokenizer.json');
$fix  = json_decode(file_get_contents(__DIR__ . '/reference/fixtures/unigram_conformance.json'), true);
if (!is_array($fix) || count($fix) === 0) { echo "EMPTY FIXTURE\n"; exit(1); }
$fail = 0;

foreach ($fix as $i => $case) {
    $got = $ug->encode($case['text']);
    if ($got !== $case['ids']) {
        $fail++;
        echo "MISMATCH #$i text=" . json_encode($case['text']) . "\n";
        echo "  expected [" . implode(',', $case['ids']) . "]\n";
        echo "  got      [" . implode(',', $got) . "]\n";
    }
}
echo $fail === 0 ? "ALL CONFORMANT\n" : "FAILURES: $fail\n";
?>
--EXPECT--
ALL CONFORMANT
