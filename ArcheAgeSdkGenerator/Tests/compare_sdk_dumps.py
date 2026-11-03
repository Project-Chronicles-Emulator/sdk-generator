#!/usr/bin/env python3
"""Compare two ArcheAgeSdkGenerator dumps via IDA/unreal.json and write SDK_COMPARE.md."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

DEFAULT_ROOT = Path(os.environ.get("ARCHEAGE_SDK_OUTPUT_ROOT", Path.cwd() / "out" / "dumps"))
LIST_CAP = 200
TRANSIENT_PREFIX = "/Engine/Transient."
SDK_INCLUDE_RE = re.compile(r'#include\s+"SDK/([^"]+)\.h"')


@dataclass
class TypeInfo:
    package: str
    kind: str
    cpp_name: str
    field_names: set[str] = field(default_factory=set)


@dataclass
class EnumInfo:
    package: str
    cpp_name: str
    value_names: set[str] = field(default_factory=set)


@dataclass
class FuncInfo:
    package: str
    owner: str
    cpp_name: str


@dataclass
class DumpIndex:
    path: Path
    module: dict[str, Any]
    packages: set[str]
    types: dict[str, TypeInfo]
    enums: dict[str, EnumInfo]
    functions: dict[str, FuncInfo]
    actors: set[str]


def json_path(dump_root: Path) -> Path:
    return dump_root / "IDA" / "unreal.json"


def load_metadata(dump_root: Path) -> dict[str, Any]:
    path = json_path(dump_root)
    if not path.is_file():
        raise FileNotFoundError(
            f"missing {path}; re-dump with current ArcheAgeSdkGenerator to get IDA/unreal.json"
        )
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schemaVersion") != 1:
        raise ValueError(
            f"{path}: unsupported schemaVersion {data.get('schemaVersion')!r}"
        )
    for key in ("types", "enums", "functions", "blueprintActors"):
        if key not in data or not isinstance(data[key], list):
            raise ValueError(f"{path}: missing or invalid array '{key}'")
    return data


def is_valid_dump(dump_root: Path) -> bool:
    try:
        load_metadata(dump_root)
    except (OSError, ValueError, json.JSONDecodeError, KeyError):
        return False
    return True


def build_index(dump_root: Path) -> DumpIndex:
    data = load_metadata(dump_root)
    packages: set[str] = set()
    types: dict[str, TypeInfo] = {}
    enums: dict[str, EnumInfo] = {}
    functions: dict[str, FuncInfo] = {}
    actors: set[str] = set()

    for item in data["types"]:
        full_name = item.get("fullName")
        if not full_name:
            continue
        package = item.get("package") or ""
        if package:
            packages.add(package)
        field_names = {
            field.get("name")
            for field in item.get("fields") or []
            if field.get("name")
        }
        types[full_name] = TypeInfo(
            package=package,
            kind=item.get("kind") or "",
            cpp_name=item.get("cppName") or "",
            field_names=field_names,
        )

    for item in data["enums"]:
        full_name = item.get("fullName")
        if not full_name:
            continue
        package = item.get("package") or ""
        if package:
            packages.add(package)
        value_names = {
            value.get("name")
            for value in item.get("values") or []
            if value.get("name")
        }
        enums[full_name] = EnumInfo(
            package=package,
            cpp_name=item.get("cppName") or "",
            value_names=value_names,
        )

    for item in data["functions"]:
        full_name = item.get("fullName")
        if not full_name:
            continue
        package = item.get("package") or ""
        if package:
            packages.add(package)
        functions[full_name] = FuncInfo(
            package=package,
            owner=item.get("owner") or "",
            cpp_name=item.get("cppName") or "",
        )

    for item in data["blueprintActors"]:
        full_name = item.get("fullName")
        if full_name:
            actors.add(full_name)
            package = item.get("package") or ""
            if package:
                packages.add(package)

    return DumpIndex(
        path=dump_root.resolve(),
        module=data.get("module") or {},
        packages=packages,
        types=types,
        enums=enums,
        functions=functions,
        actors=actors,
    )


def find_previous_dump(new_root: Path, root: Path) -> Path | None:
    new_resolved = new_root.resolve()
    if not root.is_dir():
        return None

    candidates: list[Path] = []
    for child in root.iterdir():
        if not child.is_dir():
            continue
        try:
            if child.resolve() == new_resolved:
                continue
        except OSError:
            continue
        if is_valid_dump(child):
            candidates.append(child)

    if not candidates:
        return None

    candidates.sort(
        key=lambda path: (path.stat().st_mtime, path.name),
        reverse=True,
    )
    return candidates[0]


def is_transient(full_name: str) -> bool:
    return full_name.startswith(TRANSIENT_PREFIX)


def split_transient(names: set[str]) -> tuple[list[str], list[str]]:
    main = sorted(name for name in names if not is_transient(name))
    noise = sorted(name for name in names if is_transient(name))
    return main, noise


def format_list(items: list[str], cap: int = LIST_CAP) -> list[str]:
    if len(items) <= cap:
        return items
    remaining = len(items) - cap
    return items[:cap] + [f"... and {remaining} more"]


def md_bullet_section(title: str, items: list[str], empty: str = "_None_") -> list[str]:
    lines = [f"## {title}", ""]
    if not items:
        lines.extend([empty, ""])
        return lines
    for item in format_list(items):
        lines.append(f"- `{item}`")
    lines.append("")
    return lines


def packages_from_sdk_h(dump_root: Path) -> set[str] | None:
    master = dump_root / "SDK" / "SDK.h"
    if not master.is_file():
        return None
    text = master.read_text(encoding="utf-8", errors="replace")
    names = {
        match.group(1)
        for match in SDK_INCLUDE_RE.finditer(text)
        if match.group(1) not in {"BasicType", "EngineInternals"}
    }
    return names or None


def write_report(old: DumpIndex, new: DumpIndex, out_path: Path) -> dict[str, Any]:
    pkg_added = sorted(new.packages - old.packages)
    pkg_removed = sorted(old.packages - new.packages)

    types_added = set(new.types) - set(old.types)
    types_removed = set(old.types) - set(new.types)
    types_added_main, types_added_noise = split_transient(types_added)
    types_removed_main, types_removed_noise = split_transient(types_removed)

    enums_added = sorted(set(new.enums) - set(old.enums))
    enums_removed = sorted(set(old.enums) - set(new.enums))

    funcs_added_set = set(new.functions) - set(old.functions)
    funcs_removed_set = set(old.functions) - set(new.functions)
    funcs_added_main, funcs_added_noise = split_transient(funcs_added_set)
    funcs_removed_main, funcs_removed_noise = split_transient(funcs_removed_set)

    actors_added = sorted(new.actors - old.actors)
    actors_removed = sorted(old.actors - new.actors)

    field_additions: list[str] = []
    for full_name in sorted(set(old.types) & set(new.types)):
        if is_transient(full_name):
            continue
        added_fields = sorted(
            new.types[full_name].field_names - old.types[full_name].field_names
        )
        if not added_fields:
            continue
        field_additions.append(
            f"{full_name} (+{len(added_fields)}): {', '.join(added_fields[:40])}"
            + (
                f", ... and {len(added_fields) - 40} more"
                if len(added_fields) > 40
                else ""
            )
        )

    lines: list[str] = [
        "# SDK Compare",
        "",
        f"- **Old:** `{old.path}`",
        f"- **New:** `{new.path}`",
    ]
    for label, module in (("Old module", old.module), ("New module", new.module)):
        stamp = module.get("timeDateStamp")
        size = module.get("sizeOfImage")
        name = module.get("name")
        bits = []
        if name:
            bits.append(f"name=`{name}`")
        if stamp is not None:
            bits.append(f"timeDateStamp=`{stamp}`")
        if size is not None:
            bits.append(f"sizeOfImage=`{size}`")
        if bits:
            lines.append(f"- **{label}:** " + ", ".join(bits))
    lines.extend(
        [
            "",
            "## Summary",
            "",
            f"- Packages added: **{len(pkg_added)}**",
            f"- Packages removed: **{len(pkg_removed)}**",
            f"- Types added: **{len(types_added_main)}** "
            f"(+{len(types_added_noise)} Transient noise)",
            f"- Types removed: **{len(types_removed_main)}** "
            f"(+{len(types_removed_noise)} Transient noise)",
            f"- Enums added: **{len(enums_added)}**",
            f"- Enums removed: **{len(enums_removed)}**",
            f"- Functions added: **{len(funcs_added_main)}** "
            f"(+{len(funcs_added_noise)} Transient noise)",
            f"- Functions removed: **{len(funcs_removed_main)}** "
            f"(+{len(funcs_removed_noise)} Transient noise)",
            f"- Blueprint actors added: **{len(actors_added)}**",
            f"- Blueprint actors removed: **{len(actors_removed)}**",
            f"- Shared types with new fields: **{len(field_additions)}**",
            "",
        ]
    )

    lines.extend(md_bullet_section("Packages added", pkg_added))
    lines.extend(md_bullet_section("Packages removed", pkg_removed))
    lines.extend(md_bullet_section("Types added", types_added_main))
    lines.extend(md_bullet_section("Types removed", types_removed_main))
    lines.extend(md_bullet_section("Enums added", enums_added))
    lines.extend(md_bullet_section("Enums removed", enums_removed))
    lines.extend(md_bullet_section("Functions added", funcs_added_main))
    lines.extend(md_bullet_section("Functions removed", funcs_removed_main))
    lines.extend(md_bullet_section("Blueprint actors added", actors_added))
    lines.extend(md_bullet_section("Blueprint actors removed", actors_removed))
    lines.extend(
        md_bullet_section("Shared types with new fields", field_additions)
    )

    lines.extend(
        [
            "## Noise (Transient)",
            "",
            f"- Types added under `{TRANSIENT_PREFIX}`: "
            f"**{len(types_added_noise)}**",
            f"- Types removed under `{TRANSIENT_PREFIX}`: "
            f"**{len(types_removed_noise)}**",
            f"- Functions added under `{TRANSIENT_PREFIX}`: "
            f"**{len(funcs_added_noise)}**",
            f"- Functions removed under `{TRANSIENT_PREFIX}`: "
            f"**{len(funcs_removed_noise)}**",
            "",
        ]
    )
    if types_added_noise:
        lines.append("Sample Transient types added:")
        lines.append("")
        for name in format_list(types_added_noise, cap=20):
            lines.append(f"- `{name}`")
        lines.append("")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    return {
        "packages_added": pkg_added,
        "packages_removed": pkg_removed,
        "types_added": len(types_added_main),
        "types_removed": len(types_removed_main),
        "types_added_noise": len(types_added_noise),
        "enums_added": len(enums_added),
        "enums_removed": len(enums_removed),
        "functions_added": len(funcs_added_main),
        "functions_removed": len(funcs_removed_main),
        "actors_added": len(actors_added),
        "actors_removed": len(actors_removed),
        "fields_changed_types": len(field_additions),
        "out": str(out_path),
    }


def write_degraded_sdk_h_report(
    old_root: Path,
    new_root: Path,
    out_path: Path,
) -> dict[str, Any]:
    old_pkgs = packages_from_sdk_h(old_root)
    new_pkgs = packages_from_sdk_h(new_root)
    if old_pkgs is None or new_pkgs is None:
        raise FileNotFoundError(
            "neither IDA/unreal.json nor SDK/SDK.h package lists are available"
        )
    added = sorted(new_pkgs - old_pkgs)
    removed = sorted(old_pkgs - new_pkgs)
    lines = [
        "# SDK Compare (degraded)",
        "",
        "_No `IDA/unreal.json` on one or both dumps; package list only from `SDK/SDK.h`._",
        "",
        f"- **Old:** `{old_root.resolve()}`",
        f"- **New:** `{new_root.resolve()}`",
        "",
        f"- Packages added: **{len(added)}**",
        f"- Packages removed: **{len(removed)}**",
        "",
    ]
    lines.extend(md_bullet_section("Packages added", added))
    lines.extend(md_bullet_section("Packages removed", removed))
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return {
        "packages_added": added,
        "packages_removed": removed,
        "types_added": 0,
        "types_removed": 0,
        "types_added_noise": 0,
        "enums_added": 0,
        "enums_removed": 0,
        "functions_added": 0,
        "functions_removed": 0,
        "actors_added": 0,
        "actors_removed": 0,
        "fields_changed_types": 0,
        "out": str(out_path),
        "degraded": True,
    }


def print_summary(
    old_path: Path,
    new_path: Path,
    summary: dict[str, Any],
) -> None:
    print(f"Old: {old_path}")
    print(f"New: {new_path}")
    if summary.get("degraded"):
        print("Mode: degraded (SDK.h packages only)")
    pkgs_added = summary["packages_added"]
    pkgs_removed = summary["packages_removed"]
    print(
        "Packages added "
        f"({len(pkgs_added)}): {', '.join(pkgs_added) if pkgs_added else '(none)'}"
    )
    print(
        "Packages removed "
        f"({len(pkgs_removed)}): "
        f"{', '.join(pkgs_removed) if pkgs_removed else '(none)'}"
    )
    print(
        "Types added/removed: "
        f"{summary['types_added']}/{summary['types_removed']} "
        f"(Transient noise +{summary['types_added_noise']})"
    )
    print(
        "Enums added/removed: "
        f"{summary['enums_added']}/{summary['enums_removed']}"
    )
    print(
        "Functions added/removed: "
        f"{summary['functions_added']}/{summary['functions_removed']}"
    )
    print(
        "Blueprint actors added/removed: "
        f"{summary['actors_added']}/{summary['actors_removed']}"
    )
    print(f"Shared types with new fields: {summary['fields_changed_types']}")
    print(f"Report: {summary['out']}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare ArcheAgeSdkGenerator dumps via IDA/unreal.json"
    )
    parser.add_argument("--new", type=Path, required=True, help="New dump root")
    parser.add_argument(
        "--old",
        type=Path,
        help="Baseline dump root (default: newest valid dump under --root)",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=DEFAULT_ROOT,
        help=f"Dump root for auto-old (default: {DEFAULT_ROOT})",
    )
    parser.add_argument(
        "--out",
        type=Path,
        help="Output markdown path (default: <new>/SDK_COMPARE.md)",
    )
    args = parser.parse_args()

    new_root = args.new
    out_path = args.out or (new_root / "SDK_COMPARE.md")

    old_root = args.old
    if old_root is None:
        old_root = find_previous_dump(new_root, args.root)
        if old_root is None:
            print(
                f"No prior valid dump under {args.root} "
                f"(need IDA/unreal.json schemaVersion=1)",
                file=sys.stderr,
            )
            return 2

    try:
        if json_path(new_root).is_file() and json_path(old_root).is_file():
            old_index = build_index(old_root)
            new_index = build_index(new_root)
            summary = write_report(old_index, new_index, out_path)
        else:
            # Degraded path only when both sides lack usable JSON but have SDK.h.
            if json_path(new_root).is_file() ^ json_path(old_root).is_file():
                missing = old_root if not json_path(old_root).is_file() else new_root
                raise FileNotFoundError(
                    f"missing {json_path(missing)}; re-dump with current "
                    "ArcheAgeSdkGenerator to get IDA/unreal.json"
                )
            summary = write_degraded_sdk_h_report(old_root, new_root, out_path)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1

    print_summary(old_root.resolve(), new_root.resolve(), summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
