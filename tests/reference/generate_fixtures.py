#!/usr/bin/env python3
# Requires: pip install tiktoken
import json, os, tiktoken

CASES = [
    "", " ", "\n", "hello world", "Hello, World!",
    "Hola, ¿cómo estás?", "ünïcödé NFC", "naïve café",
    "👍🏽 multi-byte emoji 🇲🇽", "a" * 1000, "ab" * 500,
    "123 4567 89", "  leading and  double  spaces ",
    "tabs\tand\tnewlines\n\n", "<|endoftext|> as text",
    "snake_case CamelCase kebab-case", "function foo(){ return 42; }",
    "El rápido zorro marrón salta sobre el perro perezoso.",
]
out = {}
for name in ["cl100k_base", "o200k_base"]:
    enc = tiktoken.get_encoding(name)
    out[name] = [{"text": c, "ids": enc.encode(c, disallowed_special=())} for c in CASES]
os.makedirs("tests/reference/fixtures", exist_ok=True)
with open("tests/reference/fixtures/conformance.json", "w") as f:
    json.dump(out, f, ensure_ascii=False, indent=0)
    f.write("\n")
print("wrote tests/reference/fixtures/conformance.json")
