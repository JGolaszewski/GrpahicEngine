"""Run the x64 AddressSanitizer workflow after explicit CTest commands."""

from __future__ import annotations

import re
import sys
import subprocess
from pathlib import Path

from hook_common import (
    BUILD_DIRECTORY,
    ctest_executable,
    event_command,
    executable_from_cache,
    read_cmake_cache_value,
    read_event,
    run_command,
    output_tail,
    write_block,
)


ASAN_BUILD_DIRECTORY = BUILD_DIRECTORY.parent / "x64-asan"


def fail(step: str, result: subprocess.CompletedProcess[str]) -> None:
    write_block(
        f"MSVC AddressSanitizer check failed during {step} (exit code {result.returncode}).",
        output_tail(result),
    )


def main() -> int:
    event = read_event()
    if event is None or event.get("tool_name") != "Bash":
        return 0

    command = event_command(event)
    if not re.search(r"\bctest(?:\.exe)?\b", command, flags=re.IGNORECASE):
        return 0
    if re.search(r"out[\\/]build[\\/]x64-asan", command, flags=re.IGNORECASE):
        return 0

    cmake = executable_from_cache("CMAKE_COMMAND", "cmake")
    make_program = read_cmake_cache_value("CMAKE_MAKE_PROGRAM")
    installed_packages = read_cmake_cache_value("VCPKG_INSTALLED_DIR")
    if not cmake:
        write_block("MSVC AddressSanitizer hook could not find CMake on PATH or in the debug build cache.")
        return 0
    if not make_program or not Path(make_program).is_file():
        write_block("MSVC AddressSanitizer hook could not find the cached Ninja executable.")
        return 0
    if not installed_packages or not Path(installed_packages).is_dir():
        write_block("MSVC AddressSanitizer hook could not find the debug build's installed vcpkg packages.")
        return 0

    steps = (
        (
            "ASan configuration",
            [
                cmake,
                "--preset",
                "x64-asan",
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                "-DVCPKG_MANIFEST_INSTALL=OFF",
                f"-DVCPKG_INSTALLED_DIR={installed_packages}",
                f"-DCMAKE_MAKE_PROGRAM={make_program}",
            ],
        ),
        (
            "ASan build",
            [cmake, "--build", str(ASAN_BUILD_DIRECTORY), "--parallel"],
        ),
        (
            "ASan tests",
            [
                ctest_executable(cmake),
                "--test-dir",
                str(ASAN_BUILD_DIRECTORY),
                "--output-on-failure",
                "-C",
                "Debug",
            ],
        ),
    )

    for step, arguments in steps:
        try:
            result = run_command(arguments)
        except (OSError, subprocess.TimeoutExpired) as error:
            write_block(f"MSVC AddressSanitizer hook could not run {step}: {error}")
            return 0
        if result.returncode != 0:
            fail(step, result)
            return 0

    return 0


if __name__ == "__main__":
    sys.exit(main())
