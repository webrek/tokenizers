--TEST--
Extension loads and reports its version
--SKIPIF--
<?php if (!extension_loaded('tokenizers')) echo 'skip extension not built'; ?>
--FILE--
<?php
var_dump(extension_loaded('tokenizers'));
var_dump(tokenizers_version() === \Tokenizers\VERSION);
echo \Tokenizers\VERSION, "\n";
?>
--EXPECT--
bool(true)
bool(true)
0.1.0
