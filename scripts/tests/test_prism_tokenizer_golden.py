"""Golden token vectors for the pinned E5 tokenizer (requires staged tokenizer.json)."""

from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import io
import json
import os
import struct
import sys
import unittest
from pathlib import Path


TOKENIZER_PATH = os.environ.get("ENGINE_PRISM_TOKENIZER_JSON")
WORKER_PATH = Path(__file__).resolve().parents[1] / "prism_tokenizer_worker.py"
HAS_TOKENIZERS = importlib.util.find_spec("tokenizers") is not None


@unittest.skipUnless(TOKENIZER_PATH and HAS_TOKENIZERS, "set ENGINE_PRISM_TOKENIZER_JSON and install pinned tokenizers")
class PrismTokenizerGoldenTests(unittest.TestCase):
    def test_query_unicode_punctuation_and_truncation_vectors(self) -> None:
        spec = importlib.util.spec_from_file_location("prism_tokenizer_golden_worker", WORKER_PATH)
        assert spec is not None and spec.loader is not None
        worker = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(worker)
        worker.TOKENIZER_PATH = Path(TOKENIZER_PATH)
        prompts = [
            ("rough brushed titanium", False),
            ("brushed café—steel 🟦", True),
            ("a/b, C++ (matte)!", False),
            ("fine grain " * 150, True),
        ]
        request_lines = "".join(
            json.dumps({"id": index + 1, "prompt": prompt, "directional": directional}) + "\n"
            for index, (prompt, directional) in enumerate(prompts)
        )
        old_stdin = sys.stdin
        output = io.StringIO()
        sys.stdin = io.StringIO(request_lines)
        try:
            with contextlib.redirect_stdout(output):
                self.assertEqual(worker.main(), 0)
        finally:
            sys.stdin = old_stdin

        messages = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(messages[0]["type"], "ready")
        expected = [
            ("06d63c063228a81198919ab5bc0fd30599da3c06433a1650760f7200ad2831ac",
             "b9291b391a04b728bd580c77218ff5b44a9f5518c5c2e0b6857304c2da0efbfc", 7),
            ("756f1ea2b52f26be6845fc1fd40eb2d7033b9fe6f0e014950b116db1558d6040",
             "9e00fc38d7fb7994ddadbf0f44e6f35386845dd66a4825fd7a0bb8e32bcd1c7a", 9),
            ("2b59ccbe93d4f4f7060934b31d6cf476c50a75f524f4b9d374eff9e01142d50f",
             "5de7ed6de5ce07b001484a81d092c2da046eaa6bfc34542f01aff0c303db794b", 16),
            ("92caa163c5e0983980bf599c3029e127e99432ac379dd30ae86d1d6f82bb6714",
             "01476cf1a34744ed1f8b738f63b753c898716ce9a63b8410a896ab522d5cfc51", 128),
        ]
        for index, ((_, directional), message, (ids_hash, mask_hash, active_tokens)) in enumerate(
            zip(prompts, messages[1:], expected, strict=True)
        ):
            self.assertEqual(message["id"], index + 1)
            self.assertEqual(message["directional"], directional)
            self.assertEqual(len(message["input_ids"]), 128)
            self.assertEqual(sum(message["attention_mask"]), active_tokens)
            ids_bytes = struct.pack("<128q", *message["input_ids"])
            mask_bytes = struct.pack("<128q", *message["attention_mask"])
            self.assertEqual(hashlib.sha256(ids_bytes).hexdigest(), ids_hash)
            self.assertEqual(hashlib.sha256(mask_bytes).hexdigest(), mask_hash)


if __name__ == "__main__":
    unittest.main()
