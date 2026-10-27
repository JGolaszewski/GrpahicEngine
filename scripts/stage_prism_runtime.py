#!/usr/bin/env python3
"""Stage hash-pinned embedded CPython and the tokenizer into the release tree."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DESTINATION = ROOT / "out" / "prism-runtime"
RUNTIME_MANIFEST = ROOT / "scripts" / "prism-runtime-assets.json"


def digest(path: Path) -> tuple[int, str]:
    sha = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(block)
            size += len(block)
    return size, sha.hexdigest()


def fetch(entry: dict, path: Path) -> None:
    request = urllib.request.Request(entry["url"], headers={"User-Agent": "GraphicEngine-PRISM-release"})
    temp_path: Path | None = None
    try:
        with urllib.request.urlopen(request, timeout=60) as response, tempfile.NamedTemporaryFile(
            mode="wb", dir=path.parent, prefix=f".{path.name}.", suffix=".part", delete=False
        ) as output:
            temp_path = Path(output.name)
            sha = hashlib.sha256()
            size = 0
            while block := response.read(1024 * 1024):
                output.write(block)
                sha.update(block)
                size += len(block)
        if size != entry["size_bytes"] or sha.hexdigest() != entry["sha256"]:
            raise ValueError(f"download verification failed for {path.name}: {size} bytes, SHA-256 {sha.hexdigest()}")
        os.replace(temp_path, path)
        temp_path = None
    except (urllib.error.URLError, TimeoutError) as error:
        raise RuntimeError(f"download failed for {entry['url']}: {error}") from error
    finally:
        if temp_path is not None:
            temp_path.unlink(missing_ok=True)


def safe_extract(archive: Path, destination: Path) -> None:
    root = destination.resolve()
    with zipfile.ZipFile(archive) as source:
        for member in source.infolist():
            name = PurePosixPath(member.filename)
            if name.is_absolute() or ".." in name.parts:
                raise ValueError(f"unsafe archive member path: {member.filename}")
            target = (destination / Path(*name.parts)).resolve()
            if target != root and root not in target.parents:
                raise ValueError(f"archive member escapes destination: {member.filename}")
        source.extractall(destination)


def stage(destination: Path, manifest: dict, verify_only: bool) -> None:
    python_dir = destination / "python"
    python_exe = python_dir / "python.exe"
    sites = python_dir / "Lib" / "site-packages"
    tokenizer_package = sites / "tokenizers"
    if verify_only:
        if not python_exe.is_file() or not (tokenizer_package / "__init__.py").is_file():
            raise FileNotFoundError("embedded CPython or tokenizers package is missing from the staged runtime")
        result = subprocess.run(
            [str(python_exe), "-c", "import sys, tokenizers; print(f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro} {tokenizers.__version__}')"],
            check=False,
            capture_output=True,
            text=True,
        )
        expected_versions = f"{manifest['python']['version']} {manifest['tokenizers']['version']}"
        if result.returncode != 0 or result.stdout.strip() != expected_versions:
            raise RuntimeError(f"staged tokenizer version check failed: {result.stderr.strip() or result.stdout.strip()}")
        return

    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ge-prism-runtime-") as temporary:
        temp = Path(temporary)
        python_archive = temp / "python-embed.zip"
        wheel = temp / "tokenizers.whl"
        fetch(manifest["python"], python_archive)
        fetch(manifest["tokenizers"], wheel)
        python_dir.mkdir(parents=True, exist_ok=True)
        safe_extract(python_archive, python_dir)
        sites.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(wheel) as source:
            for member in source.infolist():
                relative = PurePosixPath(member.filename)
                if relative.parts and (relative.parts[0] == "tokenizers" or relative.parts[0].endswith(".dist-info")):
                    if relative.is_absolute() or ".." in relative.parts:
                        raise ValueError(f"unsafe tokenizer wheel member: {member.filename}")
                    target = (sites / Path(*relative.parts)).resolve()
                    if sites.resolve() not in target.parents and target != sites.resolve():
                        raise ValueError(f"wheel member escapes site-packages: {member.filename}")
                    source.extract(member, sites)

    pth_files = list(python_dir.glob("python*._pth"))
    if len(pth_files) != 1:
        raise RuntimeError(f"expected one embedded Python ._pth file, got {len(pth_files)}")
    stdlib_zips = list(python_dir.glob("python*.zip"))
    if len(stdlib_zips) != 1:
        raise RuntimeError(f"expected one embedded Python standard library archive, got {len(stdlib_zips)}")
    pth_files[0].write_text(f"{stdlib_zips[0].name}\n.\nLib/site-packages\nimport site\n", encoding="utf-8")
    worker_destination = python_dir / "prism_tokenizer_worker.py"
    shutil.copyfile(ROOT / "scripts" / "prism_tokenizer_worker.py", worker_destination)
    (python_dir / "PYTHON-README.txt").write_text(
        "Embedded CPython is used only to tokenize PRISM prompts. All ONNX model inference is performed by native C++ ONNX Runtime.\n",
        encoding="utf-8",
    )
    shutil.copyfile(RUNTIME_MANIFEST, python_dir / "PRISM-RUNTIME-MANIFEST.json")
    result = subprocess.run(
        [str(python_exe), "-c", "import sys, tokenizers; print(f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro} {tokenizers.__version__}')"],
        check=False,
        capture_output=True,
        text=True,
    )
    expected_versions = f"{manifest['python']['version']} {manifest['tokenizers']['version']}"
    if result.returncode != 0 or result.stdout.strip() != expected_versions:
        raise RuntimeError(f"staged embedded Python tokenizer smoke check failed: {result.stderr.strip() or result.stdout.strip()}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, default=DEFAULT_DESTINATION)
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    try:
        manifest = json.loads(RUNTIME_MANIFEST.read_text(encoding="utf-8"))
        if manifest.get("schema_version") != 1:
            raise ValueError("unsupported embedded runtime manifest schema")
        stage(args.destination.resolve(), manifest, args.verify_only)
    except (OSError, ValueError, RuntimeError, KeyError, zipfile.BadZipFile, urllib.error.URLError) as error:
        print(f"PRISM runtime staging failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
