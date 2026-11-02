"""Policy and race checks for the privileged translation automation."""
from copy import deepcopy
import unittest
from unittest.mock import patch

import weblate_prs as worker
from translation_checks import validate_catalog

HEADER = 'msgid ""\nmsgstr ""\n"Content-Type: text/plain; charset=UTF-8\\n"\n"Language: de\\n"\n\n'
TEMPLATE = HEADER + 'msgid "Hello %s"\nmsgstr ""\n'


def pull():
    return {"state": "open", "draft": False, "user": {"id": worker.WEBLATE_ID},
            "base": {"repo": {"full_name": worker.REPO}, "ref": "main", "sha": "base"},
            "head": {"repo": {"full_name": worker.FORK, "owner": {"id": worker.WEBLATE_ID}},
                     "sha": "tested", "ref": "weblate-menu"}, "changed_files": 1, "mergeable": True}


FILES = [{"filename": "translations/de.po", "status": "modified"}]


class TranslationPolicy(unittest.TestCase):
    def test_valid_and_incomplete_catalog(self):
        self.assertEqual(validate_catalog(HEADER + 'msgid "Hello %s"\nmsgstr "Hallo %s"\n', TEMPLATE), 1)
        self.assertEqual(validate_catalog(TEMPLATE, TEMPLATE), 0)
        self.assertEqual(validate_catalog(HEADER + 'msgid "Old source"\nmsgstr "Alter Text"\n', TEMPLATE), 1)

    def test_bad_catalogs(self):
        for entry in ['msgid "Hello %s"\nmsgstr "Hallo"\n',
                      'msgid "Hello %s"\nmsgstr "Hallo %d"\n',
                      'msgid "Hello %s"\nmsgstr "unterminated\n',
                      'msgctxt "x"\nmsgid "Hello %s"\nmsgstr "Hallo %s"\n']:
            with self.subTest(entry=entry), self.assertRaises((ValueError, OSError)):
                validate_catalog(HEADER + entry, TEMPLATE)

    def test_only_weblate_language_files(self):
        self.assertTrue(worker.eligible(pull(), FILES))
        for name, status in [(".github/workflows/checks.yml", "modified"),
                             ("translations/FrankyRenderer.pot", "modified"),
                             ("translations/de.po", "removed"),
                             ("translations/de.po", "renamed"),
                             ("translations/../evil.po", "added")]:
            self.assertFalse(worker.eligible(pull(), [{"filename": name, "status": status}]))
        for key in ["author", "fork", "draft", "base", "missing_file"]:
            pr = pull()
            if key == "author": pr["user"]["id"] = 123
            if key == "fork": pr["head"]["repo"]["full_name"] = "someone/FrankyRenderer-Nightly"
            if key == "draft": pr["draft"] = True
            if key == "base": pr["base"]["ref"] = "other"
            if key == "missing_file": pr["changed_files"] = 2
            self.assertFalse(worker.eligible(pr, FILES))

    def run_process(self, conclusion, changed=False, dry_run=False):
        calls = []
        reads = 0
        def api(path, method="GET", body=None):
            nonlocal reads
            calls.append((path, method, body))
            if path == "pulls/1":
                reads += 1
                pr = pull()
                if changed and reads > 1: pr["head"]["sha"] = "newer"
                return pr
            if path.startswith("actions/workflows/"):
                return {"workflow_runs": [{"id": 10, "head_sha": "tested", "head_branch": "weblate-menu",
                        "head_repository": {"full_name": worker.FORK}, "pull_requests": [],
                        "conclusion": conclusion, "status": "completed"}]}
            if path == "pulls/1/merge": return {"merged": True, "sha": "merged"}
            if path == "actions/runs/10/approve": return None
            raise AssertionError(path)
        with patch.object(worker, "api", side_effect=api), patch.object(worker, "pages", return_value=deepcopy(FILES)), \
             patch.object(worker, "validate_files"):
            worker.process(1, dry_run=dry_run)
        return calls

    def test_approval_only_before_checks(self):
        calls = self.run_process("action_required")
        self.assertEqual([path for path, method, _ in calls if method != "GET"], ["actions/runs/10/approve"])

    def test_merge_pins_successful_head(self):
        calls = self.run_process("success")
        merge = [body for path, method, body in calls if path == "pulls/1/merge"]
        self.assertEqual(merge[0]["sha"], "tested")
        self.assertEqual(merge[0]["merge_method"], "squash")

    def test_failures_and_dry_run_never_merge(self):
        for conclusion in ["failure", "cancelled", "timed_out"]:
            self.assertTrue(all(method == "GET" for _, method, _ in self.run_process(conclusion)))
        self.assertTrue(all(method == "GET" for _, method, _ in self.run_process("success", dry_run=True)))

    def test_head_change_stops_mutation(self):
        for conclusion in ["success", "action_required"]:
            with self.assertRaisesRegex(ValueError, "PR changed"):
                self.run_process(conclusion, changed=True)


if __name__ == "__main__":
    unittest.main()
