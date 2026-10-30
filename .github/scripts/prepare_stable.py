"""Prepare a stable candidate from the published Nightly; never push it here."""
import json
from pathlib import Path
import re
import subprocess

from validate_release import next_version, parts

ROOT = Path(__file__).resolve().parents[2]


def git(*args, check=True):
    return subprocess.run(["git", *args], cwd=ROOT, check=check, capture_output=True,
                          text=True, encoding="utf-8")


def api(path):
    return json.loads(subprocess.check_output(["gh", "api", path], text=True, encoding="utf-8"))


def stable_notes(current, previous=""):
    old = {line.strip() for line in previous.splitlines() if line.strip().startswith("- ")}
    entries = dict.fromkeys(line.strip() for line in current.splitlines()
                            if line.strip().startswith("- ") and line.strip() not in old)
    if not entries:
        raise ValueError("No new changelog entries since the last stable release")
    return "\n".join(entries)


def prepare(previous, nightly_sha, output):
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    stable_sha = git("rev-parse", "HEAD").stdout.strip()
    marker = ROOT / ".github/last-promoted-nightly.json"
    promoted = json.loads(marker.read_text())["source_commit"] if marker.exists() else None
    if promoted == nightly_sha:
        raise ValueError("This Nightly has already been published as a stable release")
    version = next_version(previous)
    current_notes = git("show", f"{nightly_sha}:docs/nightly-changelog.md").stdout
    previous_notes = git("show", f"{promoted}:docs/nightly-changelog.md").stdout if promoted else ""
    notes = stable_notes(current_notes, previous_notes)
    protected = ["README.md", ".github", "docs/automation.md", "docs/nightly-builds.md", "docs/build.md"]
    result = git("merge", "--no-ff", "--no-commit", nightly_sha, check=False)
    conflicts = git("diff", "--name-only", "--diff-filter=U").stdout.splitlines()
    unexpected = [name for name in conflicts if not any(name == p or name.startswith(p + "/") for p in protected)]
    if unexpected or (result.returncode and not conflicts):
        git("merge", "--abort", check=False)
        raise ValueError("Manual conflict resolution required: " + ", ".join(unexpected or [result.stderr]))
    # Preserve stable overview and automation, never replace them with the short Nightly page.
    git("restore", "--source", stable_sha, "--staged", "--worktree", "--", *protected)
    changed = git("diff", "--cached", "--name-only", stable_sha).stdout.splitlines()
    if not any(name.startswith("src/") or name in ("build.cmd", "requirements.txt", "requirements-dev.txt", "LICENSE", "THIRD_PARTY.md") for name in changed):
        git("merge", "--abort", check=False)
        raise ValueError("No new renderer/build changes to publish")
    rc = ROOT / "src/Renderer/Renderer.rc"
    text = rc.read_text(encoding="utf-8")
    old = re.search(r'VALUE "FileVersion", "([\d.]+)"', text).group(1)
    if old not in (previous, version):
        raise ValueError(f"Unexpected source version {old}; expected {previous} or {version}")
    major, minor, patch = parts(version)
    for field in ("FILEVERSION", "PRODUCTVERSION"):
        text, count = re.subn(rf"^{field}\s+[\d,]+$", f"{field} {major},{minor},{patch},0", text, flags=re.MULTILINE)
        if count != 1:
            raise ValueError("Missing resource version: " + field)
    for field in ("FileVersion", "ProductVersion"):
        text, count = re.subn(rf'(VALUE "{field}", ")[\d.]+(")', rf'\g<1>{version}\2', text)
        if count != 1:
            raise ValueError("Missing resource string: " + field)
    rc.write_text(text, encoding="utf-8", newline="\n")
    for name, pattern in (
        ("src/Tools/package.py", r'(f"FrankyRenderer )[\d.]+( - )'),
        ("src/Tools/package_variants.py", r'(version = "nightly-" \+ build_id if channel == "nightly" else ")[\d.]+(")'),
    ):
        path = ROOT / name
        text, count = re.subn(pattern, rf'\g<1>{version}\2', path.read_text(encoding="utf-8"))
        if count != 1:
            raise ValueError("Missing package version in " + name)
        path.write_text(text, encoding="utf-8", newline="\n")
    marker.write_text(json.dumps({"source_repository": "mryx007/FrankyRenderer-Nightly",
                                  "source_commit": nightly_sha, "version": version}, indent=2) + "\n")
    git("add", "src/Renderer/Renderer.rc", "src/Tools/package.py", "src/Tools/package_variants.py", ".github/last-promoted-nightly.json")
    git("commit", "-m", f"Release {version} from tested Nightly {nightly_sha[:10]}")
    sha = git("rev-parse", "HEAD").stdout.strip()
    git("bundle", "create", str(output / "candidate.bundle"), "HEAD")
    plan = {"version": version, "commit": sha, "base_commit": stable_sha, "nightly_commit": nightly_sha}
    (output / "promotion.json").write_text(json.dumps(plan, indent=2) + "\n")
    (output / "release-notes.md").write_text(notes + "\n", encoding="utf-8")
    return plan


def main():
    previous = api("repos/mryx007/FrankyRenderer/releases/latest")["tag_name"].removeprefix("v")
    release = api("repos/mryx007/FrankyRenderer-Nightly/releases/tags/nightly")
    if release["draft"]:
        raise ValueError("The Nightly is not published")
    git("fetch", "--no-tags", "https://github.com/mryx007/FrankyRenderer-Nightly.git",
        "+refs/tags/nightly:refs/remotes/nightly/published")
    nightly_sha = git("rev-parse", "refs/remotes/nightly/published^{commit}").stdout.strip()
    if f"/commit/{nightly_sha}" not in release["body"]:
        raise ValueError("Nightly source and download metadata do not match; wait for its upload to finish")
    version = next_version(previous)
    existing = subprocess.run(["gh", "api", f"repos/mryx007/FrankyRenderer/releases/tags/v{version}"], capture_output=True)
    if existing.returncode == 0:
        raise ValueError("The next stable release already exists")
    git("config", "user.name", "github-actions[bot]")
    git("config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com")
    print(json.dumps(prepare(previous, nightly_sha, ROOT / "promotion")))


if __name__ == "__main__":
    main()
