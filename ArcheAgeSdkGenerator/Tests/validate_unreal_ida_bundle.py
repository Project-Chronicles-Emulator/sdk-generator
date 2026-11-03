#!/usr/bin/env python3
"""Validate an ArcheAgeSdkGenerator Unreal IDA bundle without IDA dependencies."""

from __future__ import annotations

import argparse
import json
import py_compile
import re
import struct
import sys
from pathlib import Path


def read_pe(path: Path) -> dict:
    with path.open("rb") as stream:
        if stream.read(2) != b"MZ":
            raise AssertionError(f"{path}: missing MZ header")
        stream.seek(0x3C)
        pe_offset = struct.unpack("<I", stream.read(4))[0]
        stream.seek(pe_offset)
        if stream.read(4) != b"PE\0\0":
            raise AssertionError(f"{path}: missing PE signature")
        machine, section_count, timestamp = struct.unpack(
            "<HHI", stream.read(8)
        )
        stream.seek(pe_offset + 20)
        optional_size = struct.unpack("<H", stream.read(2))[0]
        stream.seek(pe_offset + 24)
        optional = stream.read(optional_size)
        magic = struct.unpack_from("<H", optional)[0]
        size_of_image = struct.unpack_from(
            "<I", optional, 0x38
        )[0]
        stream.seek(pe_offset + 24 + optional_size)
        sections = []
        for _ in range(section_count):
            section = stream.read(40)
            name = section[:8].rstrip(b"\0").decode("ascii", "replace")
            virtual_size, rva = struct.unpack_from("<II", section, 8)
            characteristics = struct.unpack_from("<I", section, 36)[0]
            sections.append((name, rva, virtual_size, characteristics))
    return {
        "machine": machine,
        "timeDateStamp": timestamp,
        "optionalHeaderMagic": magic,
        "sizeOfImage": size_of_image,
        "sections": sections,
    }


def number(value: int | str) -> int:
    return int(value, 0) if isinstance(value, str) else int(value)


def validate(directory: Path, pe_path: Path | None) -> None:
    expected = {
        directory / f"unreal.{extension}"
        for extension in ("py", "json", "h")
    }
    missing = [str(path) for path in expected if not path.is_file()]
    assert not missing, f"missing bundle files: {missing}"
    assert not list(directory.glob("*.tmp")), "temporary bundle files remain"

    py_compile.compile(str(directory / "unreal.py"), doraise=True)
    metadata = json.loads(
        (directory / "unreal.json").read_text(encoding="utf-8")
    )
    assert metadata["schemaVersion"] == 1
    arrays = (
        "globals", "types", "blueprintActors",
        "enums", "functions", "nativeTargets",
    )
    for key in arrays:
        assert isinstance(
            metadata[key], list
        ), f"{key} must be an array"

    # A structurally valid export can still contain invalid cooked layouts.
    # Reject fields extending beyond their containing object before IDA import.
    for item in metadata["types"]:
        if item["package"] == "BasicType":
            continue  # Symbolic native templates are not reflected fields.
        for field in item["fields"]:
            offset = number(field["offset"])
            size = number(field["size"])
            assert offset >= 0 and size >= 0 and offset + size <= number(item["maxSize"]), (
                f'{item["cppName"]}.{field["name"]}: field range '
                f'0x{offset:x}..0x{offset + size:x} exceeds size 0x{number(item["maxSize"]):x}'
            )

    size_of_image = number(metadata["module"]["sizeOfImage"])
    assert size_of_image > 0, "module SizeOfImage is missing"
    actionable = [
        *(number(item["rva"]) for item in metadata["globals"]),
        *(number(item["rva"]) for item in metadata["nativeTargets"]),
    ]
    assert all(0 < rva < size_of_image for rva in actionable), (
        "actionable RVA outside image"
    )

    targets = metadata["nativeTargets"]
    rvas = [number(target["rva"]) for target in targets]
    assert len(rvas) == len(set(rvas)), "native target RVAs are not unique"
    assert rvas == sorted(rvas), "native targets are not sorted by RVA"
    for target in targets:
        aliases = [alias["fullName"] for alias in target["aliases"]]
        assert aliases == sorted(aliases), (
            f"aliases not deterministic at {target['rva']}"
        )

    header = (directory / "unreal.h").read_text(
        encoding="utf-8", errors="replace"
    )
    for token in (
        "UObject", "UWorld", "FFrame_IDA_UE53",
        "FNamePool_IDA_UE53", "FUObjectArray_IDA",
    ):
        assert token in header, f"header missing {token}"
    game_types = [
        item["cppName"]
        for item in metadata["types"]
        if item["package"] not in {
            "BasicType", "CoreUObject", "Engine"
        }
    ]
    assert game_types, (
        "metadata has no game package type"
    )
    assert any(name in header for name in game_types[:100]), (
        "header has no sampled game package type"
    )
    blueprint_actors = metadata["blueprintActors"]
    assert blueprint_actors, "metadata has no Blueprint actor classes"
    assert all(
        actor["fullName"].startswith("/Game/")
        and actor["cppName"].startswith("A")
        and actor["cppName"].endswith("_C")
        for actor in blueprint_actors
    ), "invalid Blueprint actor index"
    indexed_names = [actor["cppName"] for actor in blueprint_actors]
    typed_names = {
        item["cppName"]
        for item in metadata["types"]
        if item.get("blueprintActor")
    }
    assert set(indexed_names) == typed_names, (
        "Blueprint actor index differs from marked types"
    )
    assert all(name in header for name in indexed_names), (
        "header missing Blueprint actor class"
    )
    marker = metadata["typeMarker"]
    assert marker in header, "header missing bundle type marker"

    sdk_root = directory.parent / "SDK"
    sdk_directory = sdk_root / "SDK"
    master_header = (sdk_root / "SDK.h").read_text(
        encoding="utf-8", errors="replace"
    )
    merged_headers = list(sdk_directory.glob("merged_*.h"))
    assert not merged_headers, (
        f"merged SDK package headers remain: {merged_headers[:3]}"
    )
    assert 'SDK/merged_' not in master_header, (
        "SDK.h still includes merged package headers"
    )
    declaration_counts = {
        actor["cppName"]: 0
        for actor in blueprint_actors
    }
    class_declaration = re.compile(r"^class\s+(\w+_C)\b", re.MULTILINE)
    for sdk_header in sdk_directory.glob("*.h"):
        sdk_text = sdk_header.read_text(
            encoding="utf-8", errors="replace"
        )
        for class_name in class_declaration.findall(sdk_text):
            if class_name in declaration_counts:
                declaration_counts[class_name] += 1

    for actor in blueprint_actors:
        file_name = f"{actor['cppName']}.h"
        actor_header = sdk_directory / file_name
        assert actor_header.is_file(), (
            f"missing Blueprint actor SDK header: {file_name}"
        )
        actor_text = actor_header.read_text(
            encoding="utf-8", errors="replace"
        )
        declaration = f"class {actor['cppName']}"
        assert declaration in actor_text, (
            f"{file_name} missing {declaration}"
        )
        include = f'#include "SDK/{file_name}"'
        assert include in master_header, (
            f"SDK.h missing Blueprint actor include: {file_name}"
        )
        assert declaration_counts[actor["cppName"]] == 1, (
            f"{actor['cppName']} must have exactly one SDK definition"
        )

    if pe_path:
        pe = read_pe(pe_path)
        module = metadata["module"]
        identity_fields = (
            "machine", "timeDateStamp",
            "optionalHeaderMagic", "sizeOfImage",
        )
        for key in identity_fields:
            assert number(module[key]) == pe[key], (
                f"PE identity mismatch: {key}"
            )
        executable_sections = [
            (rva, rva + size)
            for _, rva, size, flags in pe["sections"]
            if flags & 0x20000000
        ]
        assert all(
            any(
                start <= rva < end
                for start, end in executable_sections
            )
            for rva in rvas
        ), (
            "native target outside executable PE sections"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("ida_directory", type=Path)
    parser.add_argument("pe", nargs="?", type=Path)
    args = parser.parse_args()
    try:
        validate(args.ida_directory, args.pe)
    except (
        AssertionError, OSError, ValueError, KeyError,
        py_compile.PyCompileError,
    ) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print("PASS: Unreal IDA bundle valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
