#!/usr/bin/env python3
"""Persistent JSON-lines tokenizer worker; ONNX inference remains native C++."""

from __future__ import annotations

import json
import sys
from pathlib import Path

from tokenizers import Tokenizer


TOKENIZER_PATH = Path(__file__).resolve().parents[1] / "models" / "prism" / "tokenizer.json"
MAX_LENGTH = 128


def main() -> int:
    try:
        tokenizer = Tokenizer.from_file(str(TOKENIZER_PATH))
        tokenizer.enable_truncation(max_length=MAX_LENGTH)
        tokenizer.enable_padding(length=MAX_LENGTH)
    except Exception as error:
        print(json.dumps({"type": "ready", "ok": False, "error": str(error)}), flush=True)
        return 2

    print(json.dumps({"type": "ready", "ok": True, "protocol": 1, "max_length": MAX_LENGTH}), flush=True)
    for line in sys.stdin:
        request_id = None
        try:
            request = json.loads(line)
            request_id = request.get("id")
            prompt = request.get("prompt")
            if not isinstance(prompt, str):
                raise ValueError("prompt must be a string")
            directional = request.get("directional", False)
            if not isinstance(directional, bool):
                raise ValueError("directional must be a boolean")
            # PRISM's E5 encoder expects query-prefixed text. The directional
            # flag is deliberately forwarded to native retriever inference,
            # not inserted into the text prompt.
            encoding = tokenizer.encode("query: " + prompt, add_special_tokens=True)
            response = {
                "type": "tokens",
                "id": request_id,
                "input_ids": encoding.ids,
                "attention_mask": encoding.attention_mask,
                "directional": directional,
            }
            if len(response["input_ids"]) != MAX_LENGTH or len(response["attention_mask"]) != MAX_LENGTH:
                raise RuntimeError("tokenizer did not produce fixed length 128 tensors")
            print(json.dumps(response, separators=(",", ":")), flush=True)
        except Exception as error:
            print(json.dumps({"type": "error", "id": locals().get("request_id"), "error": str(error)}), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
