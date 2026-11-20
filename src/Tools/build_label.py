"""Writes BuildLabel.h: what the settings window shows instead of the version.

  build_label.py OUT

Stable releases show the version alone (an empty label), Nightlies
"Nightly <build id>" and every other build "Test <UTC time> <commit>", with
"+" when the commit has uncommitted changes. The release workflows set
FRANKY_CHANNEL and FRANKY_BUILD_ID.
"""
import datetime
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def git(*arguments):
    try:
        return subprocess.run(["git", *arguments], cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def label():
    channel = os.environ.get("FRANKY_CHANNEL", "")
    if channel == "stable":
        return ""
    if channel == "nightly":
        return "Nightly " + os.environ["FRANKY_BUILD_ID"]
    time = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M")
    commit = git("rev-parse", "--short", "HEAD")
    changed = commit and git("status", "--porcelain", "--untracked-files=no")
    return f"Test {time} {commit}{'+' if changed else ''}".strip()


def main():
    text = label()
    if any(c in text for c in '"\\') or not text.isprintable():
        sys.exit("Unexpected characters in the build label: " + text)
    out = pathlib.Path(sys.argv[1])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(f'#pragma once\n#define FRANKY_BUILD_LABEL L"{text}"\n', encoding="utf-8")
    print("Build label:", text or "(version only)")


if __name__ == "__main__":
    main()
