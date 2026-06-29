<?php
namespace Tokenizers;

use Tokenizers\Remote\Anthropic;
use Tokenizers\Remote\Gemini;

final class TokenCounter
{
    private ?Anthropic $anthropic;
    private ?Gemini    $gemini;

    public function __construct(?Anthropic $anthropic = null, ?Gemini $gemini = null)
    {
        $this->anthropic = $anthropic;
        $this->gemini    = $gemini;
    }

    /**
     * Classify a model string into a provider name.
     * Returns 'anthropic', 'gemini', or 'local'.
     */
    public static function route(string $model): string
    {
        $m = strtolower($model);
        if (str_starts_with($m, 'claude') || str_starts_with($m, 'anthropic')) {
            return 'anthropic';
        }
        if (str_starts_with($m, 'gemini') || str_starts_with($m, 'models/gemini')) {
            return 'gemini';
        }
        return 'local';
    }

    /**
     * Count tokens for $text using the appropriate backend.
     *
     * @throws TokenizerException on unknown provider
     */
    public function count(string $model, string $text, ?string $provider = null): int
    {
        $provider ??= self::route($model);

        return match ($provider) {
            'anthropic' => ($this->anthropic ?? new Anthropic())->countTokens($model, $text),
            'gemini'    => ($this->gemini    ?? new Gemini())->countTokens($model, $text),
            'local'     => Encoding::load($model)->countTokens($text),
            default     => throw new TokenizerException("unknown provider for model: $model"),
        };
    }
}
