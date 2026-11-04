"""Offline tests for dependency_updates.py: what an update changes."""
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

import dependency_updates as updates

OLD = "a" * 40
NEW = "b" * 40


class FFmpegUpdate(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        (self.root / "src/deps").mkdir(parents=True)
        (self.root / "docs").mkdir()
        (self.root / "src/deps/dv-ffmpeg.lock.json").write_text(json.dumps({"commit": OLD}), encoding="utf-8")
        (self.root / "THIRD_PARTY.md").write_text(f"FFmpeg commit `{OLD}`.\n", encoding="utf-8")
        (self.root / "docs/dependency-sources.md").write_text(
            f"- [LAV FFmpeg, commit {OLD[:12]}](https://github.com/clsid2/FFmpeg/tree/{OLD})\n", encoding="utf-8")
        (self.root / "docs/nightly-changelog.md").write_bytes(b"- Earlier entry.\r\n")
        patches = {"ROOT": self.root, "FFMPEG_LOCK": self.root / "src/deps/dv-ffmpeg.lock.json",
                   "CHANGELOG": self.root / "docs/nightly-changelog.md"}
        self.patches = [mock.patch.object(updates, name, value) for name, value in patches.items()]
        # Test messages stay out of the summary of the GitHub run.
        self.patches.append(mock.patch.dict(os.environ, {"GITHUB_STEP_SUMMARY": ""}))
        for patch in self.patches:
            patch.start()

    def tearDown(self):
        for patch in self.patches:
            patch.stop()
        shutil.rmtree(self.root)

    def answers(self, path):
        if path == "repos/clsid2/FFmpeg":
            return {"default_branch": "master"}
        if path.startswith("repos/clsid2/FFmpeg/compare/"):
            return {"ahead_by": 3}
        return {"sha": NEW, "html_url": "https://example/commit",
                "commit": {"committer": {"date": "2026-11-04T19:00:00Z"}}}

    def test_current_commit_offers_nothing(self):
        with mock.patch.object(updates, "gh", lambda path: dict(self.answers(path), sha=OLD)):
            self.assertIsNone(updates.ffmpeg_update())

    def test_new_commit_updates_pins_docs_and_changelog(self):
        with mock.patch.object(updates, "gh", self.answers):
            update = updates.ffmpeg_update()
        self.assertEqual(update["branch"], f"update/ffmpeg-{NEW[:12]}")
        update["apply"]()
        self.assertEqual(json.loads((self.root / "src/deps/dv-ffmpeg.lock.json").read_text())["commit"], NEW)
        self.assertIn(NEW, (self.root / "THIRD_PARTY.md").read_text())
        sources = (self.root / "docs/dependency-sources.md").read_text()
        self.assertIn(f"commit {NEW[:12]}", sources)
        self.assertNotIn(OLD[:12], sources)
        changelog = (self.root / "docs/nightly-changelog.md").read_bytes().decode("utf-8")
        self.assertEqual(changelog, "- Earlier entry.\r\n"
                         "- Updated FFmpeg to the latest LAV-maintained version (4 November 2026).\r\n")


if __name__ == "__main__":
    unittest.main()
