#!/usr/bin/env python3
"""Run portable compatibility tests with memory/undefined-behavior checks."""

import os
from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    compiler = os.environ.get("CC", "clang")
    with tempfile.TemporaryDirectory(prefix="tailadb-native-tests-") as folder:
        for name in ("daemon_profiles", "auth_compat", "status_record", "network_recovery", "bounded_command"):
            for validation in (0, 1) if name == "daemon_profiles" else (0,):
                output = Path(folder) / f"{name}-{validation}"
                subprocess.run(
                    [
                        compiler,
                        "-std=c11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-fsanitize=address,undefined",
                        f"-DTAILADB_ANDROID17_VALIDATION={validation}",
                        str(root / "tests" / f"{name}_test.c"),
                        "-o",
                        str(output),
                    ],
                    check=True,
                )
                subprocess.run([str(output)], check=True)


if __name__ == "__main__":
    main()
