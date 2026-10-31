"""Exercise stable promotion with divergent branches, without GitHub or a renderer build."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import prepare_stable


class StablePromotionTest(unittest.TestCase):
    def test_exact_nightly_tree_with_stable_docs_and_release_notice(self):
        with tempfile.TemporaryDirectory(prefix="promotion-test-") as directory:
            root = Path(directory)

            def git(*args):
                return subprocess.check_output(["git", *args], cwd=root, text=True,
                                               encoding="utf-8", stderr=subprocess.STDOUT).strip()

            def write(name, text):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")

            git("init", "--initial-branch=main")
            git("config", "user.name", "Promotion test")
            git("config", "user.email", "test@example.invalid")
            write("README.md", "base overview")
            for name in ("automation.md", "nightly-builds.md", "build.md"):
                write("docs/" + name, "base docs")
            write(".github/workflows/build.yml", "base CI")
            write("docs/nightly-changelog.md", "- Fixed the first issue.\n")
            write("src/Renderer/Feature.cpp", "base implementation")
            write("src/Renderer/Renderer.rc", 'FILEVERSION 1,0,9,0\nPRODUCTVERSION 1,0,9,0\n'
                  'VALUE "FileVersion", "1.0.9"\nVALUE "ProductVersion", "1.0.9"\n')
            write("src/Tools/package.py", 'print(f"FrankyRenderer 1.0.9 - package")\n')
            write("src/Tools/package_variants.py", "obsolete packager")
            git("add", ".")
            git("commit", "-m", "Base")
            git("switch", "-c", "nightly")
            write("src/Renderer/Feature.cpp", "tested Nightly implementation")
            write("README.md", "short Nightly overview")
            write(".github/workflows/build.yml", "Nightly CI")
            write("docs/nightly-changelog.md", "- Fixed the first issue.\n- Fixed the second issue.\n")
            (root / "src/Tools/package_variants.py").unlink()
            write("src/Tools/package_release.py", 'version = "nightly-" + build_id if channel == "nightly" else "1.0.9"\n')
            git("add", "-A")
            git("commit", "-m", "Tested Nightly")
            nightly = git("rev-parse", "HEAD")
            git("switch", "main")
            write("src/Renderer/Feature.cpp", "divergent stable implementation")
            write("README.md", "full stable overview")
            write(".github/workflows/build.yml", "stable publication CI")
            write(".github/release-notices/1.1.md", "## Important upgrade information\n\nOne package for both players.\n")
            git("add", ".")
            git("commit", "-m", "Divergent stable changes")
            stable = git("rev-parse", "HEAD")

            original_root = prepare_stable.ROOT
            prepare_stable.ROOT = root
            try:
                plan = prepare_stable.prepare("1.0.9", nightly, root / "promotion")
                self.assertEqual(plan["version"], "1.1")
                self.assertEqual(plan["base_commit"], stable)
                self.assertEqual(git("show", "HEAD:src/Renderer/Feature.cpp"), "tested Nightly implementation")
                self.assertEqual(git("show", "HEAD:README.md"), "full stable overview")
                self.assertEqual(git("show", "HEAD:.github/workflows/build.yml"), "stable publication CI")
                self.assertFalse((root / "src/Tools/package_variants.py").exists())
                changed = git("diff", "--name-only", nightly, "HEAD", "--", "src").splitlines()
                self.assertEqual(set(changed), {"src/Renderer/Renderer.rc", "src/Tools/package.py",
                                                "src/Tools/package_release.py"})
                self.assertEqual(git("show", "-s", "--format=%P", "HEAD").split(), [stable, nightly])
                notes = (root / "promotion/release-notes.md").read_text(encoding="utf-8")
                self.assertTrue(notes.startswith("## Important upgrade information"))
                self.assertIn("## Changes\n\n- Fixed the first issue.\n- Fixed the second issue.", notes)
                self.assertEqual(json.loads((root / ".github/last-promoted-nightly.json").read_text())["source_commit"], nightly)
                with self.assertRaisesRegex(ValueError, "already been published"):
                    prepare_stable.prepare("1.1", nightly, root / "second-promotion")
            finally:
                prepare_stable.ROOT = original_root


if __name__ == "__main__":
    unittest.main()
