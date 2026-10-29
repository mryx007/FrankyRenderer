"""Write release notes as data, never interpolate commit messages into a shell."""
import os
from pathlib import Path
import subprocess

repository = os.environ["GITHUB_REPOSITORY"]
sha = os.environ["GITHUB_SHA"]
build = os.environ["BUILD_ID"]
tag = subprocess.run(["git", "rev-parse", "--verify", "refs/tags/nightly"], capture_output=True, text=True)
revision = f"{tag.stdout.strip()}..{sha}" if tag.returncode == 0 else "HEAD~5..HEAD"
changes = subprocess.check_output(["git", "log", "--max-count=20", "--format=- %s", revision], text=True)
body = (f"Build **{build}** — [source code](https://github.com/{repository}/commit/{sha}).\n\n"
        + (changes.strip() or "Rebuilt the current source.")
        + "\n\nBoth packages passed the automated build, package integrity and non-GPU regression checks. "
        "TV/HDR signalling, hardware decoding and player-visible behavior still need user testing.\n\n"
        "Choose the MPC-BE or PotPlayer ZIP. [Report results](https://github.com/mryx007/FrankyRenderer/issues) "
        "with the build ID and player version.\n")
Path("nightly-notes.md").write_text(body, encoding="utf-8")
