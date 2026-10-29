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
from prism_staging import replace_directory, temporary_sibling


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DESTINATION = ROOT / "out" / "prism-runtime"
RUNTIME_MANIFEST = ROOT / "scripts" / "prism-runtime-assets.json"
CONTENT_MANIFEST_NAME = "PRISM-RUNTIME-CONTENTS.json"


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


def runtime_file_inventory(python_dir: Path) -> list[dict[str, object]]:
    inventory = []
    for path in sorted(python_dir.rglob("*")):
        if path.is_file() and path.name != CONTENT_MANIFEST_NAME:
            digest = hashlib.sha256()
            size = 0
            with path.open("rb") as stream:
                while block := stream.read(1024 * 1024):
                    digest.update(block)
                    size += len(block)
            inventory.append({"path": path.relative_to(python_dir).as_posix(), "size_bytes": size,
                              "sha256": digest.hexdigest()})
    return inventory


def verify_runtime_inventory(python_dir: Path) -> None:
    path = python_dir / CONTENT_MANIFEST_NAME
    expected = json.loads(path.read_text(encoding="utf-8"))
    actual_files = runtime_file_inventory(python_dir)
    if expected.get("schema_version") != 1 or expected.get("files") != actual_files:
        expected_files = expected.get("files", [])
        for index, (expected_file, actual_file) in enumerate(zip(expected_files, actual_files)):
            if expected_file != actual_file:
                raise ValueError(f"embedded runtime inventory differs at {expected_file.get('path')}: "
                                 f"expected {expected_file}, got {actual_file}")
        raise ValueError(f"embedded runtime inventory file count differs: expected {len(expected_files)}, got {len(actual_files)}")


def smoke_test(python_dir: Path, manifest: dict) -> None:
    python_exe = python_dir / "python.exe"
    worker_path = python_dir / "prism_tokenizer_worker.py"
    if (not python_exe.is_file() or not worker_path.is_file()
            or not (python_dir / "Lib" / "site-packages" / "tokenizers" / "__init__.py").is_file()):
        raise FileNotFoundError("embedded CPython, tokenizers, or tokenizer worker is missing")
    if (python_dir / "PRISM-RUNTIME-MANIFEST.json").read_bytes() != RUNTIME_MANIFEST.read_bytes():
        raise ValueError("staged runtime manifest differs from the pinned source manifest")
    result = subprocess.run(
        [str(python_exe), "-c", "import sys, tokenizers; print(f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro} {tokenizers.__version__}')"],
        check=False, capture_output=True, text=True, timeout=30,
    )
    expected_versions = f"{manifest['python']['version']} {manifest['tokenizers']['version']}"
    if result.returncode != 0 or result.stdout.strip() != expected_versions:
        raise RuntimeError(f"staged tokenizer version check failed: {result.stderr.strip() or result.stdout.strip()}")
    worker = subprocess.run(
        [str(python_exe), str(worker_path)],
        input='{"id":1,"prompt":"brushed steel — warm light","directional":true}\n',
        check=False, capture_output=True, text=True, timeout=30,
    )
    lines = worker.stdout.splitlines()
    if worker.returncode != 0 or len(lines) != 2:
        raise RuntimeError(f"staged tokenizer worker smoke check failed: {worker.stderr.strip() or worker.stdout.strip()}")
    ready, tokens = (json.loads(line) for line in lines)
    if (ready.get("type") != "ready" or not ready.get("ok") or ready.get("protocol") != 1
            or tokens.get("type") != "tokens" or tokens.get("id") != 1
            or tokens.get("directional") is not True
            or len(tokens.get("input_ids", [])) != 128
            or len(tokens.get("attention_mask", [])) != 128):
        raise RuntimeError("staged tokenizer worker returned an invalid handshake or tensor payload")


def stage(destination: Path, manifest: dict, verify_only: bool) -> None:
    python_dir = destination / "python"
    if verify_only:
        smoke_test(python_dir, manifest)
        verify_runtime_inventory(python_dir)
        return

    destination.mkdir(parents=True, exist_ok=True)
    staged_python = temporary_sibling(python_dir)
    try:
        with tempfile.TemporaryDirectory(prefix="ge-prism-runtime-") as temporary:
            temp = Path(temporary)
            python_archive = temp / "python-embed.zip"
            wheel = temp / "tokenizers.whl"
            fetch(manifest["python"], python_archive)
            fetch(manifest["tokenizers"], wheel)
            safe_extract(python_archive, staged_python)
            staged_sites = staged_python / "Lib" / "site-packages"
            staged_sites.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(wheel) as source:
                for member in source.infolist():
                    relative = PurePosixPath(member.filename)
                    if relative.parts and (relative.parts[0] == "tokenizers" or relative.parts[0].endswith(".dist-info")):
                        if relative.is_absolute() or ".." in relative.parts:
                            raise ValueError(f"unsafe tokenizer wheel member: {member.filename}")
                        target = (staged_sites / Path(*relative.parts)).resolve()
                        if staged_sites.resolve() not in target.parents and target != staged_sites.resolve():
                            raise ValueError(f"wheel member escapes site-packages: {member.filename}")
                        source.extract(member, staged_sites)

        pth_files = list(staged_python.glob("python*._pth"))
        if len(pth_files) != 1:
            raise RuntimeError(f"expected one embedded Python ._pth file, got {len(pth_files)}")
        stdlib_zips = list(staged_python.glob("python*.zip"))
        if len(stdlib_zips) != 1:
            raise RuntimeError(f"expected one embedded Python standard library archive, got {len(stdlib_zips)}")
        pth_files[0].write_text(f"{stdlib_zips[0].name}\n.\nLib/site-packages\nimport site\n", encoding="utf-8")
        shutil.copyfile(ROOT / "scripts" / "prism_tokenizer_worker.py", staged_python / "prism_tokenizer_worker.py")
        (staged_python / "PYTHON-README.txt").write_text(
            "Embedded CPython is used only to tokenize PRISM prompts. All ONNX model inference is performed by native C++ ONNX Runtime.\n",
            encoding="utf-8",
        )
        shutil.copyfile(RUNTIME_MANIFEST, staged_python / "PRISM-RUNTIME-MANIFEST.json")
        staged_exe = staged_python / "python.exe"
        result = subprocess.run(
            [str(staged_exe), "-c", "import sys, tokenizers; print(f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro} {tokenizers.__version__}')"],
            check=False, capture_output=True, text=True,
        )
        expected_versions = f"{manifest['python']['version']} {manifest['tokenizers']['version']}"
        if result.returncode != 0 or result.stdout.strip() != expected_versions:
            raise RuntimeError(f"staged embedded Python tokenizer smoke check failed: {result.stderr.strip() or result.stdout.strip()}")
        smoke_test(staged_python, manifest)
        (staged_python / CONTENT_MANIFEST_NAME).write_text(
            json.dumps({"schema_version": 1, "files": runtime_file_inventory(staged_python)},
                       indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        verify_runtime_inventory(staged_python)
        replace_directory(staged_python, python_dir)
    finally:
        if staged_python.exists():
            shutil.rmtree(staged_python)


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
