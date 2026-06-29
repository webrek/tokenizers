<?php
namespace Tokenizers;

/*
 * The C extension registers \Tokenizers\TokenizerException at module init.
 * Define a pure-PHP equivalent only when the extension is NOT loaded, so the
 * remote companion (Remote\Anthropic / Remote\Gemini / TokenCounter) works
 * standalone — counting Claude/Gemini tokens without compiling the extension.
 * When the extension is present, its class already exists and this is a no-op.
 */
if (!\class_exists(TokenizerException::class, false)) {
    class TokenizerException extends \RuntimeException {}
}
