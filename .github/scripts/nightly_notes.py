"""Show only new Nightly entries; keep the cumulative list for stable releases."""
import json
import os
from pathlib import Path
import re
import subprocess


def entries(text):
    return list(dict.fromkeys(line.strip() for line in text.splitlines()
                             if line.strip().startswith("- ")))


def key(entry):
    # An image attached after <br> may change without making the entry new.
    return entry.split("<br>")[0].strip()


def new_entries(current, previous):
    old = {key(line) for line in entries(previous)}
    return "\n".join(line for line in entries(current) if key(line) not in old)


def promoted_commit(marker):
    """Nightly commit of the latest stable release, from the stable repository's marker file."""
    commit = json.loads(marker).get("source_commit", "")
    return commit if re.fullmatch(r"[0-9a-f]{40}", commit) else None


def stable_changelog():
    # Without a previous Nightly (they are removed after a stable release),
    # compare with the changelog the latest stable release was built from.
    result = subprocess.run(["gh", "api", "-H", "Accept: application/vnd.github.raw",
                             "repos/mryx007/FrankyRenderer/contents/.github/last-promoted-nightly.json"],
                            capture_output=True, text=True, encoding="utf-8")
    commit = promoted_commit(result.stdout) if result.returncode == 0 else None
    if not commit:
        return ""
    shown = subprocess.run(["git", "show", f"{commit}:docs/nightly-changelog.md"],
                           capture_output=True, text=True, encoding="utf-8")
    return shown.stdout if shown.returncode == 0 else ""


def main():
    repository = os.environ["GITHUB_REPOSITORY"]
    sha = os.environ["GITHUB_SHA"]
    build = os.environ["BUILD_ID"]
    current = Path("docs/nightly-changelog.md").read_text(encoding="utf-8")
    if not entries(current):
        raise ValueError("The cumulative changelog must contain concise bullet entries")
    result = subprocess.run(["gh", "api", f"repos/{repository}/releases/latest"],
                            capture_output=True, text=True, encoding="utf-8")
    previous = ""
    if result.returncode:
        if "404" not in result.stderr:
            raise RuntimeError("Cannot read the previous Nightly release: " + result.stderr)
        previous = stable_changelog()
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
