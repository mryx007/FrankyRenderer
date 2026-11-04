"""Offer newer LAV FFmpeg and libplacebo versions as pull requests.

Never merges anything. Each update gets its own branch and pull request with
the pinned versions, documentation and a changelog entry; the Code checks
workflow is started on it, since branches pushed with the built-in token start
no workflows themselves. New libplacebo commits between releases are only
listed in the run summary, so chosen fixes can be added as patches by hand.
"""
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
REPOSITORY = os.environ.get("GITHUB_REPOSITORY", "mryx007/FrankyRenderer-Nightly")
# Commits carry the owner's name, like the Weblate merges, so no bot account
# joins the contributors.
AUTHOR = ["-c", "user.name=mryx007", "-c", "user.email=149878151+mryx007@users.noreply.github.com"]
FFMPEG_LOCK = ROOT / "src/deps/dv-ffmpeg.lock.json"
LIBPLACEBO_LOCK = ROOT / "src/deps/libplacebo-source.lock.json"
CHANGELOG = ROOT / "docs/nightly-changelog.md"


def gh(*args):
    return json.loads(subprocess.check_output(["gh", "api", *args], text=True, encoding="utf-8"))


def summary(text):
    print(text)
    path = os.environ.get("GITHUB_STEP_SUMMARY")
    if path:
        with open(path, "a", encoding="utf-8") as output:
            output.write(text + "\n")


# Read and write bytes, so the files keep their line endings.
def read(path):
    return path.read_bytes().decode("utf-8")


def replace_once(path, old, new):
    text = read(path)
    if text.count(old) != 1:
        raise ValueError(f"{path.relative_to(ROOT)} must contain {old!r} exactly once")
    path.write_bytes(text.replace(old, new).encode("utf-8"))


def add_changelog(entry):
    text = read(CHANGELOG)
    newline = "\r\n" if "\r\n" in text else "\n"
    CHANGELOG.write_bytes((text.rstrip("\r\n") + newline + entry + newline).encode("utf-8"))


def day(iso):
    date = datetime.date.fromisoformat(iso[:10])
    return f"{date.day} {date.strftime('%B %Y')}"


def ffmpeg_update():
    lock = json.loads(FFMPEG_LOCK.read_text(encoding="utf-8"))
    current = lock["commit"]
    branch = gh("repos/clsid2/FFmpeg")["default_branch"]
    latest = gh(f"repos/clsid2/FFmpeg/commits/{branch}")
    sha, date = latest["sha"], latest["commit"]["committer"]["date"]
    if sha == current:
        summary(f"LAV FFmpeg: pinned {current[:12]} is current.")
        return None
    ahead = gh(f"repos/clsid2/FFmpeg/compare/{current}...{sha}")["ahead_by"]
    summary(f"LAV FFmpeg: pinned {current[:12]}, newest [{sha[:12]}]({latest['html_url']}) "
            f"from {date[:10]}, {ahead} commits newer.")

    def apply():
        replace_once(FFMPEG_LOCK, current, sha)
        replace_once(ROOT / "THIRD_PARTY.md", current, sha)
        sources = ROOT / "docs/dependency-sources.md"
        replace_once(sources, current, sha)
        replace_once(sources, f"commit {current[:12]}", f"commit {sha[:12]}")
        add_changelog(f"- Updated FFmpeg to the latest LAV-maintained version ({day(date)}).")

    return {
        "prefix": "update/ffmpeg-",
        "branch": f"update/ffmpeg-{sha[:12]}",
        "title": f"Update LAV FFmpeg to {sha[:12]}",
        "body": (f"Newest commit of the LAV-maintained FFmpeg fork: [{sha[:12]}]({latest['html_url']}) "
                 f"from {date[:10]}, {ahead} commits after the pinned {current[:12]}.\n\n"
                 f"Compare: https://github.com/clsid2/FFmpeg/compare/{current}...{sha}\n\n"
                 "Code checks build FFmpeg from this commit and run the tests. Before merging, "
                 "test Dolby Vision playback in a player; GitHub has no GPU or HDR display."),
        "apply": apply,
    }


def libplacebo_update():
    lock = json.loads(LIBPLACEBO_LOCK.read_text(encoding="utf-8"))
    compare = gh(f"repos/haasn/libplacebo/compare/{lock['commit']}...master")
    commits = compare.get("commits", [])
    lines = [f"libplacebo: pinned {lock['version']}, {compare['ahead_by']} newer commits on master."]
    for commit in commits[-30:]:
        lines.append(f"- [{commit['sha'][:10]}]({commit['html_url']}) "
                     + commit["commit"]["message"].splitlines()[0])
    summary("\n".join(lines))
    release = gh("repos/haasn/libplacebo/releases/latest")
    version = release["tag_name"].lstrip("v")
    if version == lock["version"]:
        return None
    sha = gh(f"repos/haasn/libplacebo/commits/{release['tag_name']}")["sha"]
    url = f"https://codeload.github.com/haasn/libplacebo/tar.gz/{sha}"
    with urllib.request.urlopen(url, timeout=120) as response:
        archive = response.read()
    api = int(version.split(".")[1])
    summary(f"libplacebo: new release [{release['tag_name']}]({release['html_url']}).")

    def apply():
        updated = dict(lock, version=version, api=api, commit=sha, url=url,
                       sha256=hashlib.sha256(archive).hexdigest())
        LIBPLACEBO_LOCK.write_text(json.dumps(updated, indent=2) + "\n", encoding="utf-8")
        add_changelog(f"- Updated libplacebo to {version}.")

    note = ""
    if api != lock["api"]:
        note = (f"\n\n**The API changes from {lock['api']} to {api}.** The DLL name, headers and import "
                "definition have to follow by hand; until then the checks are expected to fail.")
    return {
        "prefix": "update/libplacebo-",
        "branch": f"update/libplacebo-{version}",
        "title": f"Update libplacebo to {version}",
        "body": (f"New libplacebo release [{release['tag_name']}]({release['html_url']}) "
                 f"replaces the pinned {lock['version']}.{note}\n\n"
                 "Check whether the patches in `src/deps/patches/libplacebo` are still needed and "
                 "update `docs/libplacebo-update.md`. Test playback in a player before merging."),
        "apply": apply,
    }


def offer(update, dry_run):
    if subprocess.run(["gh", "api", f"repos/{REPOSITORY}/branches/{update['branch']}"],
                      capture_output=True).returncode == 0:
        summary(f"Already offered: {update['branch']}")
        return
    if dry_run:
        summary(f"Dry run: would open {update['title']}")
        return
    subprocess.run(["git", "checkout", "-B", update["branch"], "origin/main"], cwd=ROOT, check=True)
    update["apply"]()
    subprocess.run(["git", *AUTHOR, "commit", "-am", update["title"]], cwd=ROOT, check=True)
    subprocess.run(["git", "push", "origin", update["branch"]], cwd=ROOT, check=True)
    # An older offer of the same dependency is superseded by this one.
    older = json.loads(subprocess.check_output(
        ["gh", "pr", "list", "--repo", REPOSITORY, "--state", "open", "--json", "number,headRefName"], text=True))
    for pr in older:
        if pr["headRefName"].startswith(update["prefix"]):
            subprocess.run(["gh", "pr", "close", str(pr["number"]), "--repo", REPOSITORY, "--delete-branch",
                            "--comment", f"Superseded by {update['branch']}."], check=True)
    subprocess.run(["gh", "pr", "create", "--repo", REPOSITORY, "--base", "main", "--head", update["branch"],
                    "--title", update["title"], "--body", update["body"]], check=True)
    subprocess.run(["gh", "workflow", "run", "checks.yml", "--repo", REPOSITORY, "--ref", update["branch"]],
                   check=True)
    subprocess.run(["git", "checkout", "--detach", "origin/main"], cwd=ROOT, check=True)
    summary(f"Opened: {update['title']}")


def main():
    dry_run = "--dry-run" in sys.argv
    for update in (ffmpeg_update(), libplacebo_update()):
        if update:
            offer(update, dry_run)


if __name__ == "__main__":
    main()
