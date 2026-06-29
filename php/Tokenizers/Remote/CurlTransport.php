<?php
namespace Tokenizers\Remote;
use Tokenizers\TokenizerException;
final class CurlTransport implements Transport {
    public function post(string $url, array $headers, string $body, int $timeout): array {
        if (!\function_exists('curl_init')) throw new TokenizerException('ext-curl is required for remote token counting');
        $ch = \curl_init($url);
        \curl_setopt_array($ch, [
            \CURLOPT_POST => true,
            \CURLOPT_POSTFIELDS => $body,
            \CURLOPT_HTTPHEADER => $headers,
            \CURLOPT_RETURNTRANSFER => true,
            \CURLOPT_TIMEOUT => $timeout,
            \CURLOPT_CONNECTTIMEOUT => $timeout,
        ]);
        $resp = \curl_exec($ch);
        if ($resp === false) { $e = \curl_error($ch); \curl_close($ch); throw new TokenizerException("HTTP transport error: $e"); }
        $status = (int)\curl_getinfo($ch, \CURLINFO_HTTP_CODE);
        \curl_close($ch);
        return ['status'=>$status, 'body'=>(string)$resp];
    }
}
