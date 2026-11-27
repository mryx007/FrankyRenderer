"""Build the non-GPU regression checks at the same time, then run them one after another.

Used by the CI build. The builds are independent and dominate the time; the
checks themselves run in seconds and stay sequential, so their output is not
interleaved and no two touch settings at once.

test_settings_ini writes the real settings outside CI, so a local run takes it
through src/Tests/run_tests.py, which gives it disposable settings.
"""

from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
TESTS = SRC / "Tests"
# Each check with the folder its build script runs it from.
CHECKS = [
    ("motion_timing", SRC),
    ("presentation_timing", SRC),
    ("settings_ini", ROOT),
    ("settings_before_playback", SRC),
    ("settings_store", ROOT),
    ("dovi_mapping", SRC),
    ("dv_status", SRC),
    ("field_order", SRC),
]


def build(name):
    env = dict(os.environ, FRANKY_TEST_BUILD_ONLY="1")
    started = time.monotonic()
    result = subprocess.run(
        ["cmd", "/c", str(TESTS / f"build_{name}_tests.cmd")],
        env=env,
        capture_output=True,
        text=True,
        errors="replace",
    )
    return name, result.returncode, result.stdout + result.stderr, time.monotonic() - started


def main():
    in_ci = os.environ.get("GITHUB_ACTIONS") == "true"
    started = time.monotonic()
    with ThreadPoolExecutor(max_workers=len(CHECKS)) as pool:
        builds = list(pool.map(build, [name for name, _ in CHECKS]))
    failed = False
    for name, code, output, seconds in builds:
        print(f"build {name}: {seconds:.1f} s{'' if code == 0 else f', FAILED ({code})'}")
        if code:
            print(output)
            failed = True
    print(f"builds together: {time.monotonic() - started:.1f} s", flush=True)
    if failed:
        return 1
    for name, folder in CHECKS:
        if name == "settings_ini" and not in_ci:
            command = [sys.executable, str(TESTS / "run_tests.py"), "--settings-ini"]
        else:
            command = [str(SRC / "build" / "bin" / f"test_{name}.exe")]
        print(f"== test_{name}", flush=True)
        code = subprocess.run(command, cwd=folder).returncode
        if code:
            print(f"FAIL test_{name} ({code})")
            return 1
    print(f"all {len(CHECKS)} checks passed in {time.monotonic() - started:.1f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
