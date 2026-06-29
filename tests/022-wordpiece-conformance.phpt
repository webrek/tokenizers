--TEST--
Byte-exact WordPiece tokenization vs transformers BertTokenizerFast (bert-base-uncased)
--SKIPIF--
<?php
if (!extension_loaded('tokenizers')) { echo 'skip'; exit; }
$f = __DIR__ . '/reference/fixtures/bert_tokenizer.json';
if (!is_file($f)) { echo 'skip bert_tokenizer.json fixture missing'; exit; }
$f2 = __DIR__ . '/reference/fixtures/wordpiece_conformance.json';
if (!is_file($f2)) { echo 'skip wordpiece_conformance.json fixture missing'; exit; }
?>
--FILE--
<?php
use Tokenizers\Encoding;
require __DIR__ . '/../php/Tokenizers/Encoding.php';

$wp   = Encoding::fromHuggingFace(__DIR__ . '/reference/fixtures/bert_tokenizer.json');
$fix  = json_decode(file_get_contents(__DIR__ . '/reference/fixtures/wordpiece_conformance.json'), true);
if (!is_array($fix) || count($fix) === 0) { echo "EMPTY FIXTURE\n"; exit(1); }
$fail = 0;

foreach ($fix as $i => $case) {
    $got = $wp->encode($case['text']);
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
