"""Helpers for replacing generated PRISM staging trees without mixed versions."""

from __future__ import annotations

import os
import shutil
import tempfile
import uuid
from pathlib import Path


def replace_directory(staged: Path, destination: Path) -> None:
    """Replace destination with a completed sibling tree, restoring it on failure."""
    staged = staged.resolve()
    destination = destination.resolve()
    if staged.parent != destination.parent:
        raise ValueError("staged and destination directories must share a parent for atomic rename")
    backup = destination.parent / f".{destination.name}.previous-{os.getpid()}-{uuid.uuid4().hex}"
    moved_old = False
    try:
        if destination.exists():
            os.replace(destination, backup)
            moved_old = True
        os.replace(staged, destination)
    except OSError:
        if moved_old and backup.exists() and not destination.exists():
            os.replace(backup, destination)
        raise
    if backup.exists():
        shutil.rmtree(backup)


def temporary_sibling(destination: Path) -> Path:
    destination.parent.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix=f".{destination.name}.staging-", dir=destination.parent))
