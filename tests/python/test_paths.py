"""Resolve shared host code relative to the repository, independent of the working directory."""

from pathlib import Path
import sys

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
for directory in (REPOSITORY_ROOT / "host", REPOSITORY_ROOT / "host" / "python_apis"):
    path = str(directory)
    if path not in sys.path:
        sys.path.insert(0, path)
