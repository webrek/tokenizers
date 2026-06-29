<?php
namespace Tokenizers\Remote;
use Tokenizers\TokenizerException;
final class Anthropic {
    private string $version; private int $timeout; private ?string $apiKey; private Transport $transport;
    public function __construct(?string $apiKey = null, ?Transport $transport = null, string $version = '2023-06-01', int $timeout = 30) {
        $this->apiKey = $apiKey ?? (getenv('ANTHROPIC_API_KEY') ?: null);
        $this->transport = $transport ?? new CurlTransport();
        $this->version = $version; $this->timeout = $timeout;
    }
    /** @param string|array $messages plain string (one user turn) or a full messages array */
    public function countTokens(string $model, string|array $messages, ?string $system = null): int {
        if (!$this->apiKey) throw new TokenizerException('ANTHROPIC_API_KEY not set');
        $msgs = \is_string($messages) ? [['role'=>'user','content'=>$messages]] : $messages;
        $payload = ['model'=>$model, 'messages'=>$msgs];
        if ($system !== null) $payload['system'] = $system;
        $body = \json_encode($payload, \JSON_UNESCAPED_SLASHES | \JSON_UNESCAPED_UNICODE);
        if ($body === false) throw new TokenizerException('Anthropic count_tokens: failed to JSON-encode payload');
        $resp = $this->transport->post(
            'https://api.anthropic.com/v1/messages/count_tokens',
            ['x-api-key: '.$this->apiKey, 'anthropic-version: '.$this->version, 'content-type: application/json'],
            $body, $this->timeout
        );
        if ($resp['status'] < 200 || $resp['status'] >= 300) throw new TokenizerException("Anthropic count_tokens HTTP {$resp['status']}: {$resp['body']}");
        $j = \json_decode($resp['body'], true);
        if (!\is_array($j) || !isset($j['input_tokens'])) throw new TokenizerException('Anthropic count_tokens: missing input_tokens');
        return (int)$j['input_tokens'];
    }
}
