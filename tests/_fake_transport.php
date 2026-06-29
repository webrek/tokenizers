<?php
namespace Tokenizers\Remote;
final class FakeTransport implements Transport {
    public array $calls = [];
    public function __construct(private int $status, private string $respBody) {}
    public function post(string $url, array $headers, string $body, int $timeout): array {
        $this->calls[] = ['url'=>$url, 'headers'=>$headers, 'body'=>$body, 'timeout'=>$timeout];
        return ['status'=>$this->status, 'body'=>$this->respBody];
    }
}
