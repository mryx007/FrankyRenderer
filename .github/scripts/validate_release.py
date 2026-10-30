"""Fail before building if the requested stable version differs from the source."""
import os
from pathlib import Path
import re

def parts(version):
    if not re.fullmatch(r"(?:0|[1-9]\d*)\.[0-9](?:\.[1-9])?", version):
        raise ValueError("Use 1.1 or 1.1.1 through 1.1.9; after .9 increase the middle number")
    numbers = [int(value) for value in version.split(".")]
    return (*numbers, 0) if len(numbers) == 2 else tuple(numbers)


def next_version(version):
    major, minor, patch = parts(version)
    if patch < 9:
        return f"{major}.{minor}.{patch + 1}"
    if minor < 9:
        return f"{major}.{minor + 1}"
    return f"{major + 1}.0"


def main():
    root = Path(__file__).resolve().parents[2]
    version = os.environ["RELEASE_VERSION"]
    major, minor, patch = parts(version)
    previous = os.environ.get("PREVIOUS_VERSION")
    if previous and version != next_version(previous):
        raise ValueError(f"The next version after {previous} must be {next_version(previous)}")
    rc = (root / "src/Renderer/Renderer.rc").read_text()
    for field in ("FileVersion", "ProductVersion"):
        if f'VALUE "{field}", "{version}"' not in rc:
            raise ValueError(f"Commit the version update to Renderer.rc before releasing {version}")
    for field in ("FILEVERSION", "PRODUCTVERSION"):
        if not re.search(rf"^{field}\s+{major},{minor},{patch},0$", rc, re.MULTILINE):
            raise ValueError("Numeric resource version does not match: " + field)
    packager = "src/Tools/package_release.py" if (root / "src/Tools/package_release.py").is_file() else "src/Tools/package_variants.py"
    for file in ("src/Tools/package.py", packager):
        if not re.search(r'(?<![\d.])' + re.escape(version) + r'(?![\d.])', (root / file).read_text()):
            raise ValueError("Package version does not match: " + file)


if __name__ == "__main__":
    main()
