<?php
namespace Tokenizers;

final class Encoding
{
    private const CL100K_PATTERN =
        "(?i:'s|'t|'re|'ve|'m|'ll|'d)|[^\\r\\n\\p{L}\\p{N}]?\\p{L}+|\\p{N}{1,3}| ?[^\\s\\p{L}\\p{N}]+[\\r\\n]*|\\s*[\\r\\n]+|\\s+(?!\\S)|\\s+";
    private const O200K_PATTERN =
        "[^\\r\\n\\p{L}\\p{N}]?[\\p{Lu}\\p{Lt}\\p{Lm}\\p{Lo}\\p{M}]*[\\p{Ll}\\p{Lm}\\p{Lo}\\p{M}]+(?i:'s|'t|'re|'ve|'m|'ll|'d)?|".
        "[^\\r\\n\\p{L}\\p{N}]?[\\p{Lu}\\p{Lt}\\p{Lm}\\p{Lo}\\p{M}]+[\\p{Ll}\\p{Lm}\\p{Lo}\\p{M}]*(?i:'s|'t|'re|'ve|'m|'ll|'d)?|".
        "\\p{N}{1,3}| ?[^\\s\\p{L}\\p{N}]+[\\r\\n/]*|\\s*[\\r\\n]+|\\s+(?!\\S)|\\s+";

    /** name => [url, sha256, pattern, specials]. sha256 may be null to skip verification. */
    private static function registry(): array
    {
        return [
            'cl100k_base' => [
                'url' => 'https://openaipublic.blob.core.windows.net/encodings/cl100k_base.tiktoken',
                'sha256' => '223921b76ee99bde995b7ff738513eef100fb51d18c93597a113bcffe865b2a7',
                'pattern' => self::CL100K_PATTERN,
                'specials' => [
                    '<|endoftext|>' => 100257, '<|fim_prefix|>' => 100258, '<|fim_middle|>' => 100259,
                    '<|fim_suffix|>' => 100260, '<|endofprompt|>' => 100276,
                ],
            ],
            'o200k_base' => [
                'url' => 'https://openaipublic.blob.core.windows.net/encodings/o200k_base.tiktoken',
                'sha256' => '446a9538cb6c348e3516120d7c08b09f57c36495e2acfffe59a5bf8b0cfb1a2d',
                'pattern' => self::O200K_PATTERN,
                'specials' => ['<|endoftext|>' => 199999, '<|endofprompt|>' => 200018],
            ],
        ];
    }

    public static function cacheDir(): string
    {
        $base = getenv('TOKENIZERS_CACHE_DIR')
            ?: (getenv('XDG_CACHE_HOME') ?: (getenv('HOME') ? getenv('HOME') . '/.cache' : sys_get_temp_dir()));
        $dir = rtrim($base, '/') . '/tokenizers';
        if (!is_dir($dir) && !@mkdir($dir, 0775, true) && !is_dir($dir)) {
            throw new TokenizerException("cannot create cache dir: $dir");
        }
        return $dir;
    }

    public static function download(string $url, ?string $sha256, string $dest): void
    {
        $data = @file_get_contents($url);
        if ($data === false) throw new TokenizerException("download failed: $url");
        if ($sha256 !== null && !hash_equals(strtolower($sha256), hash('sha256', $data))) {
            throw new TokenizerException("checksum mismatch for $url");
        }
        $tmp = $dest . '.' . getmypid() . '.part';
        if (@file_put_contents($tmp, $data) === false || !@rename($tmp, $dest)) {
            @unlink($tmp);
            throw new TokenizerException("cannot write cache file: $dest");
        }
    }

    public static function load(string $name): Bpe
    {
        $reg = self::registry();
        if (!isset($reg[$name])) throw new TokenizerException("unknown encoding: $name");
        $e = $reg[$name];
        $path = self::cacheDir() . '/' . $name . '.tiktoken';
        if (!is_file($path)) self::download($e['url'], $e['sha256'], $path);
        return Bpe::fromTiktokenFile($path, $e['pattern'], $e['specials']);
    }
}
