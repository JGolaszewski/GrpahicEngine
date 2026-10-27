#!/usr/bin/env python3
"""Stage pinned PRISM ONNX release assets with SHA-256 verification."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
import urllib.error
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "scripts" / "prism-model-assets.json"
LICENSES = ROOT / "licenses" / "prism-models"
PRISM_RELEASE = "https://github.com/JGolaszewski/PRISM/releases/download"
NOTICE_FILES = (
    "README.md",
    "THIRD_PARTY_NOTICES.md",
    "LICENSE-E5-MIT.txt",
    "LICENSE-CC-BY-SA-4.0.txt",
)


def load_manifest(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != 2 or not isinstance(data.get("assets"), list):
        raise ValueError(f"unsupported or malformed model manifest: {path}")
    names: set[str] = set()
    for asset in data["assets"]:
        name = asset.get("name")
        if not isinstance(name, str) or Path(name).name != name or name in names:
            raise ValueError(f"invalid or duplicate asset filename: {name!r}")
        if asset.get("release_tag") not in {"e5-onnx-v1.0.0", "prism-onnx-v1.0.0"}:
            raise ValueError(f"unapproved upstream release for {name}")
        if not isinstance(asset.get("size_bytes"), int) or asset["size_bytes"] < 0:
            raise ValueError(f"invalid expected size for {name}")
        digest = asset.get("sha256")
        if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise ValueError(f"invalid SHA-256 for {name}")
        names.add(name)
    required = {
        "e5_encoder.onnx", "e5_encoder.onnx.data", "special_tokens_map.json",
        "tokenizer.json", "tokenizer_config.json", "vocab.txt", "retriever.onnx",
        "retriever.onnx.data", "vae_decoder.onnx", "vae_decoder.onnx.data",
    }
    if names != required:
        raise ValueError(f"manifest asset set mismatch; missing={sorted(required - names)}, extra={sorted(names - required)}")
    return data


def digest_file(path: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
            size += len(chunk)
    return size, digest.hexdigest()


def verify_asset(path: Path, asset: dict) -> bool:
    if not path.is_file():
        return False
    size, digest = digest_file(path)
    return size == asset["size_bytes"] and digest == asset["sha256"]


def download_asset(asset: dict, destination: Path) -> None:
    url = f"{PRISM_RELEASE}/{asset['release_tag']}/{asset['name']}"
    request = urllib.request.Request(url, headers={"User-Agent": "GraphicEngine-PRISM-model-packager"})
    temporary_name: str | None = None
    try:
        with urllib.request.urlopen(request, timeout=60) as response, tempfile.NamedTemporaryFile(
            mode="wb", dir=destination, prefix=f".{asset['name']}.", suffix=".part", delete=False
        ) as output:
            temporary_name = output.name
            size = 0
            digest = hashlib.sha256()
            while chunk := response.read(1024 * 1024):
                output.write(chunk)
                size += len(chunk)
                digest.update(chunk)
        if size != asset["size_bytes"] or digest.hexdigest() != asset["sha256"]:
            raise ValueError(
                f"verification failed for {asset['name']}: got {size} bytes, SHA-256 {digest.hexdigest()}"
            )
        os.replace(temporary_name, destination / asset["name"])
        temporary_name = None
        print(f"verified {asset['name']} ({size:,} bytes)")
    except (urllib.error.URLError, TimeoutError) as error:
        raise RuntimeError(f"download failed for {asset['name']} from {url}: {error}") from error
    finally:
        if temporary_name is not None:
            Path(temporary_name).unlink(missing_ok=True)


def stage_notices(destination: Path, manifest_path: Path) -> None:
    for filename in NOTICE_FILES[1:]:
        source = LICENSES / filename
        if not source.is_file():
            raise FileNotFoundError(f"required model notice is missing: {source}")
        shutil.copyfile(source, destination / filename)
    shutil.copyfile(manifest_path, destination / "prism-model-assets.json")
    readme = """# PRISM ONNX model assets

These files are the pinned PRISM v1.0.0 ONNX release assets. The E5 encoder
weights and tokenizer come from `e5-onnx-v1.0.0`; the PRISM retriever and
decoder come from `prism-onnx-v1.0.0`. Keep each `.onnx.data` file beside its
matching ONNX graph. The model package is approximately 1.35 GB.

The checked-in `prism-model-assets.json` records upstream revisions, expected
sizes, and SHA-256 digests. See `THIRD_PARTY_NOTICES.md` and the adjacent
license files for attribution and asset-specific terms. The app bundle stages
ONNX Runtime CPU libraries for native C++ inference services. Embedded CPython
is restricted to text tokenization.
"""
    (destination / "README.md").write_text(readme, encoding="utf-8", newline="\n")


def validate_notice_sources() -> None:
    missing = [str(LICENSES / name) for name in NOTICE_FILES[1:] if not (LICENSES / name).is_file()]
    if missing:
        raise FileNotFoundError("required model notices are missing: " + ", ".join(missing))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, default=ROOT / "out" / "prism-runtime" / "models" / "prism")
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--verify-only", action="store_true", help="verify staged assets without downloading")
    arguments = parser.parse_args()

    try:
        manifest = load_manifest(arguments.manifest)
        validate_notice_sources()
        destination = arguments.destination.resolve()
        destination.mkdir(parents=True, exist_ok=True)
        failures: list[str] = []
        for asset in manifest["assets"]:
            path = destination / asset["name"]
            if verify_asset(path, asset):
                print(f"verified {asset['name']} (already staged)")
            elif arguments.verify_only:
                failures.append(f"missing or invalid asset: {path}")
            else:
                download_asset(asset, destination)
        if failures:
            raise ValueError("\n".join(failures))
        stage_notices(destination, arguments.manifest)
    except (OSError, ValueError, RuntimeError, urllib.error.URLError) as error:
        print(f"PRISM model staging failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
