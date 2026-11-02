"""Validate menu PO data without running anything supplied by a translation PR."""
from pathlib import Path
import re

import polib

ROOT = Path(__file__).resolve().parents[2]
LANGUAGE_FILE = re.compile(r"translations/[A-Za-z][A-Za-z0-9_@-]*\.po\Z")
# The renderer uses printf arguments in their original order; positional
# arguments and changed width/precision modifiers are deliberately rejected.
PRINTF = re.compile(r"%(?:%|(?:\d+\$)?[-+#0 ']*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|I64|I32|[hljztLw])?[diuoxXfFeEgGaAcCsSpn])")


def placeholders(text):
    return [match.group() for match in PRINTF.finditer(text) if match.group() != "%%"]


def validate_catalog(text, template_text):
    if len(text.encode("utf-8")) > 2_000_000 or "\x00" in text:
        raise ValueError("Invalid or oversized PO file")
    catalog = polib.pofile(text, check_for_duplicates=True)
    polib.pofile(template_text, check_for_duplicates=True)
    if "utf-8" not in catalog.metadata.get("Content-Type", "").lower():
        raise ValueError("PO catalog must declare UTF-8")
    if not catalog.metadata.get("Language", "").strip():
        raise ValueError("Missing PO Language header")
    for entry in catalog:
        if entry.obsolete:
            continue
        if entry.msgctxt or entry.msgid_plural or entry.msgstr_plural:
            raise ValueError("Menu catalogs support singular entries without context only")
        # Weblate can lag behind a newly generated template. Old source texts
        # are harmless: the renderer ignores them and completeness only counts
        # entries present in the current template.
        if entry.msgstr and placeholders(entry.msgstr) != placeholders(entry.msgid):
            raise ValueError(f"Changed printf placeholders: {entry.msgid!r}")
    # Exercise the same serializer used to consume PO catalogs as well.
    catalog.to_binary()
    return len(catalog.translated_entries())


def main():
    template = (ROOT / "translations/FrankyRenderer.pot").read_text(encoding="utf-8-sig")
    for path in sorted((ROOT / "translations").glob("*.po")):
        if not LANGUAGE_FILE.fullmatch(path.relative_to(ROOT).as_posix()):
            raise ValueError(f"Invalid language filename: {path.name}")
        translated = validate_catalog(path.read_text(encoding="utf-8-sig"), template)
        print(f"{path.name}: valid, {translated} translated entries")


if __name__ == "__main__":
    main()
