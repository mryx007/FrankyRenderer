"""Fail before building if the requested stable version differs from the source."""
import os
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
version = os.environ["RELEASE_VERSION"]
if not re.fullmatch(r"\d+\.\d+\.\d+", version):
    raise ValueError("Use a version such as 1.0.10, without a v prefix")
rc = (root / "src/Renderer.rc").read_text()
for field in ("FileVersion", "ProductVersion"):
    if f'VALUE "{field}", "{version}"' not in rc:
        raise ValueError(f"Commit the version update to Renderer.rc before releasing {version}")
for file in ("src/package.py", "src/package_variants.py"):
    if version not in (root / file).read_text():
        raise ValueError("Package version does not match: " + file)
