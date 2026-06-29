# Getting Started with `tokenizers`

A native PHP extension that counts, encodes, and decodes LLM tokens — byte-exact with the reference tokenizers — plus a pure-PHP companion that counts Claude/Gemini tokens via their official APIs.

---

## Requirements

| Requirement | Details |
|---|---|
| PHP | 8.3 or 8.4, NTS or ZTS |
| Build dep | `libpcre2-dev` (Debian/Ubuntu) / `brew install pcre2` (macOS); `pcre2-config` must be on `PATH` |
| Runtime dep | `libpcre2-8` |
| PHP extension | `ext-json` (bundled with PHP) |
| Remote companion only | `ext-curl` |

No Rust toolchain. No `ffi.enable`. Just a standard C PECL extension.

---

## Install from source (recommended / known-good)

This is the verified, working path. Use it unless you have a specific reason to prefer PECL or PIE.

```bash
git clone https://github.com/webrek/tokenizers.git
cd tokenizers

phpize
./configure
make
make install
```

Then enable the extension in your `php.ini`:

```ini
extension=tokenizers
```

To find which `php.ini` file is active:

```bash
php --ini
```

Look for the "Loaded Configuration File" line. Add `extension=tokenizers` there, or drop a file like `tokenizers.ini` into the `conf.d` directory listed under "Scan for additional .ini files in".

---

## Verify the install

```bash
php -m | grep tokenizers
```

You should see `tokenizers` in the output. For a version check:

```bash
php -r 'echo extension_loaded("tokenizers") ? \Tokenizers\VERSION : "not loaded";'
```

Expected output: `0.1.0`

---

## Install via PECL

The signed source package is attached to each GitHub release. Until the
extension is published on `pecl.php.net`, install it directly from the release
tarball:

```bash
pecl install https://github.com/webrek/tokenizers/releases/download/v0.1.0/tokenizers-0.1.0.tgz
```

Once it is published to the PECL channel, the short form will also work:

```bash
pecl install tokenizers
```

`pecl install` adds `extension=tokenizers` to your `php.ini` automatically.

---

## Install via PIE

PIE installs by Composer package name, not the bare extension name:

```bash
pie install webrek/tokenizers
```

For the development/unpublished version:

```bash
pie install webrek/tokenizers:*@dev
```

PIE reads the `php-ext` block in `composer.json` and runs `phpize` / `configure` / `make` / `make install` for you.

**Important:** End-to-end PIE installation has not yet been verified on a clean machine — the `pie` tool was not available in the development environment. The manifest is ready, but if you run into problems, fall back to the "Install from source" path above, which is the known-good method.

---

## Your first tokenization

```php
<?php
require_once __DIR__ . '/php/Tokenizers/Encoding.php';

use Tokenizers\Encoding;

// Load the cl100k_base encoding (used by GPT-4, GPT-4o text, o1, o3).
// On first use, the vocab file is downloaded from OpenAI's CDN,
// checksum-verified, and cached for future requests.
$enc = Encoding::load('cl100k_base');

// Count tokens without allocating the token array.
$n = $enc->countTokens('Hello, world!');
echo "Token count: $n\n";

// Encode to an array of integer token IDs.
$ids = $enc->encode('Hello world');
var_dump($ids); // array(2) { [0]=> int(9906) [1]=> int(1917) }

// Decode back to text (round-trip is exact).
$text = $enc->decode($ids);
echo $text . "\n"; // Hello world
```

Built-in encodings: `cl100k_base` (GPT-4 class) and `o200k_base` (GPT-4o multimodal, o1 mini/pro). To load any other model, use `Encoding::fromHuggingFace()` — see [guides/loading-models.md](guides/loading-models.md).

The vocab file is downloaded once per machine and cached. See [Troubleshooting](#troubleshooting) if you are in a network-restricted environment.

---

## Using it without the C extension (remote only)

The classes under `php/Tokenizers/Remote/` are pure PHP and work without the `.so` loaded. They require only `ext-curl` and `ext-json`.

```php
<?php
require_once __DIR__ . '/php/Tokenizers/TokenizerException.php'; // polyfill
require_once __DIR__ . '/php/Tokenizers/Remote/Http.php';        // Transport interface
require_once __DIR__ . '/php/Tokenizers/Remote/CurlTransport.php';
require_once __DIR__ . '/php/Tokenizers/Remote/Anthropic.php';

use Tokenizers\Remote\Anthropic;

// Reads ANTHROPIC_API_KEY from the environment.
$n = (new Anthropic())->countTokens('claude-opus-4-8', 'Hello, world!');
echo "Token count: $n\n";
```

For Gemini, replace `Anthropic` with `Gemini` (requires `GEMINI_API_KEY` or `GOOGLE_API_KEY`).

The `TokenCounter` facade can route automatically by model name:

```php
use Tokenizers\TokenCounter;
$tc = new TokenCounter();
$tc->count('cl100k_base', $text);      // local, no network
$tc->count('claude-opus-4-8', $text);  // remote Anthropic
$tc->count('gemini-1.5-flash', $text); // remote Gemini
```

See [guides/remote-providers.md](guides/remote-providers.md) for full setup details, key configuration, and honest limitations.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `php -m` does not list `tokenizers` | `extension=tokenizers` not added, wrong `php.ini`, or wrong SAPI ini (CLI vs FPM) | Run `php --ini` and confirm you edited the correct file; FPM/Apache use a separate ini |
| Build fails with "pcre2 not found" or "pcre2-config: command not found" | `libpcre2-dev` not installed, or `pcre2-config` not on `PATH` | macOS: `brew install pcre2`; Debian/Ubuntu: `apt-get install libpcre2-dev` |
| `TokenizerException: unknown encoding: <name>` | Only `cl100k_base` and `o200k_base` are built-in | Use `Encoding::fromHuggingFace($path)` with a HuggingFace `tokenizer.json` for other models |
| Network error on first `Encoding::load()` | Vocab download blocked by firewall or proxy | Set `TOKENIZERS_CACHE_DIR` to a writable directory, then pre-place the vocab file; or run in a network-permitted environment first |

Cache directory resolution order (first match wins):
1. `$TOKENIZERS_CACHE_DIR/tokenizers`
2. `$XDG_CACHE_HOME/tokenizers`
3. `$HOME/.cache/tokenizers`
4. `sys_get_temp_dir()/tokenizers`

---

## Next steps

- [api-reference.md](api-reference.md) — complete API for all classes and functions
- [guides/loading-models.md](guides/loading-models.md) — load OpenAI, HuggingFace BPE, WordPiece, and Unigram models
- [guides/estimating-costs.md](guides/estimating-costs.md) — estimate and budget LLM API costs before calling
- [guides/remote-providers.md](guides/remote-providers.md) — Claude/Gemini companion: setup, keys, usage, limitations
- [status.md](status.md) — version status, conformance results, limitations, roadmap
