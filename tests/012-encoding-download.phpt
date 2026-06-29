--TEST--
Encoding::download fetches file:// and verifies checksum
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip'; ?>
--FILE--
<?php
use Tokenizers\Encoding;
require __DIR__ . '/../php/Tokenizers/Encoding.php';
$src = __DIR__ . '/fixtures/fake_cl.tiktoken';
$sha = hash_file('sha256', $src);
$dest = sys_get_temp_dir() . '/tk_test_' . getmypid() . '.tiktoken';
Encoding::download('file://' . $src, $sha, $dest);
var_dump(file_exists($dest));
// wrong checksum throws
try { Encoding::download('file://' . $src, str_repeat('0', 64), $dest . '.x'); echo "no throw\n"; }
catch (\Tokenizers\TokenizerException $e) { echo "checksum throws\n"; }
@unlink($dest);
?>
--EXPECT--
bool(true)
checksum throws
