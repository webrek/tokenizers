<?php
namespace Tokenizers\Remote;
interface Transport {
    /** @return array{status:int, body:string} */
    public function post(string $url, array $headers, string $body, int $timeout): array;
}
