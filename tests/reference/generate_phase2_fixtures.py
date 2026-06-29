#!/usr/bin/env python3
"""
Generate WordPiece conformance fixtures vs bert-base-uncased.

Requires: pip install transformers tokenizers
Run from the repo root: python3 tests/reference/generate_phase2_fixtures.py

Outputs (both committed as test fixtures):
  tests/reference/fixtures/bert_tokenizer.json
  tests/reference/fixtures/wordpiece_conformance.json

Case selection stays within documented v1 normalization coverage:
  ASCII + Latin-1 Supplement lowercase & accent-strip,
  ASCII punctuation isolation, CJK ideograph spacing,
  control/whitespace cleanup.
Exotic scripts needing full NFD (Arabic, Devanagari, etc.) are excluded.
"""
import json, os
from transformers import BertTokenizerFast

FIXTURE_DIR = os.path.join(os.path.dirname(__file__), "fixtures")
os.makedirs(FIXTURE_DIR, exist_ok=True)

# Load bert-base-uncased (downloads ~500 KB tokenizer.json if not cached)
tok = BertTokenizerFast.from_pretrained("bert-base-uncased")

# (a) Save the raw HuggingFace tokenizer.json so PHP can load it
bert_json_path = os.path.join(FIXTURE_DIR, "bert_tokenizer.json")
tok.backend_tokenizer.save(bert_json_path)
print(f"wrote {bert_json_path}")

# (b) Curated cases — within documented v1 normalization coverage only.
# Excludes: Arabic, Devanagari, Thai, Hebrew, Hangul, etc. (need full NFD).
CASES = [
    # empty / whitespace
    "",
    " ",
    "   ",
    "\t\n",
    # plain English words
    "hello",
    "world",
    "hello world",
    "tokenization",
    # mixed case (normalizer lowercases)
    "Hello",
    "WORLD",
    "Hello World",
    "CamelCase",
    # punctuation — ASCII: commas, periods, parens, quotes, hyphens
    "hello, world.",
    "Hello, World!",
    "It's a test.",
    "(parentheses)",
    "double--hyphen",
    '"quoted"',
    "semi;colon:test",
    # numbers
    "123",
    "42.5",
    "1000000",
    # subword splitting
    "unaffable",
    "tokenization",
    "unhappy",
    "preprocessing",
    # accented Latin (U+00C0–U+00FF range — in-scope)
    "café",
    "naïve",
    "résumé",
    "Zürich",
    "façade",
    "über",
    "El niño",
    "cliché",
    # simple CJK (U+4E00–U+9FFF — in-scope via CJK spacing)
    "你好",
    "世界",
    "你好世界",
    # mixed English + CJK
    "hello 世界",
    # whitespace runs
    "hello   world",
    "  leading spaces",
    "trailing spaces  ",
    # longer sentence
    "The quick brown fox jumps over the lazy dog.",
    "Hello, how are you today?",
    "I love machine learning and natural language processing.",
]

out = []
for text in CASES:
    ids = tok.encode(text, add_special_tokens=False)
    out.append({"text": text, "ids": ids})

conf_path = os.path.join(FIXTURE_DIR, "wordpiece_conformance.json")
with open(conf_path, "w", encoding="utf-8") as f:
    json.dump(out, f, ensure_ascii=False, indent=0)
    f.write("\n")

print(f"wrote {conf_path} ({len(out)} cases)")
