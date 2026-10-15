"""Format changed C++ files and run the fast incremental verification loop."""

from __future__ import annotations

import shutil
import subprocess
import sys
import time
from pathlib import Path

from hook_common import (
    BUILD_DIRECTORY,
    REPOSITORY_ROOT,
    ctest_executable,
    executable_from_cache,
    read_event,
    run_command,
    touched_cpp_paths,
    write_block,
    output_tail,
)


def format_files(paths: list[Path], deadline: float) -> None:
    formatter = shutil.which("clang-format")
    if not formatter:
        return
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        return
    try:
        subprocess.run(
            [formatter, "-i", *(str(path) for path in paths)],
            cwd=REPOSITORY_ROOT,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=min(30, remaining),
        )
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired):
        # Formatting is best-effort and must not interrupt the edit/build loop.
        return


def main() -> int:
    event = read_event()
    if event is None:
        return 0

    paths = touched_cpp_paths(event)
    if not paths or not (BUILD_DIRECTORY / "CMakeCache.txt").is_file():
        return 0

    deadline = time.monotonic() + 300
    format_files(paths, deadline)

    def run_within_deadline(arguments: list[str]) -> subprocess.CompletedProcess[str]:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise subprocess.TimeoutExpired(arguments, 300)
        return run_command(arguments, timeout=remaining)

    cmake = executable_from_cache("CMAKE_COMMAND", "cmake")
    if not cmake:
        write_block("C++ build hook could not find CMake on PATH or in the existing build cache.")
        return 0

    try:
        build = run_within_deadline([cmake, "--build", str(BUILD_DIRECTORY), "--parallel"])
    except (OSError, subprocess.TimeoutExpired) as error:
        write_block(f"C++ build hook could not run: {error}")
        return 0

    if build.returncode != 0:
        write_block(
            f"CMake build failed with exit code {build.returncode}.",
            output_tail(build),
        )
        return 0

    try:
        lint = run_within_deadline(
            [cmake, "--build", str(BUILD_DIRECTORY), "--target", "lint", "--parallel"]
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        write_block(f"clang-tidy lint target could not run: {error}")
        return 0

    if lint.returncode != 0:
        write_block(
            f"clang-tidy lint target failed with exit code {lint.returncode}.",
            output_tail(lint),
        )
        return 0

    ctest = ctest_executable(cmake)
    try:
        tests = run_within_deadline(
            [
                ctest,
                "--test-dir",
                str(BUILD_DIRECTORY),
                "-L",
                "^(unit|integration)$",
                "-LE",
                "^benchmark$",
                "--output-on-failure",
                "-C",
                "Debug",
            ]
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        write_block(f"Fast CTest hook could not run: {error}")
        return 0

    if tests.returncode != 0:
        write_block(
            f"Fast CTest run failed with exit code {tests.returncode}.",
            output_tail(tests),
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
