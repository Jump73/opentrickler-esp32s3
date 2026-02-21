#!/usr/bin/env python3
"""
Repository text quality guard:
- Ensures UTF-8 readability.
- Detects common mojibake fragments.
- Detects Polish diacritics in source/docs text (English-only policy).

Usage:
  python scripts/check_text_quality.py
  python scripts/check_text_quality.py --paths components docs html main
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from typing import Iterable


DEFAULT_PATHS = ("components", "docs", "html", "main", "scripts")
TEXT_EXTS = {
    ".c",
    ".h",
    ".cpp",
    ".hpp",
    ".py",
    ".js",
    ".ts",
    ".html",
    ".css",
    ".md",
    ".txt",
    ".yml",
    ".yaml",
    ".cmake",
}

MOJIBAKE_PATTERNS = [
    "Ã",
    "â",
    "Ä",
    "Å",
    "�",
]

POLISH_DIACRITICS_RE = re.compile(r"[ąćęłńóśźżĄĆĘŁŃÓŚŹŻ]")


def is_text_file(path: str) -> bool:
    if os.path.basename(path) in ("CMakeLists.txt",):
        return True
    return os.path.splitext(path)[1].lower() in TEXT_EXTS


def iter_files(paths: Iterable[str]) -> Iterable[str]:
    for p in paths:
        if os.path.isfile(p):
            if is_text_file(p):
                yield p
            continue
        if not os.path.isdir(p):
            continue
        for root, dirs, files in os.walk(p):
            dirs[:] = [d for d in dirs if d not in {".git", "build", ".vscode", ".idea"}]
            for name in files:
                fp = os.path.join(root, name)
                if fp.endswith("scripts\\check_text_quality.py") or fp.endswith("scripts/check_text_quality.py"):
                    continue
                if is_text_file(fp):
                    yield fp


def check_file(path: str) -> list[str]:
    issues: list[str] = []
    try:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
    except UnicodeDecodeError as e:
        issues.append(f"{path}: non-UTF-8 content ({e})")
        return issues

    for pattern in MOJIBAKE_PATTERNS:
        if pattern in text:
            issues.append(f"{path}: mojibake fragment found: {pattern!r}")

    if POLISH_DIACRITICS_RE.search(text):
        issues.append(f"{path}: Polish diacritics found (English-only text policy)")

    return issues


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--paths", nargs="*", default=list(DEFAULT_PATHS))
    args = parser.parse_args()

    all_issues: list[str] = []
    for fp in iter_files(args.paths):
        all_issues.extend(check_file(fp))

    if all_issues:
        print("Text quality check failed:")
        for item in all_issues:
            safe_item = item.encode("ascii", "backslashreplace").decode("ascii")
            print(f"- {safe_item}")
        return 1

    print("Text quality check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
