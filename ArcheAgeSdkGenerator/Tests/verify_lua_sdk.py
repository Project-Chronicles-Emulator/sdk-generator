"""Exercise generated Lua SDK output and update-safe property resolution."""
from __future__ import annotations

import argparse
import importlib.util
import json
import re
from pathlib import Path
import subprocess
import tempfile

GENERATOR = Path(__file__).resolve().parents[2] / "generate_lua_sdk.py"
spec = importlib.util.spec_from_file_location("lua_sdk_generator", GENERATOR)
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


def fixture():
    field = {"name": "Level", "type": {"rendered": "int32_t", "propertyType": 8},
             "offset": 4321, "size": 4, "arrayDim": 1, "bitfield": False}
    base = {"package": "Parent", "kind": "class", "fullName": "/Script/Parent.Base",
            "cppName": "UBase", "supers": [], "fields": [field]}
    child = {"package": "Test", "kind": "class", "fullName": "/Script/Test.Child",
             "cppName": "UChild", "supers": ["UBase"], "fields": [
                 {**field, "name": "Weight", "size": 8,
                  "type": {"rendered": "double", "propertyType": 24}},
                 {**field, "name": "Enabled", "size": 1,
                  "type": {"rendered": "bool", "propertyType": 7}},
                 {**field, "name": "bPacked", "bitfield": True},
                 {**field, "name": "Array", "arrayDim": 3},
                 {**field, "name": "Duplicate"}, {**field, "name": "Duplicate"}]}
    return {"schemaVersion": 1, "types": [base, child], "functions": [{
        "ownerFullName": "/Script/Parent.Base", "fullName": "/Script/Parent.Base.Action",
        "cppName": "Action", "logicalSignature": "void UBase::Action()"}], "enums": [{
        "package": "Test", "fullName": "/Script/Test.EState", "cppName": "EState",
        "values": [{"name": "EState::Ready", "value": 1},
                   {"name": "EState::Ambiguous", "value": 2},
                   {"name": "EState::Ambiguous", "value": 3},
                   {"name": "EState__ShortName", "value": 5},
                   {"name": "名字", "value": 4}]}]}


LUA_TEST = r'''
local root, memoryLibrary = ...
local loaded = {}
function require(name)
    if loaded[name] then return loaded[name] end
    local path = name == "memory_sdk" and memoryLibrary or root .. "/" .. name .. ".lua"
    local value = assert(loadfile(path))()
    loaded[name] = value
    return value
end
local offset, reads, lookups, stale, mismatch, wrongParent = 12, 0, 0, false, false, false
game = {
    find_class = function(path)
        assert(path == "Test.Child" or path == "Parent.Base")
        return path == "Test.Child" and 10 or 11
    end,
    is_a = function(handle, class)
        assert(handle == 7)
        return not mismatch and not (class == 11 and wrongParent)
    end,
    address = function() if stale then return nil, "stale" end; return 1000 end,
    find_offset = function(owner, name)
        if owner == "Test.Child" and name == "Weight" then return 64 end
        if owner == "Test.Child" and name == "Enabled" then return 72 end
        assert(owner == "Parent.Base" and name == "Level")
        lookups = lookups + 1
        return offset
    end,
}
memory = {read = function(address, length)
    reads = reads + 1
    if length == 8 then assert(address == 1064); return string.pack("<d", 1.25) end
    if length == 1 then assert(address == 1072); return string.pack("<I1", 0) end
    assert(address == 1000 + offset and length == 4)
    return string.pack("<i4", offset)
end}
local UE = require("ue")
assert(UE.defer == nil)
assert(next(UE.classes) == nil) -- Packages load only when used.
assert(UE.Test.EState.Ready == 1)
assert(UE.Test.EState.ShortName == 5)
assert(UE.Test.EState["名字"] == 4)
assert(UE.Test.EState.Ambiguous == nil)
assert(UE.enum_ambiguities["Test.EState"][1] == "Ambiguous")
assert(not UE.Test.UChild.wrap(0))
assert(not UE.Test.UChild.wrap(7.5))
local object = assert(UE.Test.UChild.wrap(7))
assert(rawget(UE, "Parent") == nil)
assert(object:Get_Level() == 12)
assert(rawget(UE, "Parent") ~= nil)
offset = 40 -- Simulate a changed game layout without regenerating the SDK.
assert(object:Get("Level") == 40 and lookups == 2 and reads == 2)
wrongParent = true
assert(object:Get_Level() == nil and reads == 2 and lookups == 2)
wrongParent = false
offset = 0
assert(object:Get_Level() == nil and reads == 2)
offset = nil
assert(object:Get_Level() == nil and reads == 2)
offset = -1
assert(object:Get_Level() == nil and reads == 2)
offset = 40
assert(object:Get("bPacked") == nil)
assert(object:Get("Array") == nil)
assert(object:Get("Duplicate") == nil)
assert(object:Get("Unknown") == nil and reads == 2)
local method = assert(object:Method("Action"))
assert(method.path == "Parent.Base.Action" and not method.callable)
method.path = "corrupted"
assert(object:Method("Action").path == "Parent.Base.Action")
stale = true
assert(not object:IsValid())
assert(object:Get_Level() == nil and reads == 2)
stale = false
mismatch = true
assert(object:Get_Level() == nil and reads == 2)
assert(not UE.Test.UChild.wrap(7))
mismatch = false
assert(object:Get_Weight() == 1.25)
assert(object:Get_Enabled() == false)
print("Lua SDK runtime: PASS (offset relocation, inheritance, stale handles, fail closed)")
'''


def run(lua: Path, *arguments):
    subprocess.run([str(lua.resolve()), *map(str, arguments)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lua", required=True, type=Path)
    parser.add_argument("--generated", type=Path, help="also parse and load a complete generated SDK")
    args = parser.parse_args()
    header = GENERATOR.parent / "ArcheAgeSdkGenerator/Engine/enums.h"
    body = header.read_text(encoding="utf-8").split("enum class PropertyType {", 1)[1].split("};", 1)[0]
    names = re.findall(r"^\s*(\w+)\s*,?\s*$", body, re.MULTILINE)
    expected = {"FloatProperty": "f32", "ByteProperty": "u8", "BoolProperty": "bool",
                "IntProperty": "i32", "Int8Property": "i8", "Int16Property": "i16",
                "Int64Property": "i64", "UInt16Property": "u16", "UInt32Property": "u32",
                "UInt64Property": "u64", "DoubleProperty": "f64"}
    assert generator.KINDS == {names.index(name): kind for name, kind in expected.items()}
    assert generator.field_schema({"type": {"rendered": "delegate", "propertyType": 25},
                                   "size": 8}).get("kind") is None
    with tempfile.TemporaryDirectory(prefix="sharp-lua-sdk-") as temporary:
        root = Path(temporary)
        source = root / "unreal.json"
        source.write_text(json.dumps(fixture(), ensure_ascii=False), encoding="utf-8")
        output = root / "sdk"
        manifest = generator.generate(source, output)
        schema = generator.build_schema(fixture())
        assert "offset" not in json.dumps(schema).lower()
        assert manifest["classes"] == 2 and manifest["enums"] == 1
        types = generator.annotation("Test", schema["Test"])
        assert "function static_" in types and ".wrap(handle)" in types
        assert ":Method(name)" in types
        assert "---@class UE.Test\n" not in types
        assert "---@class UE.Test\n" in generator.package_annotation("Test", schema["Test"])
        hashes = manifest["files"]
        assert generator.generate(source, output)["files"] == hashes
        # A changed snapshot removes owned obsolete definitions without overwriting edits.
        obsolete = output / "obsolete.lua"
        obsolete.write_bytes(b"return {}\n")
        old = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
        old["files"]["obsolete.lua"] = generator.hashlib.sha256(obsolete.read_bytes()).hexdigest()
        (output / "manifest.json").write_text(json.dumps(old), encoding="utf-8")
        generator.generate(source, output)
        assert not obsolete.exists()
        original = (output / "ue.lua").read_bytes()
        (output / "ue.lua").write_bytes(b"-- user edit\n")
        try:
            generator.generate(source, output)
        except ValueError as error:
            assert "edited or unmanaged" in str(error)
        else:
            raise AssertionError("edited generated file overwritten")
        (output / "ue.lua").write_bytes(original)
        script = root / "test.lua"
        script.write_text(LUA_TEST, encoding="utf-8")
        run(args.lua, script, output.as_posix(),
            (generator.RUNTIME.parent / "memory_sdk.lua").as_posix())
        # Every generated file must parse, including editor-only declarations.
        roots = [output] + ([args.generated] if args.generated else [])
        files = [path.as_posix() for directory in roots for path in directory.rglob("*.lua")]
        check = root / "syntax.lua"
        check.write_text("for _, path in ipairs(" + generator.lua(files) +
                         ") do assert(loadfile(path)) end\n", encoding="utf-8")
        run(args.lua, check)
        if args.generated:
            packages = re.findall(r'UE.defer\("([A-Za-z_][A-Za-z0-9_]*)"',
                                  (args.generated / "ue.lua").read_text(encoding="utf-8"))
            check.write_text('package.path = ' + generator.lua(args.generated.resolve().as_posix() +
                             '/?.lua;') + ' .. package.path\nlocal sdk = require("ue")\n'
                             'assert(sdk.Engine.AActor and sdk.Aion2)\n'
                             'for _, name in ipairs(' + generator.lua(packages) +
                             ') do assert(sdk[name]) end\n'
                             'print("All generated packages loaded:", ' + str(len(packages)) +
                             ', "Lua memory MiB:", collectgarbage("count") / 1024)\n', encoding="utf-8")
            run(args.lua, check)
        bad = fixture()
        bad["schemaVersion"] = 2
        try:
            generator.build_schema(bad)
        except ValueError:
            pass
        else:
            raise AssertionError("unsupported schema accepted")
    print(f"Lua SDK generator: PASS ({len(files)} Lua files parsed)")


if __name__ == "__main__":
    main()
