"""Approve and merge only checked, translation-only PRs from the Weblate fork.

Runs from the trusted default branch on a schedule / workflow completion.
Never checks out or executes fork code. The API merge pins the validated head.
"""
import base64
import json
import os
import subprocess

from translation_checks import LANGUAGE_FILE, validate_catalog

REPO = "mryx007/FrankyRenderer-Nightly"
WEBLATE_ID = 1607653
FORK = "weblate/FrankyRenderer-Nightly"


def api(path, method="GET", body=None):
    command = ["gh", "api", f"repos/{REPO}/{path}", "--method", method]
    if body is not None:
        command += ["--input", "-"]
    result = subprocess.run(command, input=json.dumps(body) if body is not None else None,
                            text=True, encoding="utf-8", capture_output=True, check=True)
    return json.loads(result.stdout) if result.stdout.strip() else None


def pages(path):
    result = []
    for page in range(1, 32):
        separator = "&" if "?" in path else "?"
        items = api(f"{path}{separator}per_page=100&page={page}")
        result.extend(items)
        if len(items) < 100:
            return result
    raise ValueError("Refusing unexpectedly large API response")


def eligible(pr, files):
    return (pr["state"] == "open" and not pr["draft"]
            and pr["user"]["id"] == WEBLATE_ID
            and pr["base"]["repo"]["full_name"] == REPO
            and pr["base"]["ref"] == "main"
            and pr["head"]["repo"] is not None
            and pr["head"]["repo"]["full_name"] == FORK
            and pr["head"]["repo"]["owner"]["id"] == WEBLATE_ID
            and len(files) == pr["changed_files"] and 0 < len(files) <= 100
            and all(LANGUAGE_FILE.fullmatch(f["filename"])
                    and f["status"] in ("added", "modified") for f in files))


def validate_files(sha, files, base_sha):
    def contents(path, ref):
        item = api(f"contents/{path}?ref={ref}")
        if item.get("type") != "file" or item.get("encoding") != "base64":
            raise ValueError("Expected a regular, bounded translation file")
        return base64.b64decode(item["content"]).decode("utf-8-sig")
    template = contents("translations/FrankyRenderer.pot", base_sha)
    for file in files:
        validate_catalog(contents(file["filename"], sha), template)


def process(number, dry_run=False):
    pr = api(f"pulls/{number}")
    files = pages(f"pulls/{number}/files")
    if not eligible(pr, files):
        print(f"PR #{number}: outside translation-only Weblate policy; left for manual review")
        return
    sha = pr["head"]["sha"]
    validate_files(sha, files, pr["base"]["sha"])
    # Scope the run to the trusted Code checks workflow, exact PR and head.
    runs = api(f"actions/workflows/checks.yml/runs?event=pull_request&head_sha={sha}&per_page=100")["workflow_runs"]
    runs = [run for run in runs if run["head_sha"] == sha
            and run["head_repository"]["full_name"] == FORK
            and run["head_branch"] == pr["head"]["ref"]
            and (not run["pull_requests"] or any(p["number"] == number for p in run["pull_requests"]))]
    if not runs:
        print(f"PR #{number}: waiting for Code checks")
        return
    run = max(runs, key=lambda run: run["id"])
    if run["conclusion"] == "action_required":
        # Recheck the live file list and identity immediately before granting CI.
        fresh = api(f"pulls/{number}")
        if fresh["head"]["sha"] != sha or not eligible(fresh, pages(f"pulls/{number}/files")):
            raise ValueError("PR changed during approval")
        if not dry_run:
            api(f"actions/runs/{run['id']}/approve", "POST")
        print(f"PR #{number}: approved Code checks for {sha}")
        return
    if run["status"] != "completed" or run["conclusion"] != "success":
        print(f"PR #{number}: checks not successful yet ({run['status']}, {run['conclusion']})")
        return
    # Never merge against a changed PR, closed PR, or draft after validation.
    fresh = api(f"pulls/{number}")
    fresh_files = pages(f"pulls/{number}/files")
    if fresh["head"]["sha"] != sha or not eligible(fresh, fresh_files):
        raise ValueError("PR changed during validation")
    validate_files(sha, fresh_files, fresh["base"]["sha"])
    if fresh["mergeable"] is not True:
        print(f"PR #{number}: conflicts or mergeability pending; not merged")
        return
    if dry_run:
        print(f"PR #{number}: dry run; would merge {sha}")
        return
    # Preserve Weblate's individual commits. Squashing breaks its next rebase.
    # Use GitHub's computed merge tree, but attribute our merge commit to the
    # repository owner, so the automation does not reintroduce a bot author.
    base = fresh["base"]["sha"]
    merge = api(f"git/commits/{fresh['merge_commit_sha']}")
    if [parent["sha"] for parent in merge["parents"]] != [base, sha]:
        raise ValueError("GitHub merge tree no longer matches the checked PR")
    if api("git/ref/heads/main")["object"]["sha"] != base:
        raise ValueError("Main changed before merge; retry after refreshing")
    identity = {"name": "mryx007", "email": "149878151+mryx007@users.noreply.github.com"}
    commit = api("git/commits", "POST", {
        "message": f"Merge checked Weblate translations (#{number})",
        "tree": merge["tree"]["sha"], "parents": [base, sha],
        "author": identity, "committer": identity,
    })
    # A normal fast-forward ref update rejects a concurrent main change.
    # Retaining the PR head as a parent lets GitHub mark the PR as merged.
    api("git/refs/heads/main", "PATCH", {"sha": commit["sha"], "force": False})
    print(f"PR #{number}: merged checked translations at {commit['sha']}")


def main():
    if os.environ.get("GITHUB_REPOSITORY") != REPO:
        raise ValueError("This automation is restricted to the Nightly repository")
    # Tokens do not recursively trigger push workflows. This automation only
    # updates source; the next normal Nightly build embeds eligible languages.
    for pr in pages("pulls?state=open&base=main"):
        process(pr["number"], dry_run=os.environ.get("WEBLATE_DRY_RUN") == "1")


if __name__ == "__main__":
    main()
