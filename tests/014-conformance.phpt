--TEST--
Byte-exact tokenization vs the tiktoken reference (cl100k_base, o200k_base)
--SKIPIF--
<?php
if (!extension_loaded('tokenizers')) { echo 'skip'; exit; }
require __DIR__ . '/../php/Tokenizers/Encoding.php';
try { foreach (['cl100k_base', 'o200k_base'] as $n) \Tokenizers\Encoding::load($n); }
catch (\Throwable $e) { echo 'skip vocab unavailable (offline)'; }
?>
--FILE--
<?php
use Tokenizers\Encoding;
require __DIR__ . '/../php/Tokenizers/Encoding.php';
$fix = json_decode(file_get_contents(__DIR__ . '/reference/fixtures/conformance.json'), true);
$fail = 0;
foreach (['cl100k_base', 'o200k_base'] as $name) {
    $enc = Encoding::load($name);
    foreach ($fix[$name] as $i => $case) {
        $got = $enc->encode($case['text'], [], []); // [] disallowed => specials-as-ordinary, matches python disallowed_special=()
        if ($got !== $case['ids']) {
            $fail++;
            echo "MISMATCH $name#$i text=" . json_encode($case['text']) . "\n";
            echo "  expected " . implode(',', $case['ids']) . "\n  got      " . implode(',', $got) . "\n";
        }
        if ($enc->decode($got) !== $case['text']) { $fail++; echo "DECODE MISMATCH $name#$i\n"; }
        if ($enc->countTokens($case['text']) !== count($case['ids'])) { $fail++; echo "COUNT MISMATCH $name#$i\n"; }
    }
}
echo $fail === 0 ? "ALL CONFORMANT\n" : "FAILURES: $fail\n";
?>
--EXPECT--
ALL CONFORMANT
