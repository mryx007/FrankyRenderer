"""Nightly notes list only entries that are new since the previous build."""
import unittest

import nightly_notes


class NightlyNotesTest(unittest.TestCase):
    def test_new_entries_only(self):
        previous = "- Fixed one.\n- Added two.<br><img src=a>\n"
        current = "- Fixed one.\n- Added two.<br><img src=b>\n- Fixed three.\n"
        self.assertEqual(nightly_notes.new_entries(current, previous), "- Fixed three.")

    def test_stable_marker(self):
        sha = "38646bc7e5" + "0" * 30
        self.assertEqual(nightly_notes.promoted_commit('{"source_commit": "%s", "version": "1.2"}' % sha), sha)
        self.assertIsNone(nightly_notes.promoted_commit('{"version": "1.2"}'))
        self.assertIsNone(nightly_notes.promoted_commit('{"source_commit": "main"}'))


if __name__ == "__main__":
    unittest.main()
