"""Give packaged DLLs explicit names without changing their executable sections."""

import pathlib
import re
import struct

import pefile


RUNTIME_NAMES = {
    "libplacebo-387.dll": "VideoProcessing.dll",
    "libdovi.dll": "DolbyVision.dll",
    "liblcms2-2.dll": "ColorManagement.dll",
    "libshaderc_shared.dll": "ShaderCompiler.dll",
    "libspirv-cross-c-shared.dll": "ShaderTranslator.dll",
    "vulkan-1.dll": "VulkanLoader.dll",
    "libstdc++-6.dll": "CppRuntime.dll",
    "libgcc_s_seh-1.dll": "CompilerRuntime.dll",
    "libwinpthread-1.dll": "Threading.dll",
}
OBSOLETE_RUNTIME_NAMES = frozenset(RUNTIME_NAMES) | {
    "franky." + name.lower() for name in RUNTIME_NAMES.values()
} | {"libplacebo-360.dll"}


def aligned(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def packaged_name(name):
    return RUNTIME_NAMES.get(name.lower(), name)


def write_packaged_binary(source, destination):
    """Relocate import/export filename strings into a new read-only PE section.

    Keep original offsets intact, including any debug overlay. Refuse signatures,
    unsupported delay imports and filename references outside known PE tables.
    This deliberately fails closed if a future dependency changes its loading.
    """
    source, destination = pathlib.Path(source), pathlib.Path(destination)
    original = source.read_bytes()
    pe = pefile.PE(data=original)
    try:
        if pe.FILE_HEADER.Machine != 0x8664:
            raise ValueError(f"Expected x64 binary: {source}")
        if pe.OPTIONAL_HEADER.DATA_DIRECTORY[4].Size:
            raise ValueError(f"Cannot rename a signed binary: {source}")
        known_strings = set()
        version_names = set()
        for group in getattr(pe, "FileInfo", []):
            for info in group:
                for table in getattr(info, "StringTable", []):
                    for key in (b"OriginalFilename", b"InternalName"):
                        if key in table.entries_offsets:
                            version_names.add(table.entries_offsets[key][1])
        changes = []
        for table in ("DIRECTORY_ENTRY_IMPORT", "DIRECTORY_ENTRY_DELAY_IMPORT"):
            for entry in getattr(pe, table, []):
                field = "Name" if table == "DIRECTORY_ENTRY_IMPORT" else "szName"
                if table == "DIRECTORY_ENTRY_DELAY_IMPORT" and not entry.struct.grAttrs & 1:
                    raise ValueError(f"Unsupported VA-based delay import: {source}")
                known_strings.add(pe.get_offset_from_rva(getattr(entry.struct, field)))
                old = entry.dll.decode("ascii")
                if old.lower() in RUNTIME_NAMES:
                    changes.append((entry.struct, field, packaged_name(old)))
                    if table == "DIRECTORY_ENTRY_IMPORT":
                        entry.struct.TimeDateStamp = 0
        if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
            export = pe.DIRECTORY_ENTRY_EXPORT.struct
            known_strings.add(pe.get_offset_from_rva(export.Name))
            if source.name.lower() in RUNTIME_NAMES:
                changes.append((export, "Name", packaged_name(source.name)))
        lower = original.lower()
        for old in RUNTIME_NAMES:
            for match in re.finditer(re.escape(old.encode("ascii")), lower):
                if match.start() not in known_strings:
                    raise ValueError(f"Unrecognized filename reference to {old} in {source}")
            for match in re.finditer(re.escape(old.encode("utf-16le")), lower):
                if match.start() not in version_names:
                    raise ValueError(f"Unrecognized UTF-16 filename reference to {old} in {source}")
        if not changes:
            destination.write_bytes(original)
            return

        section_offset = pe.sections[-1].get_file_offset() + 40
        first_data = min(s.PointerToRawData for s in pe.sections if s.PointerToRawData)
        if section_offset + 40 > min(first_data, pe.OPTIONAL_HEADER.SizeOfHeaders):
            raise ValueError(f"No free PE section header: {source}")
        if any(original[section_offset:section_offset + 40]) or pe.FILE_HEADER.NumberOfSections >= 96:
            raise ValueError(f"PE section header is unavailable: {source}")
        section_alignment = pe.OPTIONAL_HEADER.SectionAlignment
        file_alignment = pe.OPTIONAL_HEADER.FileAlignment
        rva = aligned(max(s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData) for s in pe.sections),
                      section_alignment)
        raw_offset = aligned(len(original), file_alignment)
        strings = bytearray()
        offsets = {}
        for owner, field, name in changes:
            if name not in offsets:
                offsets[name] = len(strings)
                strings.extend(name.encode("ascii") + b"\0")
            setattr(owner, field, rva + offsets[name])
        raw_size = aligned(len(strings), file_alignment)
        pe.FILE_HEADER.NumberOfSections += 1
        pe.OPTIONAL_HEADER.SizeOfImage = aligned(rva + len(strings), section_alignment)
        pe.OPTIONAL_HEADER.SizeOfInitializedData += raw_size
        pe.OPTIONAL_HEADER.DATA_DIRECTORY[11].VirtualAddress = 0  # Discard bound-import cache.
        pe.OPTIONAL_HEADER.DATA_DIRECTORY[11].Size = 0
        data = bytearray(pe.write())
        data[section_offset:section_offset + 40] = struct.pack(
            "<8sIIIIIIHHI", b".frnames", len(strings), rva, raw_size, raw_offset, 0, 0, 0, 0, 0x40000040
        )
        data.extend(b"\0" * (raw_offset - len(data)))
        data.extend(strings)
        data.extend(b"\0" * (raw_size - len(strings)))
        with pefile.PE(data=bytes(data)) as result:
            for before, after in zip(pe.sections, result.sections):
                if before.Characteristics & 0x20000000 and before.get_data() != after.get_data():
                    raise ValueError(f"Executable section changed: {source}")
            old_exports = [(s.ordinal, s.name, s.address, s.forwarder)
                           for s in getattr(getattr(pe, "DIRECTORY_ENTRY_EXPORT", None), "symbols", [])]
            new_exports = [(s.ordinal, s.name, s.address, s.forwarder)
                           for s in getattr(getattr(result, "DIRECTORY_ENTRY_EXPORT", None), "symbols", [])]
            if old_exports != new_exports:
                raise ValueError(f"Exported functions changed: {source}")
            struct.pack_into("<I", data, result.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum"),
                             result.generate_checksum())
        destination.write_bytes(data)
    finally:
        pe.close()


def verify_runtime(directory):
    """Reject stale imports and missing renamed dependencies."""
    directory = pathlib.Path(directory)
    expected = {name.lower() for name in RUNTIME_NAMES.values()}
    for path in directory.iterdir():
        if path.suffix.lower() not in (".dll", ".ax"):
            continue
        if path.name.lower() in OBSOLETE_RUNTIME_NAMES:
            raise ValueError(f"Obsolete runtime filename in package: {path}")
        with pefile.PE(str(path)) as pe:
            for table in ("DIRECTORY_ENTRY_IMPORT", "DIRECTORY_ENTRY_DELAY_IMPORT"):
                for entry in getattr(pe, table, []):
                    name = entry.dll.decode("ascii").lower()
                    if name in OBSOLETE_RUNTIME_NAMES:
                        raise ValueError(f"Stale import in {path}: {name}")
                    if name in expected and not (directory / entry.dll.decode("ascii")).is_file():
                        raise ValueError(f"Missing dependency for {path}: {name}")
