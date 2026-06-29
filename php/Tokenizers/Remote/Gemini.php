<?php
namespace Tokenizers\Remote;
use Tokenizers\TokenizerException;
require_once __DIR__ . '/../TokenizerException.php';
final class Gemini {
    private int $timeout; private ?string $apiKey; private Transport $transport;
    public function __construct(?string $apiKey = null, ?Transport $transport = null, int $timeout = 30) {
        $this->apiKey = $apiKey ?? (getenv('GEMINI_API_KEY') ?: null) ?? (getenv('GOOGLE_API_KEY') ?: null);
        $this->transport = $transport ?? new CurlTransport();
        $this->timeout = $timeout;
    }
    public function countTokens(string $model, string $text): int {
        if (!$this->apiKey) throw new TokenizerException('GEMINI_API_KEY not set');
        // Normalize: strip leading "models/" if present, then re-prefix
        $id = \preg_replace('/^models\//', '', $model);
        $url = 'https://generativelanguage.googleapis.com/v1beta/models/' . $id . ':countTokens';
        $payload = ['contents' => [['parts' => [['text' => $text]]]]];
        $body = \json_encode($payload, \JSON_UNESCAPED_SLASHES | \JSON_UNESCAPED_UNICODE);
        if ($body === false) throw new TokenizerException('Gemini countTokens: failed to JSON-encode payload');
        $resp = $this->transport->post(
            $url,
            ['x-goog-api-key: ' . $this->apiKey, 'content-type: application/json'],
            $body, $this->timeout
        );
        if ($resp['status'] < 200 || $resp['status'] >= 300) throw new TokenizerException("Gemini countTokens HTTP {$resp['status']}: {$resp['body']}");
        $j = \json_decode($resp['body'], true);
        if (!\is_array($j) || !isset($j['totalTokens'])) throw new TokenizerException('Gemini countTokens: missing totalTokens');
        return (int)$j['totalTokens'];
    }
}
