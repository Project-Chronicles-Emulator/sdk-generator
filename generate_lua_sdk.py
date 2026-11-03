"""Generate a name-resolved Lua SDK from the C++ generator's shared IDA reflection export."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import zipfile
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "Sharp Engine/Scripting/lib/ue_runtime.lua"
KINDS = {5: "f32", 6: "u8", 7: "bool", 8: "i32", 9: "i8", 10: "i16",
         11: "i64", 12: "u16", 13: "u32", 14: "u64", 24: "f64"}


def lua(value):
    if isinstance(value, str):
        return '"' + ''.join('\\%03d' % byte if byte < 32 or byte >= 127 or byte in (34, 92)
                             else chr(byte) for byte in value.encode("utf-8")) + '"'
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        if not -(1 << 63) <= value < (1 << 63):
            raise ValueError("Lua integer out of range")
        return str(value)
    if isinstance(value, list):
        return "{" + ",".join(lua(item) for item in value) + "}"
    if isinstance(value, dict):
        return "{" + ",".join(f"[{lua(key)}]={lua(item)}"
                               for key, item in sorted(value.items())) + "}"
    raise ValueError(f"unsupported schema value: {type(value)}")


def reflected_path(name):
    if name.startswith("/Script/"):
        return name.removeprefix("/Script/")
    return name


def field_schema(field):
    result = {"type": field["type"]["rendered"]}
    kind = KINDS.get(field["type"]["propertyType"])
    sizes = {"u8": 1, "i8": 1, "bool": 1, "u16": 2, "i16": 2,
             "u32": 4, "i32": 4, "f32": 4, "u64": 8, "i64": 8, "f64": 8}
    if field.get("bitfield"):
        result["reason"] = "packed boolean needs live mask metadata"
    elif field.get("arrayDim", 1) not in (0, 1):
        result["reason"] = "fixed array needs live element layout metadata"
    elif not kind or field.get("size") != sizes[kind]:
        result["reason"] = "complex type needs live layout metadata"
    else:
        result["kind"] = kind
    return result


def build_schema(metadata):
    if metadata.get("schemaVersion") != 1:
        raise ValueError("unsupported reflection export schema")
    packages = defaultdict(list)
    types = metadata["types"]
    paths = defaultdict(list)
    for item in types:
        paths[item["cppName"]].append(item)
    methods = defaultdict(dict)
    for item in metadata.get("functions", []):
        owner = reflected_path(item["ownerFullName"])
        name = item["cppName"]
        descriptor = {"path": reflected_path(item["fullName"]),
                      "signature": item["logicalSignature"]}
        if name in methods[owner]:
            if methods[owner][name] == descriptor:
                continue
            name = item["fullName"].rsplit(".", 1)[-1]
            if name in methods[owner]:
                raise ValueError(f"duplicate reflected method: {owner}.{name}")
        methods[owner][name] = descriptor
    for item in types:
        path = reflected_path(item["fullName"])
        supers = []
        for name in item.get("supers", []):
            candidates = paths[name]
            local = [parent for parent in candidates if parent["package"] == item["package"]]
            candidates = local or candidates
            if len(candidates) == 1:
                supers.append({"path": reflected_path(candidates[0]["fullName"]),
                               "package": candidates[0]["package"],
                               "name": candidates[0]["cppName"]})
        fields = {}
        for field in item.get("fields", []):
            if field.get("generatedPadding"):
                continue
            if field["name"] in fields:
                fields[field["name"]] = {"type": field["type"]["rendered"],
                                         "reason": "ambiguous reflected member name"}
                continue
            fields[field["name"]] = field_schema(field)
        packages[item["package"]].append({"name": item["cppName"], "path": path,
            "kind": item["kind"], "supers": supers, "fields": fields,
            "methods": methods.get(path, {})})
    for item in metadata.get("enums", []):
        values = {}
        ambiguous = set()
        for value in item["values"]:
            name = value["name"].rsplit("::", 1)[-1]
            prefix = item["cppName"] + "__"
            if name.startswith(prefix):
                name = name.removeprefix(prefix)
            if name in values:
                if values[name] == value["value"]:
                    continue
                ambiguous.add(name)
            values[name] = value["value"]
        for name in ambiguous:
            del values[name]
        packages[item["package"]].append({"name": item["cppName"],
            "path": reflected_path(item["fullName"]), "kind": "enum", "values": values,
            "ambiguous": sorted(ambiguous)})
    for package, entries in packages.items():
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", package) or len(package) > 61:
            raise ValueError(f"package cannot be imported by the scripting host: {package}")
        names = [entry["name"] for entry in entries]
        if len(names) != len(set(names)):
            raise ValueError(f"duplicate type in package: {package}")
        entries.sort(key=lambda entry: entry["name"])
    return dict(sorted(packages.items()))


def annotation(package, entries):
    lines = ["---@meta", "-- Editor definitions only; do not require this file."]
    for entry in entries:
        qualified = f"UE.{package}.{entry['name']}"
        if entry["kind"] == "enum":
            lines.append(f"---@enum {qualified}")
            lines.append(f"local enum_{len(lines)} = " + lua(entry["values"]))
            continue
        if entry["kind"] == "struct":
            lines.append(f"---@class {qualified}.Static")
            lines.append("-- Struct metadata only; no fixed-layout memory reader is generated.")
            for name, field in entry["fields"].items():
                lines.append(f"-- {name}: {field['type']}")
            lines.append(f"local struct_{len(lines)} = {{}}")
            continue
        lines.append(f"---@class {qualified}.Static")
        lines.append(f"---@field wrap fun(handle: integer): {qualified}?, string?")
        static = f"static_{len(lines)}"
        lines.append(f"local {static} = {{}}")
        parents = ", ".join(f"UE.{parent['package']}.{parent['name']}"
                            for parent in entry["supers"])
        lines.append(f"---@class {qualified}" + (f" : {parents}" if parents else ""))
        lines.append("---@field handle integer")
        lines.append("---@field class_path string")
        lines.append("-- Read members with object:Get(name), returning value, error.")
        for name, field in entry["fields"].items():
            lines.append(f"-- {name}: {field['type']}" +
                         (f" (unsupported: {field['reason']})" if "reason" in field else ""))
        lines.extend([f"local type_{len(lines)} = {{}}", "---@param handle integer",
                      f"---@return {qualified}? object", "---@return string? error"])
        local = lines[-4].split()[1]
        lines.append(f"function {static}.wrap(handle) end")
        lines.extend(["---@param name string", "---@return number|boolean|nil value",
                      "---@return string? error", f"function {local}:Get(name) end",
                      "---@return boolean valid", "---@return string? error",
                      f"function {local}:IsValid() end", "---@param name string",
                      "---@return table? method", "---@return string? error",
                      f"function {local}:Method(name) end"])
        for name, field in entry["fields"].items():
            if field.get("kind") and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
                result = "boolean" if field["kind"] == "bool" else "number"
                lines.extend([f"---@return {result}? value", "---@return string? error",
                              f"function {local}:Get_{name}() end"])
        for name, method in entry["methods"].items():
            lines.append(f"-- {name}: {method['signature']} (declaration only)")
    return "\n".join(lines) + "\n"


def package_annotation(package, entries):
    lines = ["---@meta", f"---@class UE.{package}"]
    for entry in entries:
        suffix = "" if entry["kind"] == "enum" else ".Static"
        lines.append(f"---@field {entry['name']} UE.{package}.{entry['name']}{suffix}")
    lines.append("local package = {}")
    return "\n".join(lines) + "\n"


def write_generated_files(output, files):
    root = output.resolve()
    previous = output / "manifest.json"
    owned = json.loads(previous.read_text(encoding="utf-8"))["files"] if previous.exists() else {}
    stale = []
    for name in set(owned) | set(files):
        target = (root / name).resolve()
        if not target.is_relative_to(root):
            raise ValueError(f"generated path escapes output: {name}")
        if not target.exists():
            continue
        content = target.read_bytes()
        if name in files and content == files[name].encode("utf-8"):
            continue
        if name not in owned or hashlib.sha256(content).hexdigest() != owned[name]:
            raise ValueError(f"refusing to replace edited or unmanaged SDK file: {name}")
        if name not in files:
            stale.append(target)
    output.mkdir(parents=True, exist_ok=True)
    for name, content in files.items():
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8", newline="\n")
    for target in stale:
        target.unlink()


def generate(source: Path, output: Path, online=False):
    raw = source.read_bytes()
    packages = build_schema(json.loads(raw.decode("utf-8")))
    files = {"ue_runtime.lua": RUNTIME.read_text(encoding="utf-8"),
             "memory_sdk.lua": (RUNTIME.parent / "memory_sdk.lua").read_text(encoding="utf-8")}
    files["README.md"] = (
        "# Generated Lua SDK\n\n"
        "Copy the top-level .lua files into the Scripts/lib folder shown in game.\n"
        "Back up edited libraries before replacing them. Add definitions/ to your\n"
        "Lua Language Server workspace.library; do not execute those files.\n\n"
        "Use local UE = require(\"ue\"), then UE.Engine.AActor.wrap(handle).\n"
        "Read a scalar with object:Get(\"CustomTimeDilation\") or\n"
        "object:Get_CustomTimeDilation(). Both return value, error.\n\n"
        "Member offsets resolve through live FindOffset on every read. Missing\n"
        "offsets and invalid handles return errors. Structs and methods are\n"
        "declarations only; packed booleans and complex member types are unsupported.\n"
        "Changed names, types, and enum values still require regenerated definitions.\n\n"
        "Packages load lazily. manifest.json identifies the source reflection export\n"
        "and output hashes; this package has only been verified offline.\n")
    bootstrap = ['-- Generated reflected Lua SDK. No dumped member offsets.',
                 '---@type UE',
                 'local UE = require("ue_runtime").new()']
    files["definitions/ue.lua"] = "---@meta\n---@class UE\n" + "".join(
        f"---@field {package} UE.{package}\n" for package in packages)
    files["definitions/ue.lua"] += "---@field local_actor fun(): UE.Engine.AActor?, string?\n"
    files["definitions/ue.lua"] += "".join(
        f"---@field {name} table<string, table>\n"
        for name in ("classes", "enums", "structs", "enum_ambiguities"))
    files["definitions/ue.lua"] += "local UE = {}\nreturn UE\n"
    groups = [[]]
    group_bytes = 0
    for package, entries in packages.items():
        filename = f"ue_{package}"
        serialized = lua(entries)
        files[f"definitions/{filename}_package.lua"] = package_annotation(package, entries)
        if online:
            piece = f"[{lua(package)}]=function() return {serialized} end"
            length = len(piece.encode("utf-8"))
            if group_bytes + length > 7 * 1024 * 1024 and groups[-1]:
                groups.append([])
                group_bytes = 0
            if len(groups) > 8:
                raise ValueError("generated SDK exceeds the supported eight data libraries")
            groups[-1].append(piece)
            group_bytes += length
            bootstrap.append(f'UE.defer({lua(package)}, function() return '
                             f'require("ue_data_{len(groups) - 1}")[{lua(package)}]() end)')
        else:
            files[filename + ".lua"] = "-- Generated names and types only.\nreturn " + serialized + "\n"
            bootstrap.append(f'UE.defer({lua(package)}, function() return require({lua(filename)}) end)')
        for index in range(0, len(entries), 100):
            files[f"definitions/{filename}_{index // 100:03d}.lua"] = annotation(
                package, entries[index:index + 100])
    if online:
        for index, group in enumerate(groups):
            files[f"ue_data_{index}.lua"] = "-- Generated package loaders.\nreturn {" + ",".join(group) + "}\n"
    bootstrap.extend(['UE.defer = nil', 'return UE'])
    files["ue.lua"] = "\n".join(bootstrap) + "\n"
    # Validate the complete export before creating any output files.
    for name, content in files.items():
        if len(content.encode("utf-8")) > 8 * 1024 * 1024:
            raise ValueError(f"SDK file exceeds the host limit: {name}")
    write_generated_files(output, files)
    manifest = {"schemaVersion": 1, "sourceSha256": hashlib.sha256(raw).hexdigest(),
                "packages": len(packages),
                "classes": sum(e["kind"] == "class" for es in packages.values() for e in es),
                "structs": sum(e["kind"] == "struct" for es in packages.values() for e in es),
                "enums": sum(e["kind"] == "enum" for es in packages.values() for e in es),
                "files": {name: hashlib.sha256(content.encode("utf-8")).hexdigest()
                          for name, content in sorted(files.items())}}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="dump folder or IDA/unreal.json")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--online", action="store_true", help="combine packages into bounded download libraries")
    parser.add_argument("--release-assets", action="store_true", help="prepare the online SDK inputs and editor download")
    args = parser.parse_args()
    if args.release_assets and not args.online:
        parser.error("--release-assets requires --online")
    source = args.source / "IDA/unreal.json" if args.source.is_dir() else args.source
    output = args.output or source.parent.parent / "LuaSDK"
    result = generate(source, output, args.online)
    if args.release_assets:
        if not args.online:
            raise ValueError("release assets require --online")
        assets = ROOT / "Sharp Engine/Scripting/generated_sdk"
        assets.mkdir(parents=True, exist_ok=True)
        runtime_files = {name: digest for name, digest in result["files"].items()
                         if name.startswith("ue") and "/" not in name and name.endswith(".lua")}
        for name, digest in runtime_files.items():
            content = (output / name).read_bytes()
            if hashlib.sha256(content).hexdigest() != digest:
                raise ValueError(f"generated file changed: {name}")
            (assets / name).write_bytes(content)
        receipt = {**result, "files": runtime_files}
        (assets / "manifest.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        archive = ROOT / "Tools/lua-docs/public/downloads/sharp-aion2-ue-bundle.zip"
        archive.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
            for name in sorted(result["files"]):
                bundle.write(output / name, name)
            bundle.write(output / "manifest.json", "manifest.json")
    print(json.dumps({"output": str(output), **{k: v for k, v in result.items() if k != "files"}}))


if __name__ == "__main__":
    main()
