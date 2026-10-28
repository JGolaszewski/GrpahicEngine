"""Protocol tests for the tokenizer-only worker using a deterministic stub tokenizer."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import sys
import types
import unittest
from pathlib import Path


WORKER_PATH = Path(__file__).resolve().parents[1] / "prism_tokenizer_worker.py"


class FakeEncoding:
    ids = [7] * 128
    attention_mask = [1] * 128


class FakeTokenizer:
    created_count = 0
    last_text = ""

    @staticmethod
    def from_file(_path: str) -> "FakeTokenizer":
        FakeTokenizer.created_count += 1
        return FakeTokenizer()

    def enable_truncation(self, *, max_length: int) -> None:
        self.max_length = max_length

    def enable_padding(self, *, length: int) -> None:
        self.padding_length = length

    def encode(self, text: str, *, add_special_tokens: bool) -> FakeEncoding:
        FakeTokenizer.last_text = text
        self.text = text
        self.add_special_tokens = add_special_tokens
        return FakeEncoding()


class PrismTokenizerWorkerTests(unittest.TestCase):
    def run_worker(self, requests: str) -> list[dict]:
        FakeTokenizer.created_count = 0
        FakeTokenizer.last_text = ""
        fake_module = types.ModuleType("tokenizers")
        fake_module.Tokenizer = FakeTokenizer
        previous_module = sys.modules.get("tokenizers")
        sys.modules["tokenizers"] = fake_module
        try:
            spec = importlib.util.spec_from_file_location("prism_tokenizer_worker_test", WORKER_PATH)
            assert spec is not None and spec.loader is not None
            worker = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(worker)
            output = io.StringIO()
            previous_stdin = sys.stdin
            sys.stdin = io.StringIO(requests)
            try:
                with contextlib.redirect_stdout(output):
                    self.assertEqual(worker.main(), 0)
            finally:
                sys.stdin = previous_stdin
            self.assertEqual(FakeTokenizer.created_count, 1)
            return [json.loads(line) for line in output.getvalue().splitlines()]
        finally:
            if previous_module is None:
                sys.modules.pop("tokenizers", None)
            else:
                sys.modules["tokenizers"] = previous_module

    def test_ready_and_tokens_protocol_preserves_request_and_directional(self) -> None:
        messages = self.run_worker(json.dumps({"id": 42, "prompt": "rough titanium", "directional": True}) + "\n")
        self.assertEqual(messages[0], {"type": "ready", "ok": True, "protocol": 1, "max_length": 128})
        response = messages[1]
        self.assertEqual(response["type"], "tokens")
        self.assertEqual(response["id"], 42)
        self.assertEqual(response["directional"], True)
        self.assertEqual(len(response["input_ids"]), 128)
        self.assertEqual(len(response["attention_mask"]), 128)
        self.assertEqual(FakeTokenizer.last_text, "query: rough titanium")

    def test_invalid_request_emits_correlated_error_and_worker_continues(self) -> None:
        requests = "\n".join(
            [
                "{malformed json",
                json.dumps({"id": 3, "prompt": "bad direction", "directional": "yes"}),
                json.dumps({"id": 4, "prompt": "valid", "directional": False}),
                "",
            ]
        )
        messages = self.run_worker(requests)
        self.assertEqual(messages[1]["type"], "error")
        self.assertIsNone(messages[1]["id"])
        self.assertEqual(messages[2]["type"], "error")
        self.assertEqual(messages[2]["id"], 3)
        self.assertIn("directional must be a boolean", messages[2]["error"])
        self.assertEqual(messages[3]["type"], "tokens")
        self.assertEqual(messages[3]["id"], 4)


if __name__ == "__main__":
    unittest.main()
