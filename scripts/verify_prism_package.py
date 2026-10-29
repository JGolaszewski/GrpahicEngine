#!/usr/bin/env python3
"""Verify the installed PRISM release bundle, including models and tokenizer smoke test."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

from download_prism_models import load_manifest, verify_asset
from stage_prism_runtime import verify_runtime_inventory
from validate_prism_onnx_contract import validate_models


ROOT = Path(__file__).resolve().parents[1]
MODEL_MANIFEST = ROOT / "scripts" / "prism-model-assets.json"
RUNTIME_MANIFEST = ROOT / "scripts" / "prism-runtime-assets.json"
LICENSES = ROOT / "licenses" / "prism-models"


def verify(package: Path) -> None:
    binary = package / "bin"
    models = binary / "models" / "prism"
    manifest = load_manifest(MODEL_MANIFEST)
    expected_models = {asset["name"] for asset in manifest["assets"]}
    expected_models.update({"README.md", "prism-model-assets.json", "retriever.onnx.data", "vae_decoder.onnx.data",
                            "THIRD_PARTY_NOTICES.md",
                            "LICENSE-E5-MIT.txt", "LICENSE-CC-BY-SA-4.0.txt"})
    actual_models = {entry.name for entry in models.iterdir()} if models.is_dir() else set()
    if actual_models != expected_models:
        raise ValueError(f"packaged model files mismatch; missing={sorted(expected_models - actual_models)}, "
                         f"extra={sorted(actual_models - expected_models)}")
    for asset in manifest["assets"]:
        path = models / asset["name"]
        if not verify_asset(path, asset):
            raise ValueError(f"packaged model hash or byte size mismatch: {path}")
    validate_models(manifest, models)
    if hashlib.sha256((models / "retriever.onnx.data").read_bytes()).digest() != hashlib.sha256(
        (models / "prism_text_predictor_128.onnx.data").read_bytes()
    ).digest():
        raise ValueError("packaged predictor external-data alias differs from its pinned weights")
    if hashlib.sha256((models / "vae_decoder.onnx.data").read_bytes()).digest() != hashlib.sha256(
        (models / "prism_vae_decoder_128.onnx.data").read_bytes()
    ).digest():
        raise ValueError("packaged decoder external-data alias differs from its pinned weights")
    if (models / "prism-model-assets.json").read_bytes() != MODEL_MANIFEST.read_bytes():
        raise ValueError("packaged model manifest differs from the pinned source manifest")
    for name in ("THIRD_PARTY_NOTICES.md", "LICENSE-E5-MIT.txt", "LICENSE-CC-BY-SA-4.0.txt"):
        source_notice = (LICENSES / name).read_bytes()
        if (binary / "licenses" / "prism-models" / name).read_bytes() != source_notice:
            raise ValueError(f"packaged license notice differs from source: {name}")
        if (models / name).read_bytes() != source_notice:
            raise ValueError(f"packaged model notice differs from source: {name}")

    python = binary / "python"
    executable = python / "python.exe"
    runtime_manifest = json.loads(RUNTIME_MANIFEST.read_text(encoding="utf-8"))
    if (python / "PRISM-RUNTIME-MANIFEST.json").read_bytes() != RUNTIME_MANIFEST.read_bytes():
        raise ValueError("packaged Python runtime manifest differs from the pinned source manifest")
    verify_runtime_inventory(python)
    if hashlib.sha256((python / "prism_tokenizer_worker.py").read_bytes()).digest() != hashlib.sha256(
        (ROOT / "scripts" / "prism_tokenizer_worker.py").read_bytes()
    ).digest():
        raise ValueError("packaged tokenizer worker differs from source")
    versions = subprocess.run(
        [str(executable), "-c", "import sys, tokenizers; print(f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro} {tokenizers.__version__}')"],
        check=False, capture_output=True, text=True, timeout=30,
    )
    expected_versions = f"{runtime_manifest['python']['version']} {runtime_manifest['tokenizers']['version']}"
    if versions.returncode != 0 or versions.stdout.strip() != expected_versions:
        raise RuntimeError(f"packaged Python/tokenizers version check failed: {versions.stderr.strip() or versions.stdout.strip()}")
    worker = subprocess.run(
        [str(executable), str(python / "prism_tokenizer_worker.py")],
        input='{"id":1,"prompt":"brushed steel — warm light","directional":true}\n',
        check=False, capture_output=True, text=True, timeout=30,
    )
    lines = worker.stdout.splitlines()
    if worker.returncode != 0 or len(lines) != 2:
        raise RuntimeError(f"packaged tokenizer worker smoke check failed: {worker.stderr.strip() or worker.stdout.strip()}")
    ready, tokens = (json.loads(line) for line in lines)
    if (ready.get("type") != "ready" or not ready.get("ok") or ready.get("protocol") != 1
            or tokens.get("type") != "tokens" or tokens.get("id") != 1
            or tokens.get("directional") is not True
            or len(tokens.get("input_ids", [])) != 128
            or len(tokens.get("attention_mask", [])) != 128):
        raise ValueError("packaged tokenizer worker returned an invalid handshake or tensor payload")
    required_runtime_dlls = (
        "onnxruntime.dll",
        "onnxruntime_providers_shared.dll",
        "OpenImageIO.dll",
        "OpenImageIO_Util.dll",
    )
    missing_runtime_dlls = [name for name in required_runtime_dlls if not (binary / name).is_file()]
    if missing_runtime_dlls:
        raise FileNotFoundError(f"packaged runtime DLLs are missing from {binary}: {missing_runtime_dlls}")
    print("Packaged PRISM models, contracts, licenses, tokenizer, worker protocol, and runtime DLLs verified.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=ROOT / "out" / "package")
    arguments = parser.parse_args()
    try:
        verify(arguments.package.resolve())
    except (OSError, ValueError, RuntimeError, KeyError, json.JSONDecodeError) as error:
        print(f"PRISM package verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
