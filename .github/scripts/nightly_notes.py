"""Write release notes as data, never interpolate commit messages into a shell."""
import os
from pathlib import Path

repository = os.environ["GITHUB_REPOSITORY"]
sha = os.environ["GITHUB_SHA"]
build = os.environ["BUILD_ID"]
changes = Path("docs/nightly-changelog.md").read_text(encoding="utf-8").strip()
if not changes or len(changes) > 2000:
    raise ValueError("Keep docs/nightly-changelog.md short and specific to the current Nightly")
body = (f"Build **{build}** — [source code](https://github.com/{repository}/commit/{sha}).\n\n"
        + changes + "\n\n[Report results](https://github.com/mryx007/FrankyRenderer/issues) "
        "with the build ID and player version.\n")
Path("nightly-notes.md").write_text(body, encoding="utf-8")
