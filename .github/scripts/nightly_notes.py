"""Show only new Nightly entries; keep the cumulative list for stable releases."""
import json
import os
from pathlib import Path
import re
import subprocess


def entries(text):
    return list(dict.fromkeys(line.strip() for line in text.splitlines()
                             if line.strip().startswith("- ")))


def new_entries(current, previous):
    old = set(entries(previous))
    return "\n".join(line for line in entries(current) if line not in old)


def main():
    repository = os.environ["GITHUB_REPOSITORY"]
    sha = os.environ["GITHUB_SHA"]
    build = os.environ["BUILD_ID"]
    current = Path("docs/nightly-changelog.md").read_text(encoding="utf-8")
    if not entries(current):
        raise ValueError("The cumulative changelog must contain concise bullet entries")
    result = subprocess.run(["gh", "api", f"repos/{repository}/releases/tags/nightly"],
                            capture_output=True, text=True, encoding="utf-8")
    previous = ""
    if result.returncode:
        if "404" not in result.stderr:
            raise RuntimeError("Cannot read the previous Nightly release: " + result.stderr)
    else:
        body = json.loads(result.stdout)["body"] or ""
        source = re.search(r"/commit/([0-9a-f]{40})", body)
        if not source:
            raise ValueError("Previous Nightly has no source commit")
        if source[1] == sha:
            # Re-running a published build must not erase its release notes.
            Path("nightly-notes.md").write_text(body, encoding="utf-8")
            return
        previous = subprocess.check_output(
            ["git", "show", f"{source[1]}:docs/nightly-changelog.md"],
            text=True, encoding="utf-8")
    changes = new_entries(current, previous) or "No additional renderer fixes in this build."
    body = (f"Build **{build}** — [source code](https://github.com/{repository}/commit/{sha}).\n\n"
            + changes + "\n\n[Report results](https://github.com/mryx007/FrankyRenderer/issues) "
            "with the build ID and player version.\n")
    Path("nightly-notes.md").write_text(body, encoding="utf-8")


if __name__ == "__main__":
    main()
