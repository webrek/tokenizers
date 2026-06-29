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

    /** GPT-2 byte-level: map of unicode codepoint => raw byte. */
    private static function byteDecoder(): array
    {
        static $map = null;
        if ($map !== null) return $map;
        $bs = array_merge(range(0x21, 0x7E), range(0xA1, 0xAC), range(0xAE, 0xFF));
        $cs = $bs; $n = 0;
        for ($b = 0; $b < 256; $b++) { if (!in_array($b, $bs, true)) { $bs[] = $b; $cs[] = 256 + $n; $n++; } }
        $map = [];
        foreach ($bs as $i => $b) $map[$cs[$i]] = $b;
        return $map;
    }

    private static function utf8ord(string $ch): int
    {
        $ord = unpack('N', str_pad(mb_convert_encoding($ch, 'UTF-32BE', 'UTF-8'), 4, "\0", STR_PAD_LEFT));
        return $ord[1];
    }

    private static function tokenCharsToBytes(string $token): string
    {
        $dec = self::byteDecoder();
        $out = '';
        // iterate unicode codepoints of $token
        $cps = preg_split('//u', $token, -1, PREG_SPLIT_NO_EMPTY);
        $useIntl = extension_loaded('intl') && class_exists('IntlChar');
        foreach ($cps as $ch) {
            $cp = $useIntl ? (\IntlChar::ord($ch) ?? self::utf8ord($ch)) : self::utf8ord($ch);
            if (!isset($dec[$cp])) throw new TokenizerException("vocab token char U+" . dechex($cp) . " not in byte map");
            $out .= chr($dec[$cp]);
        }
        return $out;
    }

    public static function fromHuggingFace(string $jsonPath): Bpe
    {
        $raw = @file_get_contents($jsonPath);
        if ($raw === false) throw new TokenizerException("cannot read $jsonPath");
        $j = json_decode($raw, true);
        if (!is_array($j) || ($j['model']['type'] ?? null) !== 'BPE') {
            throw new TokenizerException("unsupported tokenizer.json (only model.type=BPE is supported in v1)");
        }
        $vocab = $j['model']['vocab'] ?? [];
        $rawVocab = [];
        foreach ($vocab as $token => $id) {
            $rawVocab[self::tokenCharsToBytes((string)$token)] = (int)$id;
        }
        $specials = [];
        foreach (($j['added_tokens'] ?? []) as $t) {
            if (!empty($t['special'])) $specials[(string)$t['content']] = (int)$t['id'];
        }
        // ByteLevel BPE uses the GPT-2 split regex
        $pattern = "(?i:'s|'t|'re|'ve|'m|'ll|'d)| ?\\p{L}+| ?\\p{N}+| ?[^\\s\\p{L}\\p{N}]+|\\s+(?!\\S)|\\s+";
        return Bpe::fromVocab($rawVocab, $j['model']['merges'] ?? [], $pattern, $specials);
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
