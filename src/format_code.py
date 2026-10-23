"""Format the project's tracked C++ files without touching third-party sources."""

import argparse
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check formatting without changing files")
    parser.add_argument("--clang-format", default="clang-format", help="Path to clang-format 21.1.8")
    args = parser.parse_args()
    formatter = shutil.which(args.clang_format)
    if formatter is None:
        parser.error("clang-format is missing. Install requirements-dev.txt first.")

    version = subprocess.check_output([formatter, "--version"], text=True)
    if "version 21.1.8" not in version:
        parser.error("Use clang-format 21.1.8 to keep formatting consistent.")

    tracked = subprocess.check_output(["git", "ls-files"], cwd=ROOT, text=True).splitlines()
    files = [
        name
        for name in tracked
        if Path(name).parent == Path("src")
        and Path(name).suffix in (".cpp", ".h")
        and (ROOT / name).is_file()
    ]
    options = ["--dry-run", "--Werror"] if args.check else ["-i"]
    subprocess.run([formatter, "--style=file", *options, *files], cwd=ROOT, check=True)
    print(f"{'Checked' if args.check else 'Formatted'} {len(files)} project C++ files.")


if __name__ == "__main__":
    main()
