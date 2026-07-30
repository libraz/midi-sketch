#!/usr/bin/env python3
"""Compile and type-check the public quick-start examples in both READMEs."""

from __future__ import annotations

import os
import re
import shlex
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
READMES = (ROOT / "README.md", ROOT / "README_ja.md")


def extract_block(markdown: str, language: str) -> str:
    match = re.search(rf"```{language}\n(.*?)```", markdown, re.DOTALL)
    if match is None:
        raise RuntimeError(f"missing {language} quick-start block")
    return match.group(1)


def run(command: list[str], label: str) -> None:
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        details = result.stderr or result.stdout
        raise RuntimeError(f"{label} failed:\n{details}")


def main() -> int:
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    with tempfile.TemporaryDirectory(prefix=".readme-examples-", dir=ROOT) as temp_dir:
        temp_path = Path(temp_dir)
        for readme in READMES:
            markdown = readme.read_text(encoding="utf-8")

            cpp_path = temp_path / f"{readme.stem}.cpp"
            cpp_path.write_text(extract_block(markdown, "cpp"), encoding="utf-8")
            run(
                [
                    *compiler,
                    "-std=c++17",
                    "-fsyntax-only",
                    "-Isrc",
                    "-Ibuild/generated",
                    str(cpp_path),
                ],
                f"{readme.name} C++ example",
            )

            ts_path = temp_path / f"{readme.stem}.ts"
            ts_path.write_text(extract_block(markdown, "typescript"), encoding="utf-8")
            run(
                [
                    "yarn",
                    "exec",
                    "tsc",
                    "--ignoreConfig",
                    "--noEmit",
                    "--target",
                    "ES2022",
                    "--module",
                    "ESNext",
                    "--moduleResolution",
                    "Bundler",
                    "--skipLibCheck",
                    str(ts_path),
                ],
                f"{readme.name} TypeScript example",
            )

    print("README quick-start examples compile and type-check successfully.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
