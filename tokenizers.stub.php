<?php
/** @generate-class-entries */

namespace Tokenizers {
    /** @var string */
    const VERSION = "0.1.0";

    class TokenizerException extends \RuntimeException {}

    final class WordPiece {
        public static function fromVocab(array $tokenToId, array $opts = []): WordPiece {}
        public function encode(string $text): array {}
        public function countTokens(string $text): int {}
        public function decode(array $ids): string {}
        public function vocabSize(): int {}
    }

    final class Unigram {
        public static function fromVocab(array $pieces, array $opts = []): Unigram {}
        public function encode(string $text): array {}
        public function countTokens(string $text): int {}
        public function decode(array $ids): string {}
        public function vocabSize(): int {}
    }

    final class Bpe {
        public static function fromTiktokenFile(string $path, string $pattern, array $specialTokens = []): Bpe {}
        public static function fromVocab(array $tokenBytesToId, array $merges, string $pattern, array $specialTokens = []): Bpe {}
        public function encode(string $text, array|string $allowedSpecial = [], array|string $disallowedSpecial = "all"): array {}
        public function countTokens(string $text): int {}
        public function decode(array $ids): string {}
        public function decodeSingle(int $id): string {}
        public function vocabSize(): int {}
        public function name(): ?string {}
    }
}

namespace {
    function tokenizers_version(): string {}
    function tokenizers_cache_count(): int {}
    function tokenizers_encode(\Tokenizers\Bpe $t, string $text, array $allowedSpecial = [], array|string $disallowedSpecial = "all"): array {}
    function tokenizers_decode(\Tokenizers\Bpe $t, array $ids): string {}
    function tokenizers_count(\Tokenizers\Bpe $t, string $text): int {}
}
