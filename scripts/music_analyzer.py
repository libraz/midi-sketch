#!/usr/bin/env python3
"""Thin wrapper for backward compatibility.

Delegates to the music_analyzer package.
Usage: python3 scripts/music_analyzer.py [args...]
"""
import sys
from pathlib import Path

# Ensure the scripts/ directory is on the path so the package can be found
sys.path.insert(0, str(Path(__file__).resolve().parent))

from music_analyzer.cli import main

main()
